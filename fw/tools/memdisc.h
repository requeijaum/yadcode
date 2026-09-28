/* SPDX-License-Identifier: Apache-2.0 */
/*
 * memdisc.h - Imagem de GD-ROM em memoria, para os testes.
 *
 * Reproduz a estrutura de um GDI de GD-ROM: 3 tracks, 2 sessoes,
 * lead-out em 549300.
 */
#ifndef MEMDISC_H
#define MEMDISC_H

#include "gd_disc.h"
#include "gd_spec.h"

#define MEMDISC_SECTORS 64
#define MEMDISC_SECTOR_SIZE 2048
#define MEMDISC_TRACKS 3

typedef struct {
    gd_disc_t disc;
    uint8_t   data[MEMDISC_SECTORS][MEMDISC_SECTOR_SIZE];
    uint32_t  read_count;
    int       fail_on;
} memdisc_t;

void memdisc_init(memdisc_t *md, int high_density);
int  memdisc_read_sectors(gd_disc_t *d, uint32_t fad, uint32_t n,
                          uint32_t sector_size, void *dst);
void memdisc_fill_toc(gd_disc_t *d, int area, uint8_t *out);
void memdisc_fill_session_info(gd_disc_t *d, int session, uint8_t *out);
uint8_t memdisc_track_of_fad(gd_disc_t *d, uint32_t fad);
void memdisc_track_control(gd_disc_t *d, uint8_t track,
                           uint8_t *control, uint8_t *adr);

/* Padrao esperado do sector FAD, para verificacao. */
void memdisc_expected(uint32_t fad, uint32_t sector_size, uint8_t *out);

#endif /* MEMDISC_H */
