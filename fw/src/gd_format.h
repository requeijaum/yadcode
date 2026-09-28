/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_format.h - Constantes do formato GD-ROM e dos formatos de imagem.
 *
 * Fonte primaria das constantes:
 *   SEGA, "GD-ROM Format Basic Specifications Ver. 2.14",
 *   Document GDP-0000-02, 17/03/1999. Tabela 4-1 "Specified Addresses".
 *   https://segaretro.org/images/5/5d/Gdfm_k214e.pdf
 *
 * TODAS as aritmeticas foram verificadas contra essa tabela. Onde a spec
 * e ambigua ou contradiz o ecosistema, o comment diz-o.
 *
 * RELACAO LBA / FAD / ATime
 * -------------------------
 *   LBA  = FAD - 150
 *   ATime = FAD / 75 -> minutos:segundos:frames
 *
 * E' esta a armadilha principal do GD-ROM: o "LBA 45000" de que toda a
 * gente fala e' o INDEX 00 da track 3 (a zona de Pause de 2 s). Os
 * DADOS comecam no FAD 45150, que e' o INDEX 01. Um ODE que aponte para
 * 45000 em vez de 45150 le 150 sectores de pausa antes do primeiro byte
 * de dados, e a BIOS nao encontra a tabela de boot.
 */
#ifndef GD_FORMAT_H
#define GD_FORMAT_H

#include <stdint.h>
#include "gd_spec.h"

/* ------------------------------------------------------------------ */
/* Tabela 4-1 da spec GD-ROM Format Basic Specifications Ver. 2.14     */
/* ------------------------------------------------------------------ */
/*
 * ATENCAO: so' existe LBA para FAD >= 150. O FAD 0 e' o inicio
 * absoluto do disco e nao tem LBA correspondente, porque a aritmetica
 * e' sem sinal. Passar fad < 150 da' um resultado enorme em vez de
 * erro, por isso quem chama tem de garantir o intervalo.
 */
#define GD_FAD_TO_LBA(fad)        ((uint32_t)((fad) - 150u))
#define GD_LBA_TO_FAD(lba)        ((uint32_t)((lba) + 150u))
#define GD_FAD_HAS_LBA(fad)       ((fad) >= 150u)

/* Area Single-Density (baixa densidade) */
#define GD_LD_TRACK1_FAD          0x000000u   /* head da track 1, ATime 00:00:00 */
#define GD_LD_SYSTEM_ID0_FAD      0x000096u   /* 150   = 00:02:00 */
#define GD_LD_PVD0_FAD            0x0000a6u   /* 166   = 00:02:16 */
#define GD_LD_LEADOUT_FAD         0x004650u   /* 18000 = 04:00:00 (maximo)       */
#define GD_LD_SECTORS_MAX         18000u      /* 4 minutos                        */

/* Area High-Density (alta densidade) */
#define GD_HD_TRACK3_INDEX00_FAD  0x00afc8u   /* 45000 = 10:00:00 = o Pause      */
#define GD_HD_TRACK3_FAD          0x00b05eu   /* 45150 = 10:02:00 = INDEX 01     */
#define GD_HD_SYSTEM_ID1_FAD      0x00b05eu   /* 45150 - o BIOS le este primeiro */
#define GD_HD_PVD1_FAD            0x00b06eu   /* 45166 = 10:02:16                */
#define GD_HD_LAST_SECTOR_FAD     0x0861b3u   /* 549299 = 122:03:74 (ultimo)     */
#define GD_HD_LEADOUT_FAD         0x0861b4u   /* 549300 = 122:04:00              */
#define GD_HD_SECTORS_MAX         504300u     /* 112 min 4 s                      */

/* Verificado: 45000 + 504300 = 549300. A spec fecha. */

/*
 * Capacidade util (spec secao 1.2.2):
 *   LD: 18 000 sectores  = 36 000 KB
 *   HD: 504 300 sectores  = 1 008 600 KB
 *   total = 1 044 600 KB ~ 0,996 GiB
 *
 * O "~1,2 GB" que circula e' o tamanho do ficheiro de imagem quando
 * cada sector inclui os 96 bytes de subcode: 549 156 x 2448 =
 * 1 344 333 888 bytes. Nao e' capacidade de dados.
 */
#define GD_CAPACITY_LD_KB         36000u
#define GD_CAPACITY_HD_KB         1008600u
#define GD_CAPACITY_TOTAL_KB      1044600u

/*
 * O backup Lead Out com 549150 sectores (PLBA=549150) aparece em
 * ferramentas antigas (CloneCD, Dreamcast Doc v1). A spec oficial e' o
 * 549300. Usamos 549300 e so aceitamos 549150 se for preciso.
 */
