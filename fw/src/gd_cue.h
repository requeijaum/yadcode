/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_cue.h - Leitor de CUE, o formato de preservacao do Redump.
 *
 * POR QUE LER CUE SE O RUNTIME USA GDI
 * -------------------------------------
 * O CUE e' o formato de preservacao: guarda pregap, postgap, indices e
 * as marcas de area. O GDI perde tudo isso e e' por isso que o Redump o
 * considera insuficiente ("GDI format lacks of track pregap length").
 * Mas o GDI e' trivial de mapear, e e' o que os ODEs leem.
 *
 * Portanto: CUE entra, GDI sai. Ver a ferramenta cue2gdi.
 *
 * FORMATOS ACEITES
 * ----------------
 * 1. Multi-Cue do Redump (o que a maioria dos dumps tem hoje):
 *
 *      REM SINGLE-DENSITY AREA
 *      FILE "Game (Track 1).bin" BINARY
 *        TRACK 01 MODE1/2352
 *          INDEX 01 00:00:00
 *      REM HIGH-DENSITY AREA
 *      FILE "Game (Track 3).bin" BINARY
 *        TRACK 03 MODE1/2352
 *          INDEX 01 00:00:00
 *
 *    O marcador `REM HIGH-DENSITY AREA` e' o que diz ao parser que o
 *    disco e' um GD-ROM e onde comeca a area de alta densidade
 *    (FAD 45150). Sem ele um CUE de GD e' indistinguivel de um CD.
 *
 * 2. TOSEC / httpd-ack, so com as tracks de alta densidade:
 *
 *      FILE "track03.bin" BINARY
 *        TRACK 03 MODE1/2352
 *          PREGAP 10:00:00
 *          INDEX 01 00:00:00
 *
 *    Aqui o PREGAP e' a unica coisa que diz onde a track 3 comeca. O
 *    valor correcto e' 10:00:00 (45000) e o ficheiro NAO contem o
 *    pregap; o leitor tem de o sintetizar. Ha sheets com 10:02:00
 *    (45150), que ja aponta para o INDEX 01. Aceitamos os dois e
 *    distinguimos.
 *
 *    O Flycast NAO suporta este formato: nao tem handler para PREGAP, e
 *    nunca marca o disco como GD-ROM. E' uma lacuna real.
 *
 * O QUE NAO FAZEMOS
 * ------------------
 * - `REM SESSION 01/02`: errado para GD-ROM. O admin do Redump e'
 *   explicito - "GDs aren't multisessional, those are 2 separate images
 *   written on the same media". Sao duas AREAS, nao duas sessoes.
 * - `POSTGAP`: aceite e registado, mas o GDI de saida nao o consegue
 *   representar. Perde-se. Registamos um aviso.
 * - CHDs nao sao suportados. Ver o doc 15.
 */
#ifndef GD_CUE_H
#define GD_CUE_H

#include <stdint.h>
#include "gd_spec.h"
#include "gd_format.h"
#include "gd_fs.h"
#include "gd_gdi.h"

typedef enum {
    GD_SECT_AUDIO,        /* 2352 */
    GD_SECT_MODE1_2048,   /* 2048 */
    GD_SECT_MODE1_2352,   /* 2352 */
    GD_SECT_MODE2_2336,   /* 2336 */
    GD_SECT_MODE2_2352,   /* 2352 */
    GD_SECT_CDI_2336,     /* 2336 */
    GD_SECT_CDI_2352,     /* 2352 */
    GD_SECT_UNKNOWN
} gd_cue_sector_t;

typedef struct {
    int            track;
    gd_cue_sector_t sector;
    uint32_t       sector_size;
    uint8_t        ctrl;          /* 0 = audio, 4 = dados          */

    uint32_t       index0;        /* FAD, ou UINT32_MAX se ausente */
    uint32_t       index1;        /* FAD                            */

    /* FAD onde a track comeca no disco. Nao e' derivavel do CUE sem
     * somar as duracoes, e o Redump calcula-o a partir dos marcadores
     * de area. */
    uint32_t       start_fad;
    uint32_t       pregap_sectors;   /* a sintetizar antes do INDEX 01 */
    uint32_t       postgap_sectors;

    char           path[GD_PATH_MAX];
    long           file_size;       /* -1 se ainda nao lido           */
} gd_cue_track_t;

typedef struct {
    gd_cue_track_t track[GD_MAX_TRACKS];
    int            ntracks;
    int            is_gdrom;
    int            saw_high_density;   /* encontrou REM HIGH-DENSITY */
    uint32_t       catalog_len;
    char           catalog[128];
} gd_cue_t;

/*
 * Le um ficheiro .cue. `cue_path` e' usado tambem para resolver os
 * caminhos relativos dos .bin.
 * Devolve 0 em sucesso, -1 com `err` preenchido.
 */
int gd_cue_parse(const gd_fs_t *fs, const char *cue_path,
                 gd_cue_t *out, const char **err);

/* Valida a geometria de um CUE de GD-ROM. 0 se ok. */
int gd_cue_validate(const gd_cue_t *c, const char **err);

/*
 * Converte para GDI. Escreve em `gdi` as tracks com LBA, CTRL, SSIZE,
 * ficheiro e offset. `fs` e' usado para medir os ficheiros .bin.
 * Devolve 0 em sucesso.
 */
int gd_cue_to_gdi(const gd_fs_t *fs, const gd_cue_t *c,
                  gd_gdi_t *gdi, const char **err);

/* Converte "mm:ss:ff" em sectores (1/75 s). Devolve -1 se mal formado. */
int gd_cue_parse_time(const char *s, uint32_t *frames);

#endif /* GD_CUE_H */
