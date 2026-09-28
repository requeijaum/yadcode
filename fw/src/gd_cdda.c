/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "gd_cdda.h"
#include "gd_taskfile.h"
#include "gd_spi.h"

/* ------------------------------------------------------------------ */
/* CRC-16/XMODEM                                                      */
/* ------------------------------------------------------------------ */

/*
 * Polinomio 0x1021, init 0x0000, sem reflect, saida COMPLEMENTADA.
 *
 * Isto e' o que a implementacao de referencia descreve ("return ~crc")
 * e e' o que um GD-ROM real produz. Nao e' uma variante padrao: o
 * CRC-16/XMODEM (mesmo polinomio e mesma seed) NAO tem complemento
 * final, e da 0x31C3 para "123456789" em vez de 0xCE3C.
 *
 * A diferenca importa porque o subcode e' copiado verbatim pelos
 * jogos. Sem um dump de hardware nao da para decidir - ver
 * doc 13, questao aberta F. Mantido o comportamento observado.
 */
uint16_t gd_cdda_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

/* ------------------------------------------------------------------ */
/* Estado                                                             */
/* ------------------------------------------------------------------ */

gd_cdda_t *gd_cdda_get(gd_device_t *dev)
{
    return &dev->cdda;
}

void gd_cdda_reset(gd_device_t *dev)
{
    memset(&dev->cdda, 0, sizeof dev->cdda);
    dev->cdda.status = GD_CDDA_NO_INFO;
}

