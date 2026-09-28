/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_disc.h - Interface da camada de imagem (L5).
 *
 * A camada SPI nunca sabe de onde vem o dado: SD, eMMC, RAM, ou um teste.
 */
#ifndef GD_DISC_H
#define GD_DISC_H

#include <stdint.h>
#include "gd_spec.h"

typedef struct gd_disc gd_disc_t;

/* Uma entrada da TOC: Control/ADR + FAD 24-bit BE, 4 bytes. */
typedef struct {
    uint8_t  control;      /* bits 7..4, tal como vem da imagem        */
    uint8_t  adr;          /* bits 3..0. O GD-ROM real forca ADR = 1.  */
    uint32_t fad;
} gd_toc_entry_t;

/* FAD do lead-out de um GD-ROM de densidade unica. Constante no
 * flycast (Disc::FillGDSession) e no iceGDROM. */
#define GD_LEADOUT_FAD 549300u

struct gd_disc {
    /* 1 se ha disco carregado, 0 se a bandeja esta vazia. */
    int      present;

    /* GD-ROM = 0x8 (spec seca 2.3, campo Disc Format) */
    uint8_t  disc_format;

    /* 1 se o disco e de alta densidade (GD de dupla densidade). */
    int      high_density;

    /* TOC em memoria: 99 entradas. Preenchida por fill_toc(). */
    gd_toc_entry_t toc[GD_MAX_TRACKS];

    /* Quantas entradas de toc[] sao validas. Um track_of_fad pode
     * devolver 0xAA (FAD fora de qualquer track) e um TNO de 0xAA
     * indexado sem cuidado daria um overrun. */
    uint8_t  num_tracks;

    /* FAD do lead-out (549300 num GD-ROM). */
    uint32_t leadout_fad;

    /* Um GD-ROM tem sempre 2 sessoes: a 1 comeca na track 1, a 2 na
     * track 3 (a zona de baixa densidade). Ver Disc::FillGDSession. */
    uint8_t  num_sessions;

    /*
     * Preenche os 408 bytes da TOC (spec seca 8.2).
     * area = 0 -> densidade unica, area = 1 -> dupla densidade.
     */
    void (*fill_toc)(gd_disc_t *disc, int area, uint8_t *out);

    /* Numero da track (1-based) a que o FAD pertence, ou 0xAA. */
    uint8_t (*track_of_fad)(gd_disc_t *disc, uint32_t fad);

    /* Control/ADR da track, para o Byte 2 do REQ_STAT. */
    void (*track_control)(gd_disc_t *disc, uint8_t track,
                          uint8_t *control, uint8_t *adr);

    /*
     * Preenche os 6 bytes de REQ_SES.
     * session = 0 -> devolve o total de sessoes e o EndFAD do disco.
     * O Byte 0 e' reescrito pelo chamador com o estado da unidade.
     */
    void (*fill_session_info)(gd_disc_t *disc, int session, uint8_t *out);

    /*
     * Le n sectores de `sector_size` bytes a partir do FAD `fad`.
     * Devolve 0 em sucesso, -1 em erro.
     */
    int (*read_sectors)(gd_disc_t *disc, uint32_t fad, uint32_t n,
                        uint32_t sector_size, void *dst);

    void *priv;
};

#endif /* GD_DISC_H */
