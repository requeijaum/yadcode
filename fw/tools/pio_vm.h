/* SPDX-License-Identifier: Apache-2.0 */
/*
 * pio_vm.h - Interpretador de PIO, ciclo a ciclo, para o PC.
 *
 * PORQUE ISTO EXISTE
 * ------------------
 * O ciclo de acesso do barramento G1 tem de respeitar os tempos minimos
 * da ATA-3 Tabela 22 (ver doc 03). Um erro de um ciclo no `nop [n]` e'
 * invisivel numa revisao e' um drive que nao boota - e nao ha hardware
 * para descobrir isso.
 *
 * Este interpretador executa as MISMAS palavras de 16 bits que o pioasm
 * produz, sobre um modelo de pinos, e mede os tempos. E' o unico jeito
 * de validar o timing sem uma placa.
 *
 * NAO E' um emulador de RP2350. Nao modela o RP2350 todo - so as
 * instrucoes que os nossos .pio usam, e erro-se se aparecer outra.
 *
 * A codificacao foi obtida empiricamente do pioasm 2.3.1 e esta'
 * verificada contra ele em test_timing.c, caso 40.
 */
#ifndef PIO_VM_H
#define PIO_VM_H

#include <stdint.h>

#define PIO_VM_PINS 32
#define PIO_VM_STACK 32

/*
 * Campo de 3 bits do destino em `in` e da fonte em `out`. Confirmado
 * contra o pioasm:
 *
 *   out pins, 16 = 0x6010  -> 0
 *   out x, 8     = 0x6028  -> 1
 *   out y, 1     = 0x6041  -> 2
 *   out null, 16 = 0x6070  -> 3
 *   out exec, 1  = 0x60e1  -> 7
 *
 * Repara-se que em `in` o valor 0 (`in pins, N`) significa "deslocar
 * os pinos de entrada para o ISR" - o assembler chama-lhe `pins` mas
 * o destino real e' o ISR.
 */
enum { PIO_F_PINS = 0, PIO_F_X = 1, PIO_F_Y = 2, PIO_F_NULL = 3, PIO_F_EXEC = 7 };

/* Campo de destino de `set` (0..4) e de `mov` (0..7). */
enum { PIO_S_PINS = 0, PIO_S_X = 1, PIO_S_Y = 2, PIO_S_PINDIRS = 4,
       PIO_S_ISR = 6, PIO_S_OSR = 7 };

enum { PIO_WAIT_GPIO = 0, PIO_WAIT_PIN = 1, PIO_WAIT_IRQ = 2 };

typedef struct {
    uint32_t cycle;                 /* ciclo actual                       */
    uint32_t pins;                  /* pinos de entrada, tal como o host */
    uint32_t side_set;              /* valor em side_set                  */
    uint8_t  side_active;           /* side_set activo?                   */

    uint32_t x, y;
    uint32_t osr, isr;
    uint32_t side;                  /* registo de side_set                */

    uint32_t fifo[PIO_VM_STACK];
    uint8_t  fifo_head, fifo_tail;

    /* Programa carregado. Sem `.wrap` o hardware repete por wrap_target. */
    uint16_t prog[PIO_VM_STACK];
    uint16_t len;

    /* Contador de programa: retoma de onde ficou. */
    uint32_t pc;

    /* Bloqueio: onde o programa parou a espera de uma borda. */
    int      blocked;
    uint32_t blocked_pc;

    /* Instrumentacao */
    uint32_t last_wait_fall;        /* ciclo da ultima borda de descida   */
    uint32_t last_wait_rise;        /* ciclo da ultima borda de subida    */
    uint32_t out_cycle;             /* ciclo do ultimo `out pins, N`      */
    uint32_t in_cycle;              /* ciclo do ultimo `in pins, N`       */
    uint32_t out_words;             /* palavras escritas no bus           */
    uint32_t in_words;              /* palavras amostradas               */
    uint32_t pushed;                /* eventos emitidos pela PIO           */
    uint32_t irqs;                  /* IRQ assertadas                     */
} pio_vm_t;

/* Instrucoes expostas para o teste. */
#define PIO_OPCODE(v)  ((v) & 0xe000u)

/* Decodifica uma instrução de 16 bits. Devolve 1 se reconhecido, 0 se
 * o opcode nao for suportado por este interpretador. */
int pio_vm_decode(uint16_t v, const char **name);

/* Carrega um programa (16 bits por instrucao) e repete em `pc`. */
void pio_vm_load(pio_vm_t *vm, const uint16_t *prog, size_t len, uint16_t wrap);

/* Executa ate `pc` chegar a `stop_pc` ou `max_cycles` esgotar.
 * Devolve o numero de ciclos executados. */
uint32_t pio_vm_run(pio_vm_t *vm, uint16_t stop_pc, uint32_t max_cycles);

/* Alimenta uma transicao de um pino de entrada, durante `cycles`, e
 * deixa o programa correr. Usado para simular o strobe do host. */
void pio_vm_drive(pio_vm_t *vm, unsigned pin, int level, uint32_t cycles,
                  uint16_t stop_pc, uint32_t *elapsed);

/* Quantos eventos ha por ler da FIFO simulada, e o proximo. */
uint32_t pio_vm_fifo_avail(const pio_vm_t *vm);
uint32_t pio_vm_fifo_pop(pio_vm_t *vm);

/* Reinicia o contador de programa para 0. */
void pio_vm_reset_pc(pio_vm_t *vm);

#endif /* PIO_VM_H */
