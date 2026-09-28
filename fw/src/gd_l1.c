/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_l1.c - Camada L1: despacho device-side. Puro: compila e testa no PC.
 *
 * Quem toca silicio esta' em gd_l1_hw.c e entra por `gd_l1_hw_t`.
 * Este ficheiro so' conhece pinos como numeros e o nucleo como API.
 */
#include <string.h>
#include "gd_l1.h"
#include "gd_taskfile.h"

/* ------------------------------------------------------------------ */
/* Hardware (fakes por omissao; o firmware instala os reais)           */
/* ------------------------------------------------------------------ */

static void hw_nop(void) {}
static void hw_nop_level(int level) { (void)level; }
static uint32_t hw_zero(void) { return 0; }

static const gd_l1_hw_t hw_stubs = {
    hw_nop, hw_nop_level, hw_nop_level, hw_nop_level, hw_zero
};
static const gd_l1_hw_t *hw = &hw_stubs;

void gd_l1_set_hw(const gd_l1_hw_t *h)
{
    hw = (h) ? h : &hw_stubs;
}

/* ------------------------------------------------------------------ */
/* Descodificacao: Tabela 3.1 [DOC-01 §3.4], linha a linha              */
/* ------------------------------------------------------------------ */

gd_l1_reg_t gd_l1_decode(unsigned cs0, unsigned cs1, unsigned da,
                         unsigned rd, unsigned wr, int *is_write)
{
    unsigned w = (wr == 0);   /* strobes activo-baixo: 0 = asserted */
    unsigned r = (rd == 0);
    unsigned c0 = (cs0 == 0);
    unsigned c1 = (cs1 == 0);

    if (is_write) *is_write = 0;

    /* Nenhum strobe, ou os dois: nao e' um acesso. A ultima linha da
     * tabela (A/A) cai aqui tambem: e' invalida [DOC-01 Tabela 3.1]. */
    if ((r == 0 && w == 0) || (r && w)) return L1R_NONE;
    if (is_write) *is_write = (int)w;

    if (w) {
        /* WRITE (DIOW-). Linhas N/N e N/A-qualquer-coisa sao hi-Z. */
        if (!c0 && !c1) return L1R_NONE;
        if (!c0 && c1) {
            /* N/A/110 e' o unico endereco valido no Control Block. */
            return (da == 0x6) ? L1R_DEVCONTROL : L1R_NONE;
        }
        if (c0 && !c1) {
            switch (da) {
            case 0x0: return L1R_DATA;
            case 0x1: return L1R_FEATURES;   /* ERROR na leitura */
            case 0x2: return L1R_NONE;       /* IntReason: RO */
            case 0x3: return L1R_NONE;       /* SectorNum: RO */
            case 0x4: return L1R_BYTECOUNTL;
            case 0x5: return L1R_BYTECOUNTH;
            case 0x6: return L1R_DRIVESEL;
            case 0x7: return L1R_COMMAND;    /* STATUS na leitura */
            default:  return L1R_NONE;       /* da so' tem 3 bits */
            }
        }
        return L1R_NONE;   /* A/A: invalido */
    }

    /* READ (DIOR-). */
    if (!c0 && !c1) return L1R_NONE;
    if (!c0 && c1) {
        return (da == 0x6) ? L1R_ALTSTATUS : L1R_NONE;
    }
    if (c0 && !c1) {
        switch (da) {
        case 0x0: return L1R_DATA;
        case 0x1: return L1R_ERROR;
        case 0x2: return L1R_INTREASON;
        case 0x3: return L1R_SECTORNUM;
        case 0x4: return L1R_BYTECOUNTL;
        case 0x5: return L1R_BYTECOUNTH;
        case 0x6: return L1R_DRIVESEL;
        case 0x7: return L1R_STATUS;
        default:  return L1R_NONE;
        }
    }
    return L1R_NONE;
}

unsigned gd_l1_width(gd_l1_reg_t reg)
{
    /* [DOC-01 §2.1]: tudo em bytes excepto Data, sempre 16 bits. */
    if (reg == L1R_DATA) return 16;
    if (reg == L1R_NONE) return 0;
    return 8;
}

/* ------------------------------------------------------------------ */
/* Despacho: cada braco diz que chamada L2 faz e que efeito colateral  */
/* tem no hardware.                                                    */
/* ------------------------------------------------------------------ */

