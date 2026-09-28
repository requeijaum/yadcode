/* SPDX-License-Identifier: Apache-2.0 */
/*
 * pio_vm.c - Interpretador de PIO, ciclo a ciclo, para o PC.
 *
 * PORQUE ISTO EXISTE
 * ------------------
 * O ciclo de acesso do barramento G1 tem de respeitar os tempos minimos
 * da ATA-3 Tabela 22 (ver doc 03). Um erro de um ciclo no `nop [n]` e'
 * invisivel numa revisao e' um drive que nao boota - e nao ha hardware
 * para descobrir isso.
 *
 * Este interpretador executa as MESMAS palavras de 16 bits que o
 * pioasm produz, sobre um modelo de pinos, e mede os tempos. E' o unico
 * jeito de validar o timing sem uma placa.
 *
 * E' por isto que o codigo de encoding nao esta aqui escrito a mao: foi
 * obtido empiricamente do pioasm 2.3.1, e test_timing.c fixa os valores
 * para que uma mudanca no pioasm seja detectada.
 *
 * NAO E' um emulador de RP2350. Nao modela o RP2350 todo - so as
 * instrucoes que os nossos .pio usam, e erro-se se aparecer outra.
 */
#include <string.h>
#include "pio_vm.h"

/* ------------------------------------------------------------------ */
/* Decodificacao                                                       */
/* ------------------------------------------------------------------ */
/*
 * Encoding de 16 bits, confirmado contra o pioasm 2.3.1
 * (cada linha abaixo foi assemblada e o hex copiado para aqui):
 *
 *   WAIT  0x2000 | pol<<7 | src<<5 | index
 *         src: 0=gpio, 1=pin, 2=irq
 *   JMP   0x0000 | cond<<5 | addr      (cond ocupa os bits 7..5)
 *         cond: 0 always, 1 !x, 2 x--, 3 !y, 4 y--, 5 x!=y, 6 pin, 7 !osre
 *         so' 0, 1, 2, 4 e 5 sao modelados; o resto para a execucao
 *   IN    0x4000 | dest<<5 | count
 *   OUT   0x6000 | src<<5  | count
 *   PUSH  0x8020          PULL 0x80a0   (so' estas duas formas)
 *   NOP   0xa000 | delay<<8 | 0x0042
 *   IRQ   0xc000 | irq
 *   SET   0xe000 | dest<<5 | data
 *
 * MOV: 101 | DDD | 0 | OO | SSS, isto e' destino nos bits 7..5,
 * operacao nos bits 4..3 (00 nenhuma, 01 inverte, 10 reverte bits)
 * e fonte nos bits 2..0. Confirmado contra o pioasm:
 *   mov pins, x  0xa001   mov x, null    0xa023
 *   mov x, status 0xa025   mov x, isr     0xa026
 *   mov y, ~x    0xa049   mov x, ::y     0xa032
 *   mov x, y     0xa022   mov y, x       0xa041
 *   mov isr, x   0xa0c1   mov isr, pins  0xa0c0
 *   mov osr, x   0xa0e1   mov exec, x    0xa081
 *   mov pindirs, x 0xa061  nop           0xa042
 *   nop [3]      0xa342   nop [5]        0xa542
 *
 * Destinos: 0=PINS 1=X 2=Y 3=PINDIRS 4=EXEC 6=ISR 7=OSR (5 reservado).
 * Fontes: 0=PINS 1=X 2=Y 3=NULL 5=STATUS 6=ISR 7=OSR (4 reservado;
 * `pindirs` nao e' fonte valida).
 *
 * LIMITACOES CONHECIDAS (nenhum dos nossos .pio precisa disto):
 *   - side-set/delay em instrucoes que nao NOP e' ignorado;
 *   - `in`/`out` com count 0 (= 32 no hardware) e' tratado como 0;
 *   - a FIFO simulada tem 32 palavras e nunca bloqueia no `push`,
 *     ao contrario da FIFO real de 8;
 *   - `jmp pin` nao e' modelado: o pino testado vem da configuracao
 *     da SM (sm_config_set_jmp_pin), nao do campo de endereco.
 */