#define GD_LEADOUT_FAD_ALT        549150u

/* ------------------------------------------------------------------ */
/* Padroes de gravacao (spec secao 3.1)                                */
/* ------------------------------------------------------------------ */
/*
 * Pattern I   - dados na track 03 (3 tracks no total)
 * Pattern II  - dados na track 03 seguida de audio (ate 96 tracks)
 * Pattern III - dados na track 03, audio, e dados finais junto ao bordo
 *               externo. E' o caso do Shenmue, Quake III, VF3tb.
 *
 * Em todos: 2 s de Pause no inicio e 2 s de Postgap no fim de cada track
 * de dados; uma track de dados tem de ter pelo menos 4 s incluindo o
 * postgap, excluindo a area de Pause.
 *
 * Quando uma track de audio e' seguida de uma de dados, ha 3 s de
 * pregap no inicio da track de dados: 1 s codificado como audio e 2 s
 * como dados. E' o "1 segundo e' audio encode, o resto e' data encode".
 */
#define GD_PAUSE_SECTORS          150u        /* 2 s                            */
#define GD_POSTGAP_SECTORS        150u        /* 2 s                            */
#define GD_PREGAP_AUDIO_SECTORS   225u        /* 3 s: 75 audio + 150 dados      */
#define GD_TRACK_MIN_SECTORS      300u        /* 4 s                            */

/* A spec e' explicita: a area de alta densidade so suporta Mode 1. */
#define GD_HD_MODE2_SUPPORTED     0

/* ------------------------------------------------------------------ */
/* Areas, nao sessoes                                                  */
/* ------------------------------------------------------------------ */
/*
 * IMPORTANTE: um GD-ROM tem DUAS AREAS, nao duas sessoes.
 *
 * O admin do Redump (F1ReB4LL) e explicito:
 *   "REM SESSION 01 and REM SESSION 02 are also wrong, these commands
 *    generate a multisessional disc TOC, while GDs aren't multisessional,
 *    those are 2 separate images written on the same media."
 *
 * A spec define 4 marcos: Lead_In 0, Lead_Out 0, Lead_In 1, Lead_Out 1.
 * Ou seja, 2 areas com lead-in e lead-out proprios, mais o security ring.
 *
 * Portanto:
 *   GET_TOC Select=0 -> area de baixa densidade, tracks 1..2,
 *                       lead-out = EndFAD(track 2) + 1
 *   GET_TOC Select=1 -> area de alta densidade, tracks 3..N,
 *                       lead-out = 549300
 *
 * A "sessao" e' apenas um campo de compatibilidade que a BIOS pode ler,
 * e que o emulador de referencia sintetiza com 2 sessoes. Ver
 * gd_taskfile.c / do_req_ses() e doc 13.
 */
enum { GD_AREA_SINGLE_DENSITY = 0, GD_AREA_HIGH_DENSITY = 1 };

/* Numeracao de tracks dentro de cada area (spec, e o GetToc do Flycast) */
#define GD_AREA_LD_FIRST_TRACK    1
#define GD_AREA_LD_LAST_TRACK     2
#define GD_AREA_HD_FIRST_TRACK    3

/* ------------------------------------------------------------------ */
/* Campos de Control/ADR de uma track                                  */
/* ------------------------------------------------------------------ */
#define GD_CTRL_AUDIO  0x00
#define GD_CTRL_DATA   0x04
#define GD_ADR_Q       0x01   /* o subcode so existe no canal Q */

/* ------------------------------------------------------------------ */
/* Layout do sector de 2352 bytes                                      */
/* ------------------------------------------------------------------ */
/*
 *   SYNC(12) HEAD(4) [data 2048] EDC(4) SPACE(8) ECC(276)  = 2352
 *                            ^ offset 0x10 = Mode 1
 *                              offset 0x18 = Mode 2
 *
 * O byte 15 (dentro do HEADER) distingue Mode 1 (== 1) de Mode 2.
 */
#define GD_SECTOR_2352          2352u
#define GD_SECTOR_2048          2048u
#define GD_SECTOR_2336          2336u
#define GD_SECTOR_2448          2448u   /* 2352 + 96 de subcode */
#define GD_SUBCODE_96            96u
#define GD_MODE1_DATA_OFFSET     0x10
#define GD_MODE2_DATA_OFFSET     0x18
#define GD_MODE_BYTE_INDEX       15      /* dentro do sector              */

#endif /* GD_FORMAT_H */