void gd_l1_write(gd_device_t *dev, gd_l1_reg_t reg, uint16_t value)
{
    uint8_t lo = (uint8_t)(value & 0xffu);

    switch (reg) {
    case L1R_DATA:
        /* 16 bits inteiros. O L2 acumula as 6 palavras do packet e
         * dispara sozinho [DOC-01 §7.1 passo 5, ver put_data]. */
        gd_taskfile_put_data(dev, value);
        break;
    case L1R_FEATURES:
        gd_taskfile_write_reg(dev, GD_R_FEATURES, lo);
        break;
    case L1R_BYTECOUNTL:
        gd_taskfile_write_reg(dev, GD_R_BYTECOUNTL, lo);
        break;
    case L1R_BYTECOUNTH:
        gd_taskfile_write_reg(dev, GD_R_BYTECOUNTH, lo);
        break;
    case L1R_DRIVESEL:
        gd_taskfile_write_reg(dev, GD_R_DRIVESEL, lo);
        break;
    case L1R_COMMAND:
        /* Pode chegar com BSY=1 (NOP, Soft Reset) [DOC-01 §3.3.1.2];
         * o nucleo decide, o L1 so' entrega. */
        gd_taskfile_command(dev, lo);
        break;
    case L1R_DEVCONTROL:
        dev->devcontrol = lo;
        /* [DOC-01 §2.3] bit nIEN=1 poe o INTRQ em hi-Z. No silicio
         * isto troca a direccao do GPIO [TODO-L1]; aqui fica o bit
         * para o L2 ler (ver taskfile.c:66). */
        break;
    case L1R_INTREASON:
    case L1R_SECTORNUM:
    case L1R_ERROR:
    case L1R_STATUS:
    case L1R_ALTSTATUS:
    case L1R_NONE:
    default:
        /* Read-only no device [DOC-01 Tabela 3.1, "nao usado"]: o L2
         * tambem ignora (ver write_reg). Chegar aqui com um destes
         * num programa rapido e' bug do decode, nao do host. */
        break;
    }
}

uint16_t gd_l1_read(gd_device_t *dev, gd_l1_reg_t reg)
{
    switch (reg) {
    case L1R_DATA:
        /* 16 bits inteiros [DOC-01 §2.1]. */
        return gd_taskfile_take_data(dev);
    case L1R_ERROR:
        return gd_taskfile_read_reg(dev, GD_R_ERROR);
    case L1R_INTREASON:
        return gd_taskfile_read_reg(dev, GD_R_INTREASON);
    case L1R_SECTORNUM:
        return gd_taskfile_read_reg(dev, GD_R_SECTORNUM);
    case L1R_BYTECOUNTL:
        return gd_taskfile_read_reg(dev, GD_R_BYTECOUNTL);
    case L1R_BYTECOUNTH:
        return gd_taskfile_read_reg(dev, GD_R_BYTECOUNTH);
    case L1R_DRIVESEL:
        return gd_taskfile_read_reg(dev, GD_R_DRIVESEL);
    case L1R_STATUS:
        /* [DOC-01 §3.4 + gd_taskfile.h:141] ler o Status LIMPA o
         * INTRQ. E' o efeito colateral mais importante do L1: sem
         * ele o host nunca sai do IRQ. No silicio, esta chamada tem
         * de desassertar o GPIO (via clear_irq -> assert_irq(0)). */
        gd_taskfile_clear_irq(dev);
        return gd_taskfile_read_reg(dev, GD_R_STATUS);
    case L1R_ALTSTATUS:
        /* [DOC-01 §3.4] AltStatus NAO limpa nada: e' para sondar sem
         * cancelar o IRQ. Jogos dependem disto [DOC-13]. */
        return gd_taskfile_read_altstatus(dev);
    case L1R_FEATURES:
    case L1R_COMMAND:
    case L1R_DEVCONTROL:
    case L1R_NONE:
    default:
        return 0;
    }
}

void gd_l1_reset(gd_device_t *dev)
{
    /* [DOC-01 §3.3.1.1] task file com os valores de reset. */
    gd_taskfile_reset(dev);
    /* Saidas do device para repouso. IORDY=pronto e' o nivel
     * inactivo (pull-up) [INFERRED: ATA, IORDY baixo = wait;
     * confirmar no scope]; INTRQ e DMARQ desassertados. */
    hw->set_iordy(1);
    hw->set_intrq(0);
    hw->set_dmarq(0);
}

