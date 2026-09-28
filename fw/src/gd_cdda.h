/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_cdda.h - Camada L4: reproducao de CD-DA e geracao de subcode.
 *
 * O subcode tem de reflectir a posicao de audio corrente, nao ser
 * calculado sob demanda no instante do pedido. A spec (secao 8.2,
 * comando CD_SCD) e explicita:
 *
 *   "Because the subcode information is updated approximately every
 *    13.3 ms, the data may be wiped out unless the subcode is read out as
 *    fast as possible when subcode is used."
 *
 * Ou seja: a posicao avanca com o tempo, independentemente de o host
 * perguntar. Um subcode calculado no momento do GET_SCD daria sempre a
 * posicao inicial e o audio apareceria parado.
 *
 * FORMATOS (spec secao 8.2, Tabela 6.1):
 *   0  raw   -> 100 bytes, Q em BCD + CRC, P por expansao
 *   1  Q     ->  14 bytes, Q em BINARIO, sem CRC
 *   2  UPC   ->  24 bytes
 *   3  ISRC  ->  16 bytes
 *
 * O contraste BCD/binario entre os formatos 0 e 1 e' real e e' a coisa
 * mais facil de errar aqui: e' literalmente o mesmo campo, codificado de
 * duas maneiras diferentes.
 */
#ifndef GD_CDDA_H
#define GD_CDDA_H

#include <stdint.h>
#include "gd_spec.h"
#include <stddef.h>

/*
 * gd_taskfile.h inclui este ficheiro, porque a estrutura struct gd_device
 * contem um gd_cdda_t. Por isso aqui so se declara o struct - incluir
 * gd_taskfile.h aqui seria um ciclo.
 */
struct gd_device;

/* Periodo de refresh do subcode, em microssegundos. 13.3 ms. */
#define GD_SCD_REFRESH_US 13300

/* Tamanhos de resposta, do campo "DATA Length" da spec. */
#define GD_SCD_RAW_LEN  100
#define GD_SCD_Q_LEN    14
#define GD_SCD_UPC_LEN  24
#define GD_SCD_ISRC_LEN 16

/* Estado da reproducao de audio. Mapeado em GD_AUD_* (spec secao 8.2). */
typedef enum {
    GD_CDDA_NO_INFO = 0,
    GD_CDDA_PLAYING,
    GD_CDDA_PAUSED,
    GD_CDDA_TERMINATED,
    GD_CDDA_ABNORMAL
} gd_cdda_status_t;

typedef struct {
    gd_cdda_status_t status;
    uint32_t curr_fad;      /* posicao de audio corrente */
    uint32_t start_fad;
    uint32_t end_fad;
    uint8_t  repeats;       /* repeat count, Byte 6 & 0x0F */
    uint8_t  param_type;    /* 1 = FAD, 2 = MSF */
    uint8_t  last_cmd;      /* comando que fixou a posicao */
    uint32_t last_tick_us;  /* ultima actualizacao */
} gd_cdda_t;

/* CRC-16/XMODEM: polinomio 0x1021, init 0x0000, sem reflect, xorout
 * 0xFFFF. E' o algoritmo que o GD-ROM real usa sobre os 10 primeiros
 * bytes do Q-subcode. */
uint16_t gd_cdda_crc16(const uint8_t *data, size_t len);

gd_cdda_t *gd_cdda_get(struct gd_device *dev);

void gd_cdda_reset(struct gd_device *dev);

/* Avanca a posicao conforme o tempo decorrido. Chamar periodicamente
 * (a cada ~1 ms ou mais). Idempotente dentro de um periodo. */
void gd_cdda_tick(struct gd_device *dev, uint32_t now_us);

/* Comandos de transporte. Devolvem 0 em sucesso, -1 sem disco. */
int gd_cdda_play(struct gd_device *dev, const uint8_t *pkt);
int gd_cdda_seek(struct gd_device *dev, const uint8_t *pkt);
int gd_cdda_scan(struct gd_device *dev, const uint8_t *pkt);

/*
 * Gera a resposta de GET_SCD. Devolve o numero de bytes escritos em
 * `out` (que tem de ter pelo menos GD_SCD_RAW_LEN bytes).
 */
uint32_t gd_cdda_get_subcode(struct gd_device *dev, uint8_t format,
                             uint8_t *out, uint32_t cap);

#endif /* GD_CDDA_H */
