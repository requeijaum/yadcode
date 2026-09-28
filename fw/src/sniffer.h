/* SPDX-License-Identifier: Apache-2.0 */
/*
 * sniffer.h - Captura e analise do trafego do barramento G1.
 *
 * PARA QUE SERVE
 * --------------
 * Ha sete questoes em aberto (doc 13 §7) e **cinco so se respondem com
 * um Dreamcast e um GD-ROM reais**:
 *
 *   A  0x71: 6 bytes ou 1012?
 *   B  0xA1: abort + blob de 80 bytes?
 *   C  o host real usa DMA?
 *   E  o lead-out e' sempre 549300?
 *   F  o CRC do subcode e' XMODEM ou a variante complementada?
 *
 * A questao D (GET_SCD formatos 2/3) resolve-se testando jogos, e a G
 * (GetBaseFAD) com um dump da BIOS: nenhuma precisa deste sniffer, e
 * por isso as letras saltam de C para E.
 *
 * Nenhuma se responde lendo documentacao, porque nenhuma fonte
 * permissiva as documenta. Todas se respondem observando um drive.
 *
 * COMO
 * ----
 * A PIO so' emite um evento quando o estado dos sinais muda (ver
 * g1_sniff.pio). Como o host esta parado a maior parte do tempo, isso
 * reduz o caudal em ordens de grandeza sem perder uma unica
 * transacao. A captura vai para um buffer circular em RAM e o host
 * despeja por UART.
 *
 * O buffer circular e' deliberado: a taxa de eventos de uma
 * transferencia de dados (um ciclo t0 de 180 ns por palavra) e'
 * muito superior ao que a UART aguenta, e nao ha memoria para guardar
 * tudo. Guarda-se uma janela e prende-se o que vier a seguir.
 */
#ifndef SNIFFER_H
#define SNIFFER_H

#include <stdint.h>
#include "gd_spec.h"

/* ------------------------------------------------------------------ */
/* Pinout do sniffer                                                   */
/* ------------------------------------------------------------------ */
/* Nao e' o pinout final da placa. E' o pinout do sniffer, que liga
 * por catrao, e e' propositadamente contiguo porque o PIO so' ve uma
 * janela de 32 pinos. Ver doc 10 §4. */
#define SN_DD(n)     (n)          /* GPIO 0..15: DD0..DD15 */
#define SN_DA(n)     (16 + (n))   /* GPIO 16..18: DA0..DA2  */
#define SN_CS0       19
#define SN_CS1       20
#define SN_RD        21
#define SN_WR        22
#define SN_IORDY     23
#define SN_INTRQ     24
#define SN_DMARQ     25
#define SN_DMACK     26
#define SN_RST       27
#define SN_FIRST     0
#define SN_LAST      27

/* Decodificacao do endereco de registo, a partir dos pinos. */
typedef enum {
    SN_REG_ALTSTATUS = 0,   /* CS1 asserted, DA = 1 1 0 */
    SN_REG_DEVCONTROL,
    SN_REG_DATA,
    SN_REG_ERROR,           /* == FEATURES na escrita */
    SN_REG_INTREASON,
    SN_REG_SECTORNR,
    SN_REG_BYTECOUNTL,
    SN_REG_BYTECOUNTH,
    SN_REG_DRIVESEL,
    SN_REG_STATUS,          /* == COMMAND na escrita */
    SN_REG_NONE
} sn_reg_t;

/* Nome do registo, para o dump legivel. */
const char *sn_reg_name(sn_reg_t r);

/* A partir do estado capturado dos pinos. `data` e' o valor no bus
 * quando houve uma transaccao. */
sn_reg_t sn_decode(const uint32_t pins, int *is_write, uint16_t *data);

/* ------------------------------------------------------------------ */
/* Captura                                                             */
/* ------------------------------------------------------------------ */
#define SN_RING_WORDS   8192    /* 32 KB de RAM                              */
#define SN_BATCH        64      /* palavras por interrupcao                  */

typedef struct {
    uint32_t ring[SN_RING_WORDS];
    uint32_t head;          /* indice de escrita                            */
    uint32_t drained;       /* indice de leitura                            */
    uint32_t dropped;       /* eventos perdidos por falta de espaco        */
    uint32_t total;         /* eventos capturados                          */
} sn_capture_t;

void sn_init(sn_capture_t *c);
void sn_push(sn_capture_t *c, uint32_t pins);
/* Move ate `max` palavras para `out`, devolvendo quantas. */
uint32_t sn_drain(sn_capture_t *c, uint32_t *out, uint32_t max);

/* ------------------------------------------------------------------ */
/* Analise                                                             */
/* ------------------------------------------------------------------ */
#define SN_MAX_FINDINGS 32

typedef enum {
    SN_Q_71_LEN = 0,        /* A: tamanho da resposta ao 0x71             */
    SN_Q_A1_LEN,            /* B: tamanho e abort da resposta ao 0xA1     */
    SN_Q_DMA,               /* C: o host alguma vez pediu DMA?            */
    SN_Q_LEADOUT,           /* E: lead-out reportado no GET_TOC           */
    SN_Q_CRC,               /* F: variante do CRC do subcode              */
    SN_Q__COUNT
} sn_question_t;

typedef struct {
    sn_question_t q;
    const char   *question;     /* texto literal, valido para sempre      */
    char          answer[80];   /* copiado: o chamador usa buffer local  */
    int           confident;    /* 1 se a captura permite responder       */
    const char   *evidence;     /* porque, em uma linha                   */
} sn_finding_t;

typedef struct {
    sn_finding_t f[SN_MAX_FINDINGS];
    int          n;
} sn_report_t;

/* Analisa uma captura e responde ao que for possivel responder. */
void sn_analyse(const uint32_t *words, uint32_t n, sn_report_t *out);

/* Imprime o relatorio. */
void sn_report_print(const sn_report_t *r);

#endif /* SNIFFER_H */