/* ------------------------------------------------------------------ */
/* O gd_hal_t do firmware: cada membro e' uma linha para o despacho    */
/* acima, excepto os que tocam silicio (vão por `hw`).                 */
/* ------------------------------------------------------------------ */

static uint32_t hal_micros(gd_device_t *dev)
{
    (void)dev;
    return hw->micros();
}

static void hal_write_control(gd_device_t *dev, uint8_t reg, uint8_t value)
{
    /* Os ids GD_R_* distinguem ERROR de FEATURES; o L1R faz o mesmo
     * pelo lado do fio. A tabela tem de bater nos dois sentidos
     * (test_l1.c, caso 92). */
    switch (reg) {
    case GD_R_FEATURES:  gd_l1_write(dev, L1R_FEATURES, value);  break;
    case GD_R_BYTECOUNTL: gd_l1_write(dev, L1R_BYTECOUNTL, value); break;
    case GD_R_BYTECOUNTH: gd_l1_write(dev, L1R_BYTECOUNTH, value); break;
    case GD_R_DRIVESEL:  gd_l1_write(dev, L1R_DRIVESEL, value);  break;
    case GD_R_SECTORNUM: gd_l1_write(dev, L1R_SECTORNUM, value); break;
    case GD_R_INTREASON: gd_l1_write(dev, L1R_INTREASON, value); break;
    case GD_R_ERROR:     gd_l1_write(dev, L1R_ERROR, value);     break;
    case GD_R_STATUS:    gd_l1_write(dev, L1R_STATUS, value);    break;
    default: break;
    }
}

static void hal_write_data(gd_device_t *dev, uint16_t value)
{
    gd_l1_write(dev, L1R_DATA, value);
}

static void hal_write_command(gd_device_t *dev, uint8_t command)
{
    gd_l1_write(dev, L1R_COMMAND, command);
}

static uint8_t hal_read_control(gd_device_t *dev, uint8_t reg)
{
    switch (reg) {
    case GD_R_ERROR:     return (uint8_t)gd_l1_read(dev, L1R_ERROR);
    case GD_R_INTREASON: return (uint8_t)gd_l1_read(dev, L1R_INTREASON);
    case GD_R_SECTORNUM: return (uint8_t)gd_l1_read(dev, L1R_SECTORNUM);
    case GD_R_BYTECOUNTL: return (uint8_t)gd_l1_read(dev, L1R_BYTECOUNTL);
    case GD_R_BYTECOUNTH: return (uint8_t)gd_l1_read(dev, L1R_BYTECOUNTH);
    case GD_R_DRIVESEL:  return (uint8_t)gd_l1_read(dev, L1R_DRIVESEL);
    case GD_R_STATUS:    return (uint8_t)gd_l1_read(dev, L1R_STATUS);
    default: return 0;
    }
}

static uint16_t hal_read_data(gd_device_t *dev)
{
    return gd_l1_read(dev, L1R_DATA);
}

static void hal_assert_irq(gd_device_t *dev, int on)
{
    (void)dev;
    /* No silicio: GPIO INTRQ. Com nIEN=1 devia ser hi-Z em vez de
     * nivel [TODO-L1, ver nota em gd_l1_write/DEVCONTROL]. */
    hw->set_intrq(on ? 1 : 0);
}

static void hal_start_data_in(gd_device_t *dev, uint16_t bytecount)
{
    (void)dev;
    (void)bytecount;
    /* [TODO-L1] Na versao rapida: armar a SM de DATA com o Byte Count
     * e afirmar DRQ. No esqueleto nao ha SM de DATA (so' g1_dev_slow,
     * de registos), por isso nao ha nada a armar. */
}

static void hal_read_data_block(gd_device_t *dev, uint16_t *dst, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = gd_l1_read(dev, L1R_DATA);
}

static void hal_end_data_in(gd_device_t *dev)
{
    (void)dev;
    /* [TODO-L1] simetrico ao start_data_in. */
}

static void hal_memzero(void *dst, size_t n)
{
    memset(dst, 0, n);
}

const gd_hal_t gd_hal_rp2350 = {
    hal_micros,
    hal_write_control,
    hal_write_data,
    hal_write_command,
    hal_read_control,
    hal_read_data,
    hal_assert_irq,
    hal_start_data_in,
    hal_read_data_block,
    hal_end_data_in,
    hal_memzero
};
