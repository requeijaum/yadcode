/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "hostsim.h"

static const char *regname(uint8_t r)
{
    switch (r) {
    case GD_R_ERROR:      return "ERROR ";
    case GD_R_FEATURES:   return "FEAT  ";
    case GD_R_INTREASON:  return "INTRE ";
    case GD_R_SECTORNUM:  return "SECNR ";
    case GD_R_BYTECOUNTL: return "BCNTL ";
    case GD_R_BYTECOUNTH: return "BCNTH ";
    case GD_R_DRIVESEL:   return "DRSEL ";
    case GD_R_STATUS:     return "STATUS";
    default:              return "?????";
    }
}

static void ev(hostsim_t *hs, uint8_t op, uint8_t reg, uint16_t value)
{
    hostsim_ev_t *e;
    if (hs->ntrace >= HOSTSIM_MAX_TRACE) return;
    e = &hs->trace[hs->ntrace++];
    e->op        = op;
    e->reg       = reg;
    e->value     = value;
    e->bytecount = (uint16_t)(hs->dev.reg[GD_R_BYTECOUNTL] |
                              (hs->dev.reg[GD_R_BYTECOUNTH] << 8));
    e->micros    = hs->now_us;
    /* Um acesso de registo no G1 leva alguns microsegundos. */
    hs->now_us += 2;
}

/* ------------------------------------------------------------------ */
/* Callbacks do HAL                                                    */
/* ------------------------------------------------------------------ */

static uint32_t h_micros(gd_device_t *dev)
{
    return ((hostsim_t *)dev)->now_us;   /* dev e' o primeiro membro */
}

static void h_write_control(gd_device_t *dev, uint8_t reg, uint8_t value)
{
    gd_taskfile_write_reg(dev, reg, value);
}

static uint8_t h_read_control(gd_device_t *dev, uint8_t reg)
{
    return gd_taskfile_read_reg(dev, reg);
}

static void h_write_data(gd_device_t *dev, uint16_t w)
{
    gd_taskfile_put_data(dev, w);
}

static uint16_t h_read_data(gd_device_t *dev)
{
    return gd_taskfile_take_data(dev);
}

static void h_write_command(gd_device_t *dev, uint8_t cmd)
{
    gd_taskfile_command(dev, cmd);
}

static void h_assert_irq(gd_device_t *dev, int on)
{
    hostsim_t *hs = (hostsim_t *)dev;   /* dev e' o primeiro membro */
    if (on) { hs->irq_seen = 1; hs->irq_asserts++; }
    (void)dev;
}

static void h_start_data_in(gd_device_t *dev, uint16_t bytecount)
{
    (void)dev; (void)bytecount;
}

static void h_end_data_in(gd_device_t *dev) { (void)dev; }

static void h_memzero(void *dst, size_t n) { memset(dst, 0, n); }

static const gd_hal_t hal = {
    .micros        = h_micros,
    .write_control = h_write_control,
    .write_data    = h_write_data,
    .write_command = h_write_command,
    .read_control  = h_read_control,
    .read_data     = h_read_data,
    .assert_irq    = h_assert_irq,
    .start_data_in = h_start_data_in,
    .read_data_block = NULL,   /* o simulador chama gd_taskfile_read_data */
    .end_data_in   = h_end_data_in,
    .memzero       = h_memzero,
};

/* ------------------------------------------------------------------ */

void hostsim_init(hostsim_t *hs, gd_disc_t *disc)
{
    memset(hs, 0, sizeof *hs);
    hs->dev.hal = &hal;
    hs->dev.disc = disc;
    hs->hal = &hal;
    gd_taskfile_reset(&hs->dev);
}

uint8_t hostsim_write_control(hostsim_t *hs, uint8_t reg, uint8_t value)
{
    ev(hs, 'W', reg, value);
    h_write_control(&hs->dev, reg, value);
    return hs->dev.reg[reg];
}

uint8_t hostsim_read_control(hostsim_t *hs, uint8_t reg)
{
    uint8_t v = h_read_control(&hs->dev, reg);
    ev(hs, 'R', reg, v);
    return v;
}

uint8_t hostsim_read_altstatus(hostsim_t *hs)
{
    uint8_t v = gd_taskfile_read_altstatus(&hs->dev);
    ev(hs, 'A', GD_R_STATUS, v);
    return v;
}

void hostsim_write_data(hostsim_t *hs, uint16_t w)
{
    ev(hs, 'w', 0, w);
    h_write_data(&hs->dev, w);
}