int pio_vm_decode(uint16_t v, const char **name)
{
    switch (PIO_OPCODE(v)) {
    case 0x2000: *name = "wait";     return 1;
    case 0x4000: *name = "in";       return 1;
    case 0x6000: *name = "out";      return 1;
    case 0x8000: *name = "push/pull";return 1;
    case 0xc000: *name = "irq";     return 1;
    case 0x0000: *name = "jmp";      return 1;
    case 0xa000: *name = "nop/mov";  return 1;
    case 0xe000: *name = "set";      return 1;
    default:     *name = "?";        return 0;
    }
}

/* Conditions de JMP, nos bits 7..5. So' as marcadas com (*) sao
 * modeladas; as outras param a execucao (return). */
#define JMP_ALWAYS 0x0              /* (*) */
#define JMP_NOT_X  0x1              /* (*) */
#define JMP_X_DEC  0x2              /* (*) */
#define JMP_Y_DEC  0x4              /* (*) */
#define JMP_X_NE_Y 0x5              /* (*) */
/* 0x3 = !y, 0x6 = pin (pino da configuracao da SM, nao do endereco),
 * 0x7 = !osre: nao modelados. */

/* Destinos de MOV, bits 7..5. Confirmado pelo pioasm (ver acima). */
#define MOV_PINS 0
#define MOV_X    1
#define MOV_Y    2
#define MOV_PINDIRS 3
#define MOV_EXEC 4
#define MOV_ISR  6
#define MOV_OSR  7

/*
 * Fontes de MOV, bits 2..0. Confirmado pelo pioasm (ver acima).
 * ATENCAO: o valor 0 e' PINS, nao NULL; NULL e' 3.
 */
#define SRC_PINS    0
#define SRC_X       1
#define SRC_Y       2
#define SRC_NULL    3
#define SRC_STATUS  5
#define SRC_ISR     6
#define SRC_OSR     7

static unsigned nop_delay(uint16_t v) { return (unsigned)((v >> 8) & 0x0f); }
static unsigned wait_pol(uint16_t v)  { return (unsigned)((v >> 7) & 1); }
static unsigned wait_src(uint16_t v)  { return (unsigned)((v >> 5) & 0x03); }
static unsigned wait_idx(uint16_t v)  { return (unsigned)(v & 0x1f); }
static unsigned io_count(uint16_t v)  { return (unsigned)(v & 0x1f); }
static unsigned jmp_cond(uint16_t v)   { return (unsigned)((v >> 5) & 0x07); }
static unsigned jmp_addr(uint16_t v)   { return (unsigned)(v & 0x1f); }
static unsigned mov_dest(uint16_t v)   { return (unsigned)((v >> 5) & 0x07); }
static unsigned mov_src(uint16_t v)    { return (unsigned)(v & 0x07); }
static unsigned mov_op(uint16_t v)     { return (unsigned)((v >> 3) & 0x03); }

/*
 * Distingue MOV de NOP.
 *
 * O pioasm nao emite opcodes distintos: `nop` e' literalmente
 * `mov y, y`, e `nop [n]` e' o mesmo com o campo de shift preenchido
 * (nop = 0xa042, nop[3] = 0xa342, nop[5] = 0xa542).
 *
 * Confirmado com pioasm -o hex -v 1:
 *   mov y,x = 0xa041   mov x,y = 0xa022   nop = 0xa042
 *
 * Logo: e' NOP se e so se o destino e Y, a fonte tambem e Y e a
 * operacao e' "nenhuma". Um `mov y, ~y` tem o mesmo destino e fonte
 * mas op != 0, e por isso cai no ramo MOV (que o rejeita, porque as
 * operacoes especiais nao sao modeladas).
 */
static int is_mov(uint16_t v)
{
    return !(mov_dest(v) == MOV_Y && mov_src(v) == SRC_Y && mov_op(v) == 0);
}

static void push32(pio_vm_t *vm, uint32_t w)
{
    vm->fifo[vm->fifo_head & (PIO_VM_STACK - 1)] = w;
    vm->fifo_head++;
}

