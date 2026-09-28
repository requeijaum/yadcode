/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_gdi.h - Leitor de GDI, o formato de runtime.
 *
 * POR QUE GDI E NAO CUE
 * ---------------------
 * O GDI e' uma lista de LBA absolutos por track. Nao tem pregap, nao tem
 * indice, nao tem modos de sector: e' a coisa mais simples possivel de
 * mapear FAD -> ficheiro, e e' o que o GDEMU, o MODE, o iceGDROM e o
 * Dreamdrive leem. O CUE e' o formato de preservacao (Redump) e precisa
 * de ser convertido - ver gd_cue.h e a ferramenta cue2gdi.
 *
 * FORMATO (nao existe especificacao oficial; inferido de 3 implementacoes
 * independentes: nullDC, Flycast e o gdidrop, este ultimo BSD-2)
 * ------------------------------------------------------------------
 *   linha 1  : <track_count>              decimal, 3..99
 *   linha N  : <TRACK> <LBA> <CTRL> <SSIZE> <file|"file"> <OFFSET>
 *
 *   TRACK   1..track_count
 *   LBA     = FAD - 150.  Track 1 = 0, track 3 = 45000 (= FAD 45150)
 *   CTRL    0 = audio, 4 = dados
 *   SSIZE   2352 (raw, com sync) ou 2048 (cooked).  0 = track ausente
 *   file    opcionalmente entre aspas (o Redump usa sempre aspas)
 *   OFFSET  offset em bytes do primeiro sector DA TRACK.  0 = byte 0,
 *           ou seja o inicio do sector 2352 com o sync.  Pode ser
 *           negativo em dumps antigos do cdrwin (-8).
 *
 * byte_pos(FAD) = OFFSET + (FAD - (LBA + 150)) * SSIZE
 *
 * Nao ha mais nada: sem REM, sem cabecalho, sem indice, sem newlines
 * obrigatorios. O parser e' um fluxo de tokens.
 *
 * ARMADILHAS
 * ----------
 * 1. LBA 45000 e' o INDEX 00 (pause). Os dados comecam no FAD 45150.
 * 2. O sector entre EndFAD(track 2) e StartFAD(track 3) nao existe em
 *    lado nenhum: e' o gap entre as duas areas. O backend tem de
 *    devolver "ausente" e nao zeros.
 * 3. O Redump embebe os 150 sectores de Pause no inicio do ficheiro da
 *    track. O OFFSET=0 aponta para eles, que ficam orfaos.
 */
#ifndef GD_GDI_H
#define GD_GDI_H

#include <stdint.h>
#include <stdio.h>
#include "gd_spec.h"
#include "gd_format.h"
#include "gd_fs.h"
#include "gd_disc.h"

#define GD_MAX_TRACKS   99
#define GD_PATH_MAX     256

typedef struct {
    int      track;               /* 1..99                              */
    uint32_t lba;                 /* tal como escrito no GDI (= FAD-150)*/
    uint32_t start_fad;           /* lba + 150                          */
    uint32_t end_fad;             /* inferido do tamanho do ficheiro    */
    uint8_t  ctrl;                /* 0 = audio, 4 = dados               */
    uint8_t  adr;                 /* extraido do track_of_fad do disco  */
    uint32_t sector_size;         /* 2352 ou 2048                       */
    int32_t  offset;              /* pode ser negativo                  */
    char     path[GD_PATH_MAX];
    int      missing;             /* SSIZE == 0                         */
} gd_gdi_track_t;

typedef struct {
    gd_gdi_track_t track[GD_MAX_TRACKS];
    int            ntracks;
} gd_gdi_t;

/* Le sectors a partir de um GDI, para o backend de ficheiros. */
typedef struct {
    /* Le `n` sectores a partir de `fad`, com `sector_size` bytes cada.
     * Devolve 0 em sucesso, -1 se o sector nao existe. */
    int (*read_sectors)(void *ctx, uint32_t fad, uint32_t n,
                        uint32_t sector_size, void *dst);
    void *ctx;
} gd_gdi_reader_t;

/*
 * Le um ficheiro .gdi. `gdi_path` e' usado tambem para resolver os
 * caminhos relativos dos .bin (raiz = pasta do .gdi).
 *
 * Devolve 0 em sucesso, -1 em erro (com `err` preenchido).
 */
int gd_gdi_parse(const gd_fs_t *fs, const char *gdi_path,
                 gd_gdi_t *out, const char **err);

/*
 * Valida a geometria de um GDI de GD-ROM segundo as regras que o
 * GD-ROM real e as implementacoes de referencia aplicam.
 * Devolve 0 se valido, -1 com `err` preenchido.
 */
int gd_gdi_validate(const gd_gdi_t *g, const char **err);

/* Constrói a gd_disc_t que o resto do emulador consome. */
typedef struct gd_gdi_disc {
    gd_disc_t disc;
    gd_gdi_t   gdi;
    gd_gdi_reader_t rd;
} gd_gdi_disc_t;

int gd_gdi_open_disc(const gd_fs_t *fs, const char *gdi_path,
                     gd_gdi_disc_t *out, const char **err);

/*
 * Escreve o GDI. A linha e':
 *   TRACK LBA CTRL SSIZE "file" OFFSET
 * A track ausente escreve `SSIZE 0` e ficheiro `none`, que e' a
 * convencao que o nullDC usava e que o GDEMU e o MODE comem.
 * Devolve 0 em sucesso.
 */
int gd_gdi_write(const gd_gdi_t *g, FILE *f);

/* Le sectores directamente do GDI (a gd_disc_t chama esta). */
int gd_gdi_read_sectors(gd_disc_t *d, uint32_t fad, uint32_t n,
                        uint32_t sector_size, void *dst);

#endif /* GD_GDI_H */