uint16_t hostsim_read_data(hostsim_t *hs)
{
    uint16_t w = h_read_data(&hs->dev);
    ev(hs, 'r', 0, w);
    return w;
}

void hostsim_write_command(hostsim_t *hs, uint8_t cmd)
{
    ev(hs, 'C', GD_R_STATUS, cmd);
    h_write_command(&hs->dev, cmd);
}

uint32_t hostsim_spi_command(hostsim_t *hs, const uint8_t pkt[GD_PKT_SIZE],
                             uint8_t *out, uint32_t outcap)
{
    uint32_t bc, got = 0;
    int i;

    /* Seca 7.1 passo 1: esperar BSY=0 e DRQ=0. */
    while (hostsim_read_control(hs, GD_R_STATUS) & (GD_ST_BSY | GD_ST_DRQ)) {
        if (hs->ntrace >= HOSTSIM_MAX_TRACE) return 0;
    }

    /* Passo 2: Features / Byte Count / Drive Select ja' foram escritos
     * pelo host. Passo 3: escrever 0xA0. */
    hostsim_write_command(hs, GD_CMD_PACKET);

    /* Passo 4: o device tem de sinalizar CoD=1, IO=0, DRQ=1. */
    {
        uint8_t st = hostsim_read_control(hs, GD_R_STATUS);
        uint8_t ir = hostsim_read_control(hs, GD_R_INTREASON);
        if (!(st & GD_ST_DRQ) || (ir & GD_IR_COD) == 0) {
            fprintf(stderr, "hostsim: device nao aceitou o packet "
                            "(status=0x%02x intreason=0x%02x)\n", st, ir);
            return 0;
        }
    }

    hs->n_packets++;

    /* Passo 5: 6 palavras de 16 bits. */
    for (i = 0; i < GD_PKT_WORDS; i++) {
        uint16_t w = (uint16_t)(((uint16_t)pkt[i * 2] << 8) | pkt[i * 2 + 1]);
        hostsim_write_data(hs, w);
    }

    /* Passo 7: Byte Count diz o tamanho. */
    bc = (uint32_t)hs->dev.reg[GD_R_BYTECOUNTL] |
         ((uint32_t)hs->dev.reg[GD_R_BYTECOUNTH] << 8);

    if (bc) {
        /*
         * O registo Data entrega palavras de 16 bits, big-endian dentro
         * da palavra (spec seca 3.4). Ao reconstituir um fluxo de bytes
         * tem de se serializar big-endian, senao cada par de bytes sai
         * trocado. Foi o primeiro bug que os testes apanharam.
         */
        uint16_t words[16];
        uint32_t off = 0;
        while (off < bc) {
            uint32_t n = (bc - off) / 2;
            uint32_t k, got_n;
            if (n > 16) n = 16;
            got_n = gd_taskfile_read_data(&hs->dev, words, n);
            if (got_n == 0) break;
            for (k = 0; k < got_n; k++) {
                uint32_t o = off + k * 2;
                if (out && o + 1 < outcap) {
                    out[o + 0] = (uint8_t)(words[k] >> 8);
                    out[o + 1] = (uint8_t)(words[k] & 0xff);
                }
            }
            off += got_n * 2;
        }
        got = off;
    }

    /* Passo 8/9: ler Status fecha a transacao. */
    (void)hostsim_read_control(hs, GD_R_STATUS);
    return got;
}

void hostsim_dump(const hostsim_t *hs)
{
    int i;
    for (i = 0; i < hs->ntrace; i++) {
        const hostsim_ev_t *e = &hs->trace[i];
        switch (e->op) {
        case 'C':
            printf("%6u  CMD   0x%02x  bc=%u\n", e->micros, e->value, e->bytecount);
            break;
        case 'W':
            printf("%6u  W %s 0x%02x  bc=%u\n", e->micros, regname(e->reg),
                   e->value, e->bytecount);
            break;
        case 'A':
            printf("%6u  R ALTST 0x%02x  bc=%u  (nao limpa IRQ)\n",
                   e->micros, e->value, e->bytecount);
            break;
        case 'R':
            printf("%6u  R %s 0x%02x  bc=%u\n", e->micros, regname(e->reg),
                   e->value, e->bytecount);
            break;
        case 'w':
            printf("%6u  W DATA 0x%04x\n", e->micros, e->value);
            break;
        default:
            printf("%6u  R DATA 0x%04x\n", e->micros, e->value);
            break;
        }
    }
}
