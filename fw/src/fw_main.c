/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fw_main.c - Ponto de entrada do firmware para RP2350B.
 *
 * Existe nesta fase para TRES coisas, e so para isso:
 *
 *  1. Provar que a cadeia de compilacao fecha: pico-sdk + pioasm +
 *     arm-none-eabi-gcc + picotool. Sem isto nao ha forma de saber se
 *     o resto do firmware sequer vai compilar.
 *
 *  2. Carregar o programa PIO de temporizacao e deixá-lo correr contra
 *     pinos de teste, com um verificador de tempo em C. Isto e' o
 *     "PIO de loopback" do doc 11 §5: valida t0/t3/t5/t6/t9 em silicio
 *     real, e nao no interpretador.
 *
 *  3. Fixar o sysclk em 211,68 MHz (doc 04 §4.2b), de onde sai o ciclo
 *     de PIO de 4,7241 ns que o teste de timing assume.
 *
 * O nucleo portavel do emulador (gd_taskfile/gd_spi/gd_cdda) ainda nao
 * esta ligado: falta L1 completo e a placa. O ponto de partida e'
 * src/gd_l1.* (esqueleto comentado: decode + despacho + PIO slow-path),
 * ainda nao chamado a partir daqui.
 */
#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "sniffer.h"

/* Sniffer: definido mais abaixo, neste ficheiro. */
static int  sniff_start(void);
static void sniff_poll(void);

#include "g1_timing.pio.h"
#include "g1_sniff.pio.h"

/* O SDK 2.x tirou BIT() deHeaders publicos. */
#define BITMASK(n) (1u << (n))

/*
 * Doc 04 §4.2b:
 *   refdiv = 25, fbdiv = 3087, postdiv1 = 7, postdiv2 = 1
 *   VCO = 12/25 * 3087 = 1481,76 MHz  (dentro de 750..1600)
 *   sysclk = 211,68 MHz
 *   PIO clkdiv 6,25 -> 211,68/6,25 = 33,8688 MHz  (o clock do AICA)
 */
#define SYSCLK_HZ      211680000
#define PIO_CLK_HZ     33868800

/* Pinos do sniffer. Tem de bater com sniffer.h (SN_DD/SN_DA/...):
 * o `mov x, pins` da PIO amostra os 32 GPIO de uma vez, e pinos de
 * entrada por inicializar flutuam. Por isso inicializam-se todos os
 * 28 do sniffer como entradas, nao so' os do reader. */
#define PIN_CS0 19
#define PIN_RD  21
#define PIN_WR  22
#define PIN_DD  0        /* DD0..DD15 = GPIO 0..15 */

/* Tabela 22, ATA-3, PIO mode 3. Ver doc 03 §3. */
#define T3_NS 30.0
#define T4_NS 10.0
#define T5_NS 20.0
#define T6_NS  5.0

static bool set_sysclk(void)
{
    return set_sys_clock_khz(SYSCLK_HZ / 1000, true);
}