static uint32_t pull32(pio_vm_t *vm)
{
    uint32_t w = 0;
    if (vm->fifo_tail != vm->fifo_head) {
        w = vm->fifo[vm->fifo_tail & (PIO_VM_STACK - 1)];
        vm->fifo_tail++;
    }
    return w;
}

void pio_vm_reset_pc(pio_vm_t *vm)
{
    vm->pc = 0;
    vm->blocked = 0;
}

void pio_vm_load(pio_vm_t *vm, const uint16_t *prog, size_t len, uint16_t wrap)
{
    if (len > PIO_VM_STACK) len = PIO_VM_STACK;
    memset(vm, 0, sizeof *vm);
    if (len == 0) return;   /* sem programa: run() nao deve ser chamado */
    memcpy(vm->prog, prog, len * sizeof vm->prog[0]);
    vm->len = (uint16_t)len;
    vm->pins = ~0u;                 /* pinos altos soltos */
    vm->pc = 0;
    (void)wrap;
}

uint32_t pio_vm_run(pio_vm_t *vm, uint16_t stop_pc, uint32_t max_cycles)
{
    uint32_t done = 0;
    const char *nm;

    if (vm->len == 0) return 0;
    while (done < max_cycles) {
        uint16_t v = vm->prog[vm->pc % vm->len];
        uint16_t opcode = PIO_OPCODE(v);
        unsigned cyc = 1;

        if (!pio_vm_decode(v, &nm)) return done;   /* instrucao nao suportada */

        switch (opcode) {
        case 0x2000: {                              /* WAIT */
            unsigned pol = wait_pol(v);
            unsigned src = wait_src(v);
            unsigned idx = wait_idx(v);
            int level;

            if (src != PIO_WAIT_PIN) return done;  /* gpio/irq: nao modelado */
            level = (int)((vm->pins >> idx) & 1u);

            if (level != (int)pol) {
                /* O pino ainda nao esta no nivel pedido: bloqueia. */
                vm->blocked = 1;
                vm->blocked_pc = vm->pc;
                return done;
            }
            if (vm->blocked && vm->blocked_pc == vm->pc) {
                /*
                 * A borda aconteceu agora. Este ciclo e' o instante
                 * de referencia para t5, t3, t4 e t6.
                 */
                vm->blocked = 0;
                vm->cycle++;
                if (pol) vm->last_wait_rise = vm->cycle;
                else    vm->last_wait_fall = vm->cycle;
                done++;
                vm->pc++;
                if (vm->pc >= vm->len) vm->pc = 0;
                continue;
            }
            break;   /* condicao ja satisfeita: passa de imediato */
        }

        case 0x4000: {                              /* IN */
            unsigned dest = (v >> 5) & 0x07, n = io_count(v);
            uint32_t mask = (n >= 32) ? 0xffffffffu : ((1u << n) - 1u);
            uint32_t val = vm->pins & mask;
            if (dest == PIO_F_PINS) {
                /* `in pins, N` desloca os pinos de entrada para o ISR. */
                vm->isr = (vm->isr << n) | val;
                vm->in_cycle = vm->cycle;
                vm->in_words++;
            }
            break;
        }

        case 0x6000: {                              /* OUT */
            unsigned src = (v >> 5) & 0x07, n = io_count(v);
            uint32_t val = 0;
            if (src == PIO_F_PINS) {
                uint32_t mask = (n >= 32) ? 0xffffffffu : ((1u << n) - 1u);
                val = vm->osr;
                vm->out_cycle = vm->cycle;
                vm->out_words++;
                vm->pins = (vm->pins & ~mask) | (val & mask);
                vm->side = val;
                vm->side_active = 1;
            } else if (src == PIO_F_X)   val = vm->x;
            else if (src == PIO_F_Y)     val = vm->y;
            else if (src == PIO_F_NULL)  val = pull32(vm);
            else if (src == PIO_S_ISR)   val = vm->isr;
            else if (src == PIO_S_OSR)   val = vm->osr;
            else                         val = pull32(vm);
            vm->osr = val;
            break;
        }

        case 0x8000:                                /* PUSH / PULL */
            if (v == 0x8020)        { push32(vm, vm->isr); vm->pushed++; }
            else if (v == 0x80a0) vm->osr = pull32(vm);
            else return done;     /* push/pull condicional: nao modelado */
            break;

        case 0x0000: {                              /* JMP */
            unsigned cond = jmp_cond(v);
            unsigned addr = jmp_addr(v);
            int take = 0;

            (void)addr;   /* so' o `jmp pin` precisaria dele, e nao e' modelado */

            switch (cond) {
            case JMP_ALWAYS: take = 1; break;
            case JMP_NOT_X:  take = !vm->x; break;
            case JMP_X_DEC:  take = 1; vm->x--; break;
            case JMP_Y_DEC:  take = 1; vm->y--; break;
            case JMP_X_NE_Y: take = (vm->x != vm->y); break;
            default: return done;    /* !y, pin e !osre: nao modelados */
            }
            if (take) {
                vm->pc = addr; done++; vm->cycle++;
                continue;
            }
            (void)0;
            break;
        }

        case 0xa000: {                              /* NOP ou MOV */
            if (is_mov(v)) {
                unsigned d = mov_dest(v), s = mov_src(v), op = mov_op(v);
                uint32_t src = 0;

                switch (s) {
                case SRC_PINS:   src = vm->pins; break;
                case SRC_X:      src = vm->x; break;
                case SRC_Y:      src = vm->y; break;
                case SRC_NULL:   src = 0; break;
                default:         return done;   /* STATUS/ISR/OSR: nao modelados */
                }
                if (op) {
                    /* Operacoes especiais (inverte/reverte bits) nao sao
                     * usadas pelos nossos programas. */
                    return done;
                }
                switch (d) {
                case MOV_X:      vm->x = src; break;
                case MOV_Y:      vm->y = src; break;
                case MOV_ISR:    vm->isr = src; break;
                case MOV_OSR:    vm->osr = src; break;
                case MOV_PINDIRS: break;   /* direccao dos pinos: ignorado */
                case MOV_EXEC:   vm->x = src & 0x1f; break;
                default:         return done;   /* PINS/reservado: nao modelados */
                }
                break;
            }
            cyc = nop_delay(v) + 1;
            break;
        }

        case 0xc000:                                /* IRQ */
            /* O IRQ nao altera o estado da PIO: serve so para acordar
             * a CPU. Contamo-lo para o teste poder verifica-lo. */
            vm->irqs++;
            break;

        case 0xe000: {                              /* SET */
            unsigned dest = (v >> 5) & 0x07, data = v & 0x1f;
            if (dest == PIO_S_PINS) {
                vm->pins = (vm->pins & ~0x1fu) | (data & 0x1fu);
                vm->side = data;
                vm->side_active = 1;
            } else if (dest == PIO_S_X)  vm->x = data;
            else if (dest == PIO_S_Y)    vm->y = data;
            break;
        }

        default:
            return done;
        }

        vm->cycle += cyc;
        done += cyc;
        vm->pc++;
        if (vm->pc >= vm->len) vm->pc = 0;      /* wrap */
        if (stop_pc != 0xffffu && vm->pc == stop_pc) break;
        if (vm->blocked) return done;
    }
    return done;
}

void pio_vm_drive(pio_vm_t *vm, unsigned pin, int level, uint32_t cycles,
                  uint16_t stop_pc, uint32_t *elapsed)
{
    uint32_t start = vm->cycle;
    if (level) vm->pins |=  (1u << pin);
    else       vm->pins &= ~(1u << pin);
    if (elapsed) *elapsed = 0;
    pio_vm_run(vm, stop_pc, cycles);
    if (elapsed) *elapsed = vm->cycle - start;
}

uint32_t pio_vm_fifo_avail(const pio_vm_t *vm)
{
    return vm->fifo_head - vm->fifo_tail;
}

uint32_t pio_vm_fifo_pop(pio_vm_t *vm)
{
    uint32_t w = 0;
    if (vm->fifo_tail != vm->fifo_head) {
        w = vm->fifo[vm->fifo_tail & (PIO_VM_STACK - 1)];
        vm->fifo_tail++;
    }
    return w;
}
