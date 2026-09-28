/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "memdisc.h"
#include "gd_disc.h"

#define MEMDISC_TRACKS 3

/* FAD de inicio de cada track, como num GDI de GD-ROM. */
static const uint32_t track_fad[MEMDISC_TRACKS] = { 0, 16, 32 };
static const uint32_t track_len[MEMDISC_TRACKS]  = { 16, 16, 32 };

/*
 * Padrao por sector: os primeiros 4 bytes sao o FAD em big-endian, para
 * que um erro de offset de sector seja imediatamente visivel.
 */
void memdisc_expected(uint32_t fad, uint32_t sector_size, uint8_t *out)
{
    uint32_t i;
    for (i = 0; i < sector_size; i++) {
        out[i] = (uint8_t)(i < 4 ? (fad >> ((3 - i) * 8)) & 0xff
                                 : (fad * 31u + i) & 0xff);
    }
}

int memdisc_read_sectors(gd_disc_t *d, uint32_t fad, uint32_t n,
                         uint32_t sector_size, void *dst)
{
    memdisc_t *md = (memdisc_t *)d;
    uint32_t i, off = 0;

    md->read_count++;

    if (md->fail_on >= 0 && (int)md->read_count == md->fail_on) return -1;

    for (i = 0; i < n; i++) {
        if (sector_size != MEMDISC_SECTOR_SIZE) return -1;  /* so 2048 */
        if (fad + i >= MEMDISC_SECTORS) return -1;
        memcpy((uint8_t *)dst + off, md->data[fad + i], sector_size);
        off += sector_size;
    }
    return 0;
}

/*
 * 408 bytes (spec seca 8.2). Estrutura:
 *   [0..395]   99 entradas de 4 bytes: Control/ADR + FAD, tracks 1..99
 *   [396..399] primeira track
 *   [400..403] ultima track
 *   [404..407] lead-out
 *
 * Duas regras que o GD-ROM real segue e que nao estao na spec:
 *  - ADR e' forcado a 1 (o subcode so esta disponivel no canal Q);
 *  - nas entradas de primeira/ultima track o "FAD" e' na verdade o
 *    numero da track em big-endian.
 */
void memdisc_fill_toc(gd_disc_t *d, int area, uint8_t *out)
{
    int i, first_track, last_track;
    uint32_t leadout;

    memset(out, 0xff, GD_TOC_SIZE);

    /* Densidade dupla so existe em GD-ROM. Noutros discos a segunda
     * area e' inteiramente 0xFF. */
    if (area == 1 && d->disc_format != GD_FORMAT_GDROM) return;

    first_track = 1;
    last_track  = MEMDISC_TRACKS;

    if (area == 1) first_track = 3;

    /* Numa GD-ROM de densidade unica so as tracks 1 e 2 estao na
     * primeira area; a 3 e onwards e' de dupla densidade. */
    if (d->disc_format == GD_FORMAT_GDROM && area == 0) last_track = 2;

    for (i = 0; i < 99; i++) {
        uint8_t *p = out + i * 4;
        if (i + 1 < first_track || i + 1 > last_track) continue;
        p[0] = (uint8_t)(((d->toc[i].control & 0x0f) << 4) | 0x01);
        p[1] = (uint8_t)(d->toc[i].fad >> 16);
        p[2] = (uint8_t)(d->toc[i].fad >> 8);
        p[3] = (uint8_t)(d->toc[i].fad);
    }

    /* Primeira / ultima track: o campo FAD leva o numero da track. */
    out[396] = 0x01;
    out[397] = (uint8_t)(first_track >> 8);
    out[398] = (uint8_t)first_track;
    out[399] = 0x00;

    out[400] = 0x01;
    out[401] = (uint8_t)(last_track >> 8);
    out[402] = (uint8_t)last_track;
    out[403] = 0x00;

    leadout = d->leadout_fad;
    if (d->disc_format == GD_FORMAT_GDROM && area == 0) {
        leadout = track_fad[1] + track_len[1];
    }
    out[404] = 0x01;
    out[405] = (uint8_t)(leadout >> 16);
    out[406] = (uint8_t)(leadout >> 8);
    out[407] = (uint8_t)leadout;
}

