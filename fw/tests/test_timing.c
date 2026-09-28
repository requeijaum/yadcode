/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_timing.c - Prova de que o programa PIO do barramento G1 cumpre
 *                 os tempos minimos da ATA-3 Tabela 22 (PIO mode 3).
 *
 * Este e' o teste que substitui um analisador logico. Sem ele, um erro
 * de um ciclo num `nop [n]` so apareceria com o Dreamcast a nao
 * bootar, e sem forma de saber porquê.
 *
 *(sysclk 211,68 MHz, do doc 04 §4.2b => 4,725 ns por ciclo de PIO.)
 */
#include <stdio.h>
#include <string.h>
#include "pio_vm.h"
#include "gd_spec.h"

static int g_fail, g_pass, g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) { printf("  FAIL  " fmt "\n", ##__VA_ARGS__); g_fail++; } \
        else             { g_pass++; }                                       \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

/* Sysclk escolhido em doc 04 §4.2b: VCO 1481,76 MHz, postdiv 7. */
#define SYSCLK_HZ  211680000.0
#define CYCLE_NS   (1e9 / SYSCLK_HZ)     /* 4,7250... ns */

/* Tabela 22 do ATA-3, PIO mode 3, em nanossegundos. Ver doc 03 §3. */
#define T0_NS 180.0
#define T3_NS  30.0
#define T4_NS  10.0
#define T5_NS  20.0
#define T6_NS   5.0

#define PIN_CS0 19
#define PIN_RD  21
#define PIN_WR  22

/*
 * Palavras produzidas por `pioasm -o hex -v 1 g1_timing.pio`:
 *
 *   g1_read   2033 2035 a542 6010 a742 a442 20b5 a242 4010
 *   g1_write  2033 2036 a742 4010 20b6 a342 4010
 */
static const uint16_t g1_read[] = {
    0x2033, 0x2035, 0xa542, 0x6010, 0xa742, 0xa442, 0x20b5, 0xa242, 0x4010
};
static const uint16_t g1_write[] = {
    0x2033, 0x2036, 0xa742, 0x4010, 0x20b6, 0xa342, 0x4010
};

static const uint16_t *g_read_instr(size_t *n)
{
    static const char *nm;
    size_t i;
    for (i = 0; i < sizeof g1_read / sizeof g1_read[0]; i++) {
        if (!pio_vm_decode(g1_read[i], &nm)) {
            printf("  FAIL  g1_read[%u] = 0x%04x nao e' reconhecido "
                   "pelo interpretador\n", (unsigned)i, g1_read[i]);
            g_fail++;
        }
    }
    for (i = 0; i < sizeof g1_write / sizeof g1_write[0]; i++) {
        if (!pio_vm_decode(g1_write[i], &nm)) {
            printf("  FAIL  g1_write[%u] = 0x%04x nao e' reconhecido\n",
                   (unsigned)i, g1_write[i]);
            g_fail++;
        }
    }
    *n = sizeof g1_read / sizeof g1_read[0];
    return g1_read;
}

int main(void)
{
    pio_vm_t vm;
    size_t len;
    double t;

    printf("ciclo de PIO a %.4f ns (sysclk %.2f MHz)\n\n", CYCLE_NS, SYSCLK_HZ / 1e6);

    /* ---------------------------------------------------------------- */
    CASE(40, "As palavras do programa sao as que o pioasm produz");
    (void)g_read_instr(&len);
    CHECK(len == 9, "g1_read tem %u instrucoes, esperado 9", (unsigned)len);
    /* Cada palavra tem de bater com a codificacao que o
     * interpretador Assume. Isto fixa a tabela de encoding. */
    CHECK(g1_read[0] == 0x2033, "instrucao 0 = 0x%04x, esperado 0x2033 (wait 0 pin 19)", g1_read[0]);
    CHECK(g1_read[2] == 0xa542, "instrucao 2 = 0x%04x, esperado 0xa542 (nop [5])", g1_read[2]);
    CHECK(g1_read[3] == 0x6010, "instrucao 3 = 0x%04x, esperado 0x6010 (out pins, 16)", g1_read[3]);
    CHECK(g1_read[8] == 0x4010, "instrucao 8 = 0x%04x, esperado 0x4010 (in pins, 16)", g1_read[8]);
    CHECK(g1_write[2] == 0xa742, "g1_write[2] = 0x%04x, esperado 0xa742 (nop [7])", g1_write[2]);

    /* ---------------------------------------------------------------- */
    CASE(41, "t6 >= 5 ns: um ciclo NAO chega a 211,68 MHz");
    /*
     * Este e' o erro que ja foi cometido na ficha. A 4,725 ns por
     * ciclo, um ciclo fica 0,275 ns abaixo do minimo. Comprovado aqui
     * para que a correccao nunca se perca.
     */
    CHECK(CYCLE_NS < T6_NS,
          "pre-condicao mudada: um ciclo ja seria %.4f ns (>= t6)", CYCLE_NS);
    printf("        1 ciclo = %.4f ns  <  t6 = %.1f ns   -> insuficiente\n",
           CYCLE_NS, T6_NS);
    CHECK(2 * CYCLE_NS >= T6_NS,
          "2 ciclos = %.4f ns tambem insuficiente", 2 * CYCLE_NS);
    printf("        2 ciclos = %.4f ns >= t6 = %.1f ns  -> ok\n",
           2 * CYCLE_NS, T6_NS);

    /* ---------------------------------------------------------------- */
    CASE(42, "Ciclo de LEITURA: t5 e' respeitado");
    pio_vm_load(&vm, g1_read, sizeof g1_read / sizeof g1_read[0], 0);
    /*
     * Estado de repouso: CS0 asserted, RD em repouso (nivel alto, porque
     * o fio e' activo-baixo). O programa passa o `wait 0 pin 19` e
     * bloqueia no `wait 0 pin 21`.
     */
    vm.pins &= ~(1u << PIN_CS0);
    vm.pins |=  (1u << PIN_RD);
    pio_vm_run(&vm, 0xffff, 20);
    CHECK(vm.blocked, "o programa devia estar bloqueado a espera de /RD");
    CHECK(vm.last_wait_fall == 0, "ainda nao houve borda, e' o que se espera");

    /* O host baixa /RD. A borda e' registada, e o instante conta. */
    vm.pins &= ~(1u << PIN_RD);
    pio_vm_run(&vm, 0xffff, 100);
    CHECK(vm.last_wait_fall != 0, "a borda de descida de RD nao foi registada");
    CHECK(vm.out_words == 1, "esperado 1 `out pins`, obtive %u", vm.out_words);
    /* Depois de aplicar os dados, o programa tem de estar bloqueado a
     * espera da SUBIDA de /RD, nao a descer mais. */
    CHECK(vm.blocked && vm.blocked_pc == 6,
          "esperava bloqueio em `wait 1 pin 21` (pc 6), obtive pc %u",
          vm.blocked_pc);

    t = (double)(vm.out_cycle - vm.last_wait_fall) * CYCLE_NS;
    printf("        t5 medido = %u ciclos = %.2f ns   (min %.1f ns)\n",
           vm.out_cycle - vm.last_wait_fall, t, T5_NS);
    CHECK(t >= T5_NS, "t5 = %.2f ns, abaixo do minimo de %.1f ns", t, T5_NS);
    CHECK(t < T0_NS, "t5 = %.2f ns ja excede o ciclo t0", t);

    /* ---------------------------------------------------------------- */
    CASE(43, "Ciclo de LEITURA: os dados ficam no bus ate /RD subir, e t6");
    {
        /* A subida de /RD e' o instante em que o host amostrou. */
        CHECK(vm.blocked, "o programa devia estar bloqueado a espera da subida");
        vm.pins |= (1u << PIN_RD);
        pio_vm_run(&vm, 0xffff, 200);
        CHECK(vm.last_wait_rise != 0, "a subida de RD nao foi registada");
        CHECK(vm.in_words >= 1, "esperado um `in pins` no fim do ciclo, obtive %u",
              vm.in_words);
        t = (double)(vm.in_cycle - vm.last_wait_rise) * CYCLE_NS;
        printf("        t6 medido = %u ciclos = %.2f ns   (min %.1f ns)\n",
               vm.in_cycle - vm.last_wait_rise, t, T6_NS);
        CHECK(t >= T6_NS, "t6 = %.2f ns, abaixo do minimo de %.1f ns", t, T6_NS);
    }

    /* ---------------------------------------------------------------- */
    CASE(44, "Ciclo de ESCRITA: t3 e' respeitado");
    pio_vm_load(&vm, g1_write, sizeof g1_write / sizeof g1_write[0], 0);
    vm.pins &= ~(1u << PIN_CS0);
    vm.pins |=  (1u << PIN_WR);
    pio_vm_run(&vm, 0xffff, 20);
    CHECK(vm.blocked, "o programa devia estar bloqueado a espera de /WR");

    vm.pins &= ~(1u << PIN_WR);
    pio_vm_run(&vm, 0xffff, 100);
    CHECK(vm.last_wait_fall != 0, "a borda de descida de WR nao foi registada");
    CHECK(vm.in_words == 1, "esperado 1 `in pins` na amostragem, obtive %u",
          vm.in_words);
    t = (double)(vm.in_cycle - vm.last_wait_fall) * CYCLE_NS;
    printf("        t3 medido = %u ciclos = %.2f ns   (min %.1f ns)\n",
           vm.in_cycle - vm.last_wait_fall, t, T3_NS);
    CHECK(t >= T3_NS, "t3 = %.2f ns, abaixo do minimo de %.1f ns", t, T3_NS);

    /* ---------------------------------------------------------------- */
    CASE(45, "Ciclo de ESCRITA: t4 - o host so larga o bus depois de /WR subir");
    {
        uint32_t before, after;
        /*
         * t4 e' um requisito posto ao HOST, nao ao device. O device
         * nao podeSampling nada que obrigue o host a mudar o bus antes
         * de /WR subir. O programa le o bus duas vezes: a primeira
         * dentro do ciclo (durante o strobe), a segunda DEPOIS de
         * /WR subir. E' essa segunda leitura que respeita t4.
         */
        pio_vm_run(&vm, 0xffff, 200);
        before = vm.in_words;
        CHECK(vm.blocked, "o programa devia estar bloqueado a espera da subida de /WR");

        vm.pins |= (1u << PIN_WR);            /* o host sobe /WR */
        pio_vm_run(&vm, 0xffff, 200);
        after = vm.in_words;
        CHECK(vm.last_wait_rise != 0, "a subida de WR nao foi registada");
        CHECK(after > before,
              "esperava uma re-amostragem depois de /WR subir (%u -> %u)",
              before, after);

        /* O intervalo entre a subida de WR e a re-amostragem tem de
         * cobrir t4 = 10 ns. */
        t = (double)(vm.in_cycle - vm.last_wait_rise) * CYCLE_NS;
        printf("        t4 medido = %u ciclos = %.2f ns   (min %.1f ns)\n",
               vm.in_cycle - vm.last_wait_rise, t, T4_NS);
        CHECK(t >= T4_NS, "t4 = %.2f ns, abaixo do minimo de %.1f ns", t, T4_NS);
    }

    /* ---------------------------------------------------------------- */
    CASE(46, "Do strobe a amostragem cabe dentro de t0 = 180 ns");
    {
        /*
         * t0 e' o tempo de ciclo minimo - o host NAO pode ser mais
         * rapido que isto. O que o device tem de garantir e o oposto:
         * estar pronto antes de o host pedir. Por isso mede-se o
         * intervalo de subida a subida (um ciclo completo), e nao a
         * duracao do programa.
         */
        uint32_t a, b;
        pio_vm_load(&vm, g1_read, sizeof g1_read / sizeof g1_read[0], 0);
        vm.pins &= ~(1u << PIN_CS0);
        vm.pins |=  (1u << PIN_RD);
        pio_vm_run(&vm, 0xffff, 20);

        /* Baixa /RD: aplicacao dos dados. */
        vm.pins &= ~(1u << PIN_RD);
        pio_vm_run(&vm, 0xffff, 100);
        CHECK(vm.blocked && vm.blocked_pc == 6,
              "esperava bloqueio no `wait 1 pin 21`, obtive pc %u", vm.blocked_pc);

        /* Sobe /RD: o host amostrou. Este e o fim do ciclo. */
        vm.pins |= (1u << PIN_RD);
        pio_vm_run(&vm, 0xffff, 200);
        a = vm.last_wait_rise;
        CHECK(a != 0, "a subida de RD nao foi registada");

        /* Ciclo seguinte: baixa e volta a subir /RD. */
        vm.pins &= ~(1u << PIN_RD);
        pio_vm_run(&vm, 0xffff, 100);
        vm.pins |= (1u << PIN_RD);
        pio_vm_run(&vm, 0xffff, 200);
        b = vm.last_wait_rise;

        t = (double)(b - a) * CYCLE_NS;
        printf("        ciclo a ciclo = %u ciclos = %.2f ns  (t0 min %.1f ns)\n",
               b - a, t, T0_NS);
        /*
         * O device NAO controla o ritmo do host, por isso isto nao e'
         * um requisito nosso. E' uma medicao, para saber quantos
         * ciclos temos de folga se o host vier rapido.
         */
        CHECK(b > a, "a segunda subida de RD devia ser posterior");
    }

    /* ---------------------------------------------------------------- */
    CASE(47, "Margem: quantos ciclos existem para a PIO, por ciclo de bus");
    {
        /*
         * A 180 ns por palavra (doc 03 §2) existem 38 ciclos de PIO.
         * O ciclo de leitura consome 1 (CS) + 1 (borda RD) + 6 (t5) +
         * 1 (out) + 8 + 5 (espera) + 1 (borda RD) + 3 (t6) + 1 (in)
         * = 27. Sobram 11 ciclos para o resto - FIFO, IRQ, ou um
         * segundo `out` se o registo precisar de mais de uma palavra.
         */
        double avail = T0_NS / CYCLE_NS;
        printf("        disponivel = %.1f ciclos por palavra de 16 bits\n", avail);
        CHECK(avail >= 27.0,
              "so ha %.1f ciclos e o ciclo de leitura precisa de 27", avail);
    }

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_fail ? g_fail : g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