static uint32_t fad_from(const uint8_t *b, int is_msf)
{
    if (is_msf) return (uint32_t)((b[0] * 60u * 75u) + (b[1] * 75u) + b[2]);
    return (uint32_t)(((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]);
}

static uint8_t audio_status_byte(gd_cdda_status_t s)
{
    switch (s) {
    case GD_CDDA_PLAYING:    return GD_AUD_PLAY;
    case GD_CDDA_PAUSED:     return GD_AUD_PAUSE;
    case GD_CDDA_TERMINATED: return GD_AUD_DONE;
    case GD_CDDA_ABNORMAL:   return GD_AUD_ABEND;
    case GD_CDDA_NO_INFO:
    default:                 return GD_AUD_NONE;   /* 0x15, o default */
    }
}

static void put_be24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* Converte um valor binario de 0 a 99 para dois digitos BCD. */
static uint8_t bin2bcd(uint32_t v)
{
    if (v > 99) v = 99;
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* ------------------------------------------------------------------ */
/* Transporte                                                         */
/* ------------------------------------------------------------------ */

int gd_cdda_play(gd_device_t *dev, const uint8_t *pkt)
{
    gd_cdda_t *c = &dev->cdda;
    uint8_t ptype = pkt[1] & 0x07;
    int is_msf = (ptype == 2);

    if (!dev->disc || !dev->disc->present) {
        gd_device_set_state(dev, GD_STATE_NODISC);
        return -1;
    }

    c->repeats = (uint8_t)(pkt[6] & 0x0f);
    c->param_type = ptype;

    if (ptype == 0x07) {
        /*
         * Retomar. Se a posicao corrente ja passou do fim, a
         * reproducao terminou; caso contrario continua a tocar.
         */
        if (c->end_fad && c->curr_fad > c->end_fad) {
            c->status = GD_CDDA_TERMINATED;
            gd_device_set_state(dev, GD_STATE_STANDBY);
        } else {
            c->status = GD_CDDA_PLAYING;
            gd_device_set_state(dev, GD_STATE_PLAY);
        }
    } else if (ptype == 1 || ptype == 2) {
        c->start_fad = fad_from(&pkt[2], is_msf);
        c->curr_fad  = c->start_fad;
        c->end_fad   = fad_from(&pkt[8], is_msf);
        c->status    = GD_CDDA_PLAYING;
        gd_device_set_state(dev, GD_STATE_PLAY);
    } else {
        /* A spec so define 1 (FAD), 2 (MSF) e 7 (retomar). */
        return -1;
    }

    c->last_cmd = GD_SPI_CD_PLAY;
    c->last_tick_us = 0;
    dev->cur_fad = c->curr_fad;
    gd_taskfile_set_status(dev, GD_ST_DSC, 1);   /* seek completo */
    return 0;
}

int gd_cdda_seek(gd_device_t *dev, const uint8_t *pkt)
{
    gd_cdda_t *c = &dev->cdda;
    uint8_t ptype = pkt[1] & 0x07;

    if (!dev->disc || !dev->disc->present) return -1;

    c->param_type = ptype;

    switch (ptype) {
    case 0x01:   /* FAD */
    case 0x02:   /* MSF */
        c->start_fad = fad_from(&pkt[2], ptype == 2);
        c->curr_fad  = c->start_fad;
        gd_taskfile_set_status(dev, GD_ST_DSC, 1);
        break;

    case 0x03:   /* stop -> home */
        c->start_fad = 150;
        c->curr_fad  = 150;
        c->status    = GD_CDDA_NO_INFO;
        gd_device_set_state(dev, GD_STATE_STANDBY);
        gd_taskfile_set_status(dev, GD_ST_DSC, 1);
        break;

    case 0x04:   /* pausa de audio */
        if (c->status == GD_CDDA_PLAYING) c->status = GD_CDDA_PAUSED;
        break;

    default:
        return -1;
    }

    /*
     * Um seek interrompe o audio, mas o param type 3 (stop -> home) e'
     * o unico que deixa a unidade em STANDBY em vez de PAUSE.
     */
    if (ptype != 0x03) {
        if (c->status == GD_CDDA_PLAYING) c->status = GD_CDDA_PAUSED;
        gd_device_set_state(dev, GD_STATE_PAUSE);
    }
    c->last_cmd = GD_SPI_CD_SEEK;
    dev->cur_fad = c->curr_fad;
    return 0;
}

int gd_cdda_scan(gd_device_t *dev, const uint8_t *pkt)
{
    gd_cdda_t *c = &dev->cdda;

    if (!dev->disc || !dev->disc->present) return -1;

    c->last_cmd = GD_SPI_CD_SCAN;
    gd_device_set_state(dev, GD_STATE_SCAN);

    /* Byte 2 bit 0 = direccao, Byte 3 = velocidade. */
    c->param_type = (uint8_t)(pkt[2] & 0x01);
    c->repeats    = pkt[3];
    return 0;
}

void gd_cdda_tick(gd_device_t *dev, uint32_t now_us)
{
    gd_cdda_t *c = &dev->cdda;
    uint32_t dt, sectors;

    if (c->status != GD_CDDA_PLAYING) {
        c->last_tick_us = now_us;
        return;
    }

    dt = now_us - c->last_tick_us;
    if (dt < GD_SCD_REFRESH_US) return;

    /* Um CD avanca a 75 sectores por segundo. */
    sectors = dt / 13333u;
    c->curr_fad += sectors;
    c->last_tick_us += sectors * 13333u;

    if (c->end_fad && c->curr_fad > c->end_fad) {
        /*
         * No fim da reproducao a posicao fica no ULTIMO sector lido e o
         * audio status passa a "terminado". Se voltasse a zero, o host
         * saltava para o inicio do disco.
         */
        c->curr_fad = c->end_fad;
        c->status = GD_CDDA_TERMINATED;
        gd_device_set_state(dev, GD_STATE_PAUSE);
    }
    dev->cur_fad = c->curr_fad;
}

/* ------------------------------------------------------------------ */
/* Subcode                                                            */
/* ------------------------------------------------------------------ */

/*
 * Constroi o Q de 12 bytes:
 *   [0]  Control/ADR
 *   [1]  numero da track, BCD
 *   [2]  index, BCD
 *   [3..5] minutos, segundos, frames desde o inicio da track (BCD)
 *   [6]  reservado, zero
 *   [7..9] minutos, segundos, frames da posicao absoluta (BCD)
 *   [10..11] CRC-16 sobre [0..9]
 */
static void build_q_bcd(const gd_device_t *dev, uint32_t fad, uint8_t *q)
{
    const gd_disc_t *d = dev->disc;
    uint8_t tno = gd_spi_track_of_fad(d, fad);
    uint32_t elapsed = 0, min, sec, frm;

    /*
     * 0xAA significa "FAD fora de qualquer track". Indexar a TOC com
     * isso dava um overrun, por isso cai para a track 1 - e o host ve
     * uma posicao valida em vez de lixo.
     */
    if (!d || tno < 1 || tno > d->num_tracks) {
        tno = 1;
        if (d && d->num_tracks >= 1) elapsed = (fad > d->toc[0].fad)
                                             ? (fad - d->toc[0].fad) : 0;
    } else {
        const gd_toc_entry_t *e = &d->toc[tno - 1];
        elapsed = (fad > e->fad) ? (fad - e->fad) : 0;
    }

    min = elapsed / (60u * 75u);
    sec = (elapsed / 75u) % 60u;
    frm = elapsed % 75u;

    q[0] = 0x40;   /* Control = 4 (dados), ADR = 0 */
    q[1] = bin2bcd(tno);
    q[2] = bin2bcd(1);
    q[3] = bin2bcd(min);
    q[4] = bin2bcd(sec);
    q[5] = bin2bcd(frm);
    q[6] = 0x00;
    min = fad / (60u * 75u);
    sec = (fad / 75u) % 60u;
    frm = fad % 75u;
    q[7]  = bin2bcd(min);
    q[8]  = bin2bcd(sec);
    q[9]  = bin2bcd(frm);

    put_be16(&q[10], gd_cdda_crc16(q, 10));
}

/*
 * Expande cada byte do Q em 8 bytes de subcode P: um bit por byte, com
 * 0x40 para um bit a 1 e 0x00 para um bit a 0. 12 bytes x 8 = 96,
 * que e' exactamente o bloco P..W que a spec pede.
 */
static void expand_p(const uint8_t *q, uint8_t *p)
{
    int i, bit;
    for (i = 0; i < 12; i++) {
        for (bit = 0; bit < 8; bit++) {
            p[i * 8 + bit] = (q[i] & (0x80u >> bit)) ? 0x40 : 0x00;
        }
    }
}

static uint32_t scd_raw(const gd_device_t *dev, uint8_t *out, uint32_t cap)
{
    uint8_t q[12];

    if (cap < GD_SCD_RAW_LEN) return 0;
    memset(out, 0, GD_SCD_RAW_LEN);

    out[0] = 0x00;
    out[1] = audio_status_byte(dev->cdda.status);
    put_be16(&out[2], GD_SCD_RAW_LEN);

    build_q_bcd(dev, dev->cdda.curr_fad, q);
    expand_p(q, &out[4]);
    return GD_SCD_RAW_LEN;
}

static uint32_t scd_q(const gd_device_t *dev, uint8_t *out, uint32_t cap)
{
    const gd_disc_t *d = dev->disc;
    uint8_t tno;
    uint32_t fad = dev->cdda.curr_fad, elapsed = 0;

    if (cap < GD_SCD_Q_LEN) return 0;
    memset(out, 0, GD_SCD_Q_LEN);

    tno = gd_spi_track_of_fad(d, fad);
    if (!d || tno < 1 || tno > d->num_tracks) {
        tno = 1;
        if (d && d->num_tracks >= 1) elapsed = (fad > d->toc[0].fad)
                                             ? (fad - d->toc[0].fad) : 0;
    } else {
        const gd_toc_entry_t *e = &d->toc[tno - 1];
        elapsed = (fad > e->fad) ? (fad - e->fad) : 0;
    }

    out[0] = 0x00;
    out[1] = audio_status_byte(dev->cdda.status);
    put_be16(&out[2], GD_SCD_Q_LEN);

    /*
     * FORMATO 1: o numero da track e' BINARIO, nao BCD. E' o mesmo
     * campo que o formato 0 codifica em BCD, e a diferenca e' real.
     */
    out[4] = 0x41;                 /* Control 4, ADR forcado a 1 */
    out[5] = tno;                  /* binario */
    out[6] = 0x01;                 /* index */
    put_be24(&out[7], elapsed);
    out[10] = 0x00;                /* reservado */
    put_be24(&out[11], fad);
    return GD_SCD_Q_LEN;
}

static uint32_t scd_upc(uint8_t *out, uint32_t cap)
{
    int i;
    if (cap < GD_SCD_UPC_LEN) return 0;
    memset(out, 0, GD_SCD_UPC_LEN);
    out[0] = 0x00;
    out[1] = 0x00;
    put_be16(&out[2], GD_SCD_UPC_LEN);
    out[4] = 0x02;                 /* codigo de formato: UPC */
    /* Bytes 9..21: o numero de catalogo, em ASCII, zeros = invalido. */
    for (i = 0; i < 13; i++) out[9 + i] = '0';
    return GD_SCD_UPC_LEN;
}

static uint32_t scd_isrc(const gd_device_t *dev, uint8_t *out, uint32_t cap)
{
    if (cap < GD_SCD_ISRC_LEN) return 0;
    memset(out, 0, GD_SCD_ISRC_LEN);
    out[0] = 0x00;
    out[1] = audio_status_byte(dev->cdda.status);
    put_be16(&out[2], GD_SCD_ISRC_LEN);
    out[4] = 0x02;                 /* codigo de formato: ISRC */
    return GD_SCD_ISRC_LEN;
}

uint32_t gd_cdda_get_subcode(gd_device_t *dev, uint8_t format,
                             uint8_t *out, uint32_t cap)
{
    switch (format) {
    case GD_SCD_RAW:  return scd_raw(dev, out, cap);
    case GD_SCD_Q:    return scd_q(dev, out, cap);
    case GD_SCD_UPC:  return scd_upc(out, cap);
    case GD_SCD_ISRC: return scd_isrc(dev, out, cap);
    default:          return 0;
    }
}