/*
 * 6 bytes (spec seca 8.2):
 *   [0] reescrito pelo chamador com o estado da unidade
 *   [1] reservado
 *   [2] numero de sessoes (ou o numero da track, se sessao especifica)
 *   [3..5] EndFAD (sessao 0) ou StartFAD (sessao concreta), big-endian
 *
 * Um GD-ROM tem 2 sessoes: a comeca na track 1, a 2 na track 3.
 */
void memdisc_fill_session_info(gd_disc_t *d, int session, uint8_t *out)
{
    out[0] = 0x02;   /* placeholder, substituido pelo estado */
    out[1] = 0x00;

    if (session == 0) {
        out[2] = d->num_sessions;
        out[3] = (uint8_t)(d->leadout_fad >> 16);
        out[4] = (uint8_t)(d->leadout_fad >> 8);
        out[5] = (uint8_t)(d->leadout_fad);
    } else if (session == 1) {
        out[2] = 1;
        out[3] = (uint8_t)(track_fad[0] >> 16);
        out[4] = (uint8_t)(track_fad[0] >> 8);
        out[5] = (uint8_t)(track_fad[0]);
    } else if (session == 2) {
        out[2] = 3;
        out[3] = (uint8_t)(track_fad[2] >> 16);
        out[4] = (uint8_t)(track_fad[2] >> 8);
        out[5] = (uint8_t)(track_fad[2]);
    } else {
        memset(out, 0, 6);
    }
}

uint8_t memdisc_track_of_fad(gd_disc_t *d, uint32_t fad)
{
    int i;
    (void)d;
    for (i = 0; i < MEMDISC_TRACKS; i++) {
        uint32_t start = track_fad[i];
        if (fad >= start && fad < start + track_len[i]) return (uint8_t)(i + 1);
    }
    return 0xaa;   /* FAD fora de qualquer track */
}

void memdisc_track_control(gd_disc_t *d, uint8_t track,
                           uint8_t *control, uint8_t *adr)
{
    if (d && track >= 1 && track <= MEMDISC_TRACKS) {
        *control = d->toc[track - 1].control;
        *adr    = d->toc[track - 1].adr;
    } else {
        *control = 0x04;
        *adr     = 0x01;
    }
}

void memdisc_init(memdisc_t *md, int high_density)
{
    int i;

    memset(md, 0, sizeof *md);
    md->fail_on = -1;
    md->read_count = 0;

    md->disc.present       = 1;
    md->disc.disc_format   = GD_FORMAT_GDROM;   /* 0x8 */
    md->disc.high_density  = high_density;
    md->disc.num_tracks    = MEMDISC_TRACKS;
    md->disc.num_sessions  = 2;   /* GD-ROM tem sempre 2 */
    md->disc.leadout_fad   = GD_LEADOUT_FAD;
    md->disc.fill_toc      = memdisc_fill_toc;
    md->disc.fill_session_info = memdisc_fill_session_info;
    md->disc.track_of_fad  = memdisc_track_of_fad;
    md->disc.track_control = memdisc_track_control;
    md->disc.read_sectors  = memdisc_read_sectors;
    md->disc.priv = md;

    for (i = 0; i < MEMDISC_SECTORS; i++) {
        memdisc_expected((uint32_t)i, MEMDISC_SECTOR_SIZE, md->data[i]);
    }

    for (i = 0; i < MEMDISC_TRACKS; i++) {
        md->disc.toc[i].fad = track_fad[i];
        md->disc.toc[i].control = 0x04;   /* track de dados */
        md->disc.toc[i].adr = 0x01;
    }
}
