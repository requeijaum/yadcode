/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_l1_hw.c - L1 no silicio: GPIO, PIO e IRQ do RP2350.
 *
 * ESTADO: ESQUELETO. Compila no firmware, nunca correu. So' entra no
 * build do firmware (CMakeLists); o Makefile de host nem o ve.
 *
 * REGRA DE SEGURANCA deste ficheiro: o esqueleto NUNCA conduz o bus.
 * Todos os GPIO 0..27 sao entradas. Responder electricamente a
 * leituras (conduzir DD, IORDY) e' trabalho do fast path [TODO-L1] e
 * exige medir primeiro. Um esqueleto que conduzisse o bus sem medir
 * podia lutar com o host e queimar pinos.
 */
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#include "gd_l1.h"
#include "gd_taskfile.h"
#include "sniffer.h"   /* os pinos do bus: o mesmo G1 do sniffer */
#include "g1_dev.pio.h"

/* ------------------------------------------------------------------ */
/* Pinos do device. ATENCAO: placeholders de placa.                    */
/* ------------------------------------------------------------------ */
/* Ver gd_l1.h: os numeros vivem la' para o teste os ver. Aqui so' se
 * usam. */

static gd_device_t *g_dev;
static int g_sm = -1;

/* Contadores para bring-up: o que o analisador logico nao mostra. */
static uint32_t g_accesses, g_reads, g_writes, g_invalid;

/* Ultimo acesso descodificado, para inspeccao com debugger. */
static unsigned g_last_cs0, g_last_cs1, g_last_da;
static int g_last_is_write;
static uint16_t g_last_value;

/* ------------------------------------------------------------------ */
/* Operacoes gd_l1_hw_t                                                */
/* ------------------------------------------------------------------ */

static void hw_init_pins(void)
{
    /* Bus inteiro como entrada: o esqueleto nao conduz nada. */
    gpio_init_mask(0x0fffffffu);
    gpio_set_dir_in_masked(0x0fffffffu);

    gpio_init(GD_L1_PIN_INTRQ);
    gpio_set_dir(GD_L1_PIN_INTRQ, GPIO_OUT);
    gpio_put(GD_L1_PIN_INTRQ, 0);
}

static void hw_set_intrq(int asserted)
{
    gpio_put(GD_L1_PIN_INTRQ, asserted ? 1 : 0);
}

static void hw_set_iordy(int asserted)
{
    /* [TODO-L1] IORDY e' saida do device (baixo = wait [INFERRED,
     * ATA]). Hoje o GPIO esta' como entrada (pull-up externo = pronto)
     * e esta funcao e' um no-op de proposito: conduzir IORDY sem medir
     * primeiro e' arriscado. Ver regra de seguranca acima. */
    (void)asserted;
}

static void hw_set_dmarq(int asserted)
{
    /* [TODO-L1] So' com DMA (questao C em aberto, doc 06b). Ate' la',
     * nunca afirmar. */
    (void)asserted;
}

static uint32_t hw_micros(void)
{
    return time_us_32();
}

static const gd_l1_hw_t hw_rp2350 = {
    hw_init_pins, hw_set_intrq, hw_set_iordy, hw_set_dmarq, hw_micros
};

/* ------------------------------------------------------------------ */
/* ISR: esvazia a FIFO, descodifica, despacha escritas                 */
/* ------------------------------------------------------------------ */

static void l1_irq(void)
{
    /* Mesma licao do sniffer: limpar o flag primeiro, ler depois. */
    pio_interrupt_clear(pio0, 0);

    while (!pio_sm_is_rx_fifo_empty(pio0, (uint)g_sm)) {
        uint32_t w = pio_sm_get(pio0, (uint)g_sm);
        unsigned cs0 = (w >> SN_CS0) & 1u;
        unsigned cs1 = (w >> SN_CS1) & 1u;
        unsigned da  = (w >> SN_DA(0)) & 7u;
        unsigned rd  = (w >> SN_RD) & 1u;
        unsigned wr  = (w >> SN_WR) & 1u;
        int is_write = 0;
        gd_l1_reg_t reg;

        g_accesses++;
        g_last_cs0 = cs0; g_last_cs1 = cs1; g_last_da = da;

        reg = gd_l1_decode(cs0, cs1, da, rd, wr, &is_write);
        g_last_is_write = is_write;
        if (reg == L1R_NONE) { g_invalid++; continue; }

        if (is_write) {
            g_writes++;
            /* Escritas executam-se ja': vao diretas ao nucleo. */
            gd_l1_write(g_dev, reg, (uint16_t)(w & 0xffffu));
        } else {
            g_reads++;
            /*
             * [TODO-L1] Leituras no slow path NAO tem resposta
             * electrica: o valor calculado abaixo nao chega ao bus
             * dentro de t5. Guarda-se para inspeccao; a resposta a
             * tempo e' trabalho do fast path (DATA SM + OSR).
             */
            g_last_value = gd_l1_read(g_dev, reg);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Arranque                                                            */
/* ------------------------------------------------------------------ */

int gd_l1_hw_start(gd_device_t *dev)
{
    pio_sm_config c;
    uint off;
    int irq;

    g_dev = dev;
    gd_l1_set_hw(&hw_rp2350);
    hw_init_pins();

    g_sm = pio_claim_unused_sm(pio0, true);
    if (g_sm < 0) return 0;

    off = pio_add_program(pio0, &g1_dev_slow_program);
    c = g1_dev_slow_program_get_default_config(off);
    pio_sm_init(pio0, (uint)g_sm, off, &c);
    pio_sm_set_enabled(pio0, (uint)g_sm, true);

    pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
    irq = pio_get_irq_num(pio0, 0);
    irq_set_exclusive_handler(irq, l1_irq);
    irq_set_enabled(irq, true);
    return 1;
}

void gd_l1_hw_poll(void)
{
    /*
     * No esqueleto nao ha trabalho diferido: a ISR faz tudo o que ha
     * para fazer. Esta funcao existe para o futuro (ex.: timeouts,
     * debounce do RESET) sem mudar o main.
     * [INFERRED: /RESET devia gerar reset via GPIO IRQ ou poll aqui.
     * Hoje: nada. TODO-L1.]
     */
}