static void banner(void)
{
    const float mhz = (float)clock_get_hz(clk_sys) / 1e6f;
    const float pio = (float)clock_get_hz(clk_peri) / 1e6f;
    const float cyc_ns = 1e9f / pio;

    printf("\n");
    printf("  GD-ROM ODE - RP2350B\n");
    printf("  ------------------------------------------------------------\n");
    printf("  sysclk        %10.3f MHz   (alvo 211,680)\n", (double)mhz);
    printf("  clk_peri       %10.3f MHz\n", (double)pio);
    printf("  ciclo de PIO   %10.4f ns\n", (double)cyc_ns);
    printf("  clock do AICA  %10.4f MHz   (alvo 33,8688)\n", (double)PIO_CLK_HZ / 1e6f);
    printf("\n");

    /* As previsoes que o teste de timingOffline fixou. Se o sysclk
     * divergir, os atrasos do PIO ja nao batem e a board nao boota. */
    printf("  t5 com 6 ciclos = %6.2f ns   (min %.1f)  %s\n",
           (double)(cyc_ns * 6.0f), T5_NS, (cyc_ns * 6.0f >= T5_NS) ? "ok" : "FALHA");
    printf("  t3 com 8 ciclos = %6.2f ns   (min %.1f)  %s\n",
           (double)(cyc_ns * 8.0f), T3_NS, (cyc_ns * 8.0f >= T3_NS) ? "ok" : "FALHA");
    printf("  t6 com 3 ciclos = %6.2f ns   (min %.1f)  %s\n",
           (double)(cyc_ns * 3.0f), T6_NS, (cyc_ns * 3.0f >= T6_NS) ? "ok" : "FALHA");
    printf("  t6 com 1 ciclo  = %6.2f ns   (min %.1f)  %s\n",
           (double)(cyc_ns * 1.0f), T6_NS, (cyc_ns * 1.0f >= T6_NS) ? "ok" : "FALHA");
    printf("\n");
    printf("  A ultima linha deve dizer FALHA: a 211,68 MHz um ciclo sao\n"
           "  menos de 5 ns e por isso t6 precisa de 3 ciclos, nao de 1.\n\n");
}

int main(void)
{
    PIO pio = pio0;
    uint sm;

    stdio_init_all();

    if (!set_sysclk()) {
        printf("ERRO: nao foi possivel fixar o sysclk em %d Hz\n", SYSCLK_HZ);
        return 1;
    }
    banner();

    /* O sniffer amostra GPIO 0..27: todos como entradas. Um pino
     * flutuante aqui e' um evento fantasma na FIFO. */
    gpio_init_mask(0x0fffffffu);
    gpio_set_dir_in_masked(0x0fffffffu);

    /* PIO a divisor 1: o ciclo e' 1/clk_peri. A configuracao por
     * omissao do SDK ja' traz clkdiv 1, por isso nao se mexe aqui;
     * cada SM usa a sua (ver sniff_start). */

    /* API do SDK 2.x: o programa e' um struct `pio_program_t` e carrega-se
     * com pio_add_program(), ao contrario das versaoes antigas que geravam
     * <prog>_program_init(). O `get_default_config` recebe o offset onde
     * o programa ficou, nao o numero da SM: passar 0 a olho so' funciona
     * enquanto a PIO estiver vazia. */
    pio_sm_config c;
    uint off;
    off = pio_add_program(pio, &g1_read_program);
    c = g1_read_program_get_default_config(off);
    sm = pio_claim_unused_sm(pio, true);
    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);

    printf("  PIO 'g1_read' carregado na SM%d, a correr.\n", sm);
    printf("  Sem loopback automatico nesta fase: falta um gerador de host\n");
    printf("  em PIO. Ver fw/tests/test_timing.c para a prova de timing\n");
    printf("  em simulacao, que e' o que substitui o scope por agora.\n\n");

    if (sniff_start()) {
        printf("\n  Sniffer armado. Ctrl-C ou reset para parar.\n\n");
        while (1) {
            sleep_ms(200);
            sniff_poll();
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sniffer                                                             */
/* ------------------------------------------------------------------ */

static sn_capture_t g_cap;      /* ring, com indices em curso          */
static volatile int g_fifo_hit; /* posto pelo handler da IRQ           */
static int g_sm = -1;
static int g_irq = -1;
static uint32_t g_reported_dropped;

static uint32_t g_out[SN_BATCH];

/*
 * A IRQ da PIO e' o unico sinal de que a FIFO pode ter dados. O
 * handler e' o mais curto possivel: marca que ha trabalho e sai. Todo o
 * trabalho caro (ler a FIFO, formatar texto, escrever na UART) fica no
 * contexto normal, em sniff_poll.
 *
 * Limpar o flag da PIO aqui e' obrigatorio: sem o
 * `pio_interrupt_clear`, a saida IRQ0 da PIO continuava asserted e o
 * handler reentrava em ciclo. Nao se le a FIFO aqui: a FIFO tem 8
 * palavras, e' pequena demais para trabalho serio dentro de uma IRQ,
 * e um handler lento atrasa o IRQ seguinte.
 */
static void sniff_irq(void)
{
    pio_interrupt_clear(pio0, 0);
    g_fifo_hit = 1;
}

static int sniff_start(void)
{
    pio_sm_config c;
    uint off;

    sn_init(&g_cap);
    g_reported_dropped = 0;

    /* A SM do sniffer e' separada da do reader. Cada PIO tem 4 e o
     * main so' reclamou uma para o reader. */
    g_sm = pio_claim_unused_sm(pio0, true);
    if (g_sm < 0) return 0;

    off = pio_add_program(pio0, &g1_trace_program);
    c = g1_trace_program_get_default_config(off);
    pio_sm_init(pio0, (uint)g_sm, off, &c);
    pio_sm_set_enabled(pio0, (uint)g_sm, true);

    /*
     * Sem isto a IRQ nunca dispara: o `irq 0` do programa poe o flag
     * 0 da PIO, mas a saida IRQ0 para o NVIC so' liga com a fonte
     * habilitada. `pio_get_irq_num` devolve o numero de IRQ global
     * que corresponde a (pio, 0) nesta variante do SDK; passar 0 a
     * olho seria fragil.
     */
    pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
    g_irq = pio_get_irq_num(pio0, 0);
    irq_set_exclusive_handler(g_irq, sniff_irq);
    irq_set_enabled(g_irq, true);

    printf("  Sniffer: PIO 'g1_trace' na SM%d, IRQ %d, ring de %u palavras.\n",
           g_sm, g_irq, (unsigned)SN_RING_WORDS);
    printf("  Captura a ir para a UART. Para a analisar:\n");
    printf("    build/gdsniff <captura.txt>\n");
    return 1;
}

static void sniff_poll(void)
{
    uint32_t n, i;
    uint32_t save;

    /*
     * Esvazia a FIFO para o ring. As interrupcoes ficam desligadas
     * durante a leitura: `g_fifo_hit` e' um flag, nao uma contagem, e
     * perder um IRQ durante a leitura nao perde dados, so' atrasa o
     * proximo poll. Nao ha gancho em contar cada IRQ.
     */
    if (g_fifo_hit) {
        save = save_and_disable_interrupts();
        n = 0;
        while (!pio_sm_is_rx_fifo_empty(pio0, (uint)g_sm) && n < SN_BATCH)
            g_out[n++] = pio_sm_get(pio0, (uint)g_sm);
        g_fifo_hit = 0;
        restore_interrupts(save);

        for (i = 0; i < n; i++)
            sn_push(&g_cap, g_out[i]);
    }

    /* Avisa de perdas uma vez por patamar, nao uma vez por poll: sem
     * isto a UART entupia com o aviso em vez da captura. */
    if (g_cap.dropped != g_reported_dropped) {
        g_reported_dropped = g_cap.dropped;
        printf("  AVISO: %u evento(s) perdido(s) - a UART nao acompanha\n",
               (unsigned)g_cap.dropped);
    }

    /* Despeja o que houver no ring, haja ou nao IRQ nova: o ring pode
     * ter backlog de polls anteriores. */
    {
        uint32_t w[SN_BATCH];
        uint32_t k = sn_drain(&g_cap, w, SN_BATCH);
        /*
         * O estado dos pinos so' ocupa os 28 bits baixos. Escreve-se
         * em dois grupos de 8 hex para o `gdsniff` poder ler 16
         * digitos seguidos sem o agrupamento atrapalhar. O contador
         * permite confirmar que nao faltou nenhum evento.
         */
        for (i = 0; i < k; i++)
            printf("%08x%08x %u\n",
                   (unsigned)(w[i] >> 16), (unsigned)w[i],
                   (unsigned)(g_cap.total - k + i));
    }}
