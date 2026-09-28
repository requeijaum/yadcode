/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sniffer.h"

/* ------------------------------------------------------------------ */
/* Decodificacao dos pinos                                             */
/* ------------------------------------------------------------------ */

sn_reg_t sn_decode(const uint32_t pins, int *is_write, uint16_t *data)
{
    unsigned da = (unsigned)((pins >> SN_DA(0)) & 7u);

    /* /CS0, /CS1, /RD e /WR sao todos activo-baixo: 0 = activo.
     * A PIO le os pinos tal como estao, por isso e' aqui que se
     * inverte. */
    unsigned cs0 = (~(pins >> SN_CS0)) & 1u;   /* /CS0 activo */
    unsigned cs1 = (~(pins >> SN_CS1)) & 1u;   /* /CS1 activo */
    unsigned rda = (~(pins >> SN_RD))  & 1u;   /* /RD activo  */
    unsigned wra = (~(pins >> SN_WR))  & 1u;   /* /WR ativo   */

    if (is_write) *is_write = 0;
    if (data)     *data = 0;

    if (!rda && !wra) return SN_REG_NONE;   /* nenhum strobe activo */
    if ( rda &&  wra) return SN_REG_NONE;   /* os dois activos: ruido */

    if (is_write) *is_write = (int)wra;
    if (data)     *data = (uint16_t)(pins & 0xffffu);

    if (rda) {
        /* Leitura: o host puxa /RD baixo. */
        if (!cs0 && cs1) return (da == 0x6) ? SN_REG_ALTSTATUS : SN_REG_DEVCONTROL;
        if (cs0 && !cs1) {
            switch (da) {
            case 0x0: return SN_REG_DATA;
            case 0x1: return SN_REG_ERROR;
            case 0x2: return SN_REG_INTREASON;
            case 0x3: return SN_REG_SECTORNR;
            case 0x4: return SN_REG_BYTECOUNTL;
            case 0x5: return SN_REG_BYTECOUNTH;
            case 0x6: return SN_REG_DRIVESEL;
            case 0x7: return SN_REG_STATUS;
            default:  return SN_REG_NONE;
            }
        }
    } else {
        /* Escrita: o host empurra /WR baixo. ERROR e' FEATURES na
         * escrita e STATUS e' COMMAND, tal como na Tabela 3.1 da spec. */
        if (!cs0 && cs1) return SN_REG_DEVCONTROL;
        if (cs0 && !cs1) {
            switch (da) {
            case 0x0: return SN_REG_DATA;
            case 0x1: return SN_REG_ERROR;   /* ERROR na escrita = FEATURES */
            case 0x4: return SN_REG_BYTECOUNTL;
            case 0x5: return SN_REG_BYTECOUNTH;
            case 0x6: return SN_REG_DRIVESEL;
            case 0x7: return SN_REG_STATUS;  /* STATUS na escrita = COMMAND */
            default:  return SN_REG_NONE;
            }
        }
    }
    return SN_REG_NONE;
}

const char *sn_reg_name(sn_reg_t r)
{
    switch (r) {
    case SN_REG_ALTSTATUS:  return "ALTSTAT";
    case SN_REG_DEVCONTROL: return "DEVCTL ";
    case SN_REG_DATA:       return "DATA   ";
    case SN_REG_ERROR:      return "ERROR  ";
    case SN_REG_INTREASON:  return "INTREAS";
    case SN_REG_SECTORNR:   return "SECNR  ";
    case SN_REG_BYTECOUNTL: return "BCNTL ";
    case SN_REG_BYTECOUNTH: return "BCNTH ";
    case SN_REG_DRIVESEL:   return "DRVSEL ";
    case SN_REG_STATUS:     return "STATUS ";
    default:                return "??????"; }
}

/* ------------------------------------------------------------------ */
/* Captura: buffer circular com um unico indice                       */
/* ------------------------------------------------------------------ */

void sn_init(sn_capture_t *c)
{
    memset(c, 0, sizeof *c);
}

void sn_push(sn_capture_t *c, uint32_t pins)
{
    /*
     * NOTA: nao ha deduplicacao aqui, e' proposito.
     *
     * A PIO ja so' emite quando o estado dos pinos muda, e por isso e' o
     * unico filtro legitimo. Deduplicar tambem no software seria
     * errado: duas escritas de 0x0000 no registo DATA sao transaccoes
     * distintas mesmo que produzam o mesmo estado de pinos, e um filtro
     * por igualdade apagaria uma delas. Seria particularmente mau num
     * packet de 12 bytes, onde so' a primeira e a ultima palavras sao
     * nao-nulas.
     *
     * A PIO mantem-se a unica fonte de "isto e' um evento".
     */
    c->ring[c->head & (SN_RING_WORDS - 1)] = pins;
    c->head++;
    c->total++;
    /*
     * Quando o buffer enche, conta-se a perda e avanca-se a leitura
     * para o mais antigo que ainda existe. Nao se pode simplesmente
     * parar: a partir dai o trace diria so' "ring cheio" e a captura
     * morreria, que e' pior do que perder a janela mais antiga.
     */
    if (c->head - c->drained > SN_RING_WORDS) {
        c->dropped += c->head - c->drained - SN_RING_WORDS;
        c->drained = c->head - SN_RING_WORDS;
    }
}

uint32_t sn_drain(sn_capture_t *c, uint32_t *out, uint32_t max)
{
    uint32_t avail = c->head - c->drained;
    uint32_t i;

    if (avail > SN_RING_WORDS) { c->dropped += avail - SN_RING_WORDS; avail = SN_RING_WORDS; }
    if (avail > max) avail = max;

    for (i = 0; i < avail; i++)
        out[i] = c->ring[(c->drained + i) & (SN_RING_WORDS - 1)];

    c->drained += avail;
    return avail;
}

/* ------------------------------------------------------------------ */
/* Analise: reconstruir transaccoes e responder as perguntas          */
/* ------------------------------------------------------------------ */

static void add(sn_report_t *r, sn_question_t q, const char *question,
                const char *answer, int confident, const char *evidence)
{
    sn_finding_t *f;
    if (r->n >= SN_MAX_FINDINGS) return;
    f = &r->f[ r->n++ ];
    f->q = q;
    f->question = question;
    f->evidence = evidence;
    f->confident = confident;
    /*
     * `answer` e' copiado, nao guardado como ponteiro: os chamadores
     * formatam a resposta num buffer local e devolvem logo a seguir.
     * Guardar o ponteiro dava leitura de memoria expirada.
     */
    if (!answer) answer = "-";
    snprintf(f->answer, sizeof f->answer, "%s", answer);
}

/*
 * Cada palavra que decodifica e' uma transaccao completa: a PIO so'
 * emite no instante do strobe activo, que e' quando o dado e' valido.
 * Nao se agrupam nem se colapsam palavras iguais: `find_packet_at`
 * precisa de as contar uma a uma, e duas escritas de 0x0000 no DATA
 * sao transaccoes distintas.
 *
 * A lista e' alocada com o numero de eventos (transaccoes <= eventos)
 * e libertada no fim de `sn_analyse`. O `sn_analyse` nunca corre no
 * firmware, so' no PC e nos testes, por isso o malloc e' aceitavel.
 */
typedef struct {
    sn_reg_t reg;
    int      is_write;
    uint16_t data;
} sn_xfer_t;

/*
 * Um evento por acesso e' tudo o que este modo da: a PIO so' emite
 * quando o estado muda, e cada acesso produz o evento do strobe
 * activo, que e' quando o dado e' valido.
 *
 * Logo, cada palavra que decodifica e' uma transaccao completa. Nao ha
 * necessidade de agrupar.
 */
static void collect(const uint32_t *w, uint32_t n, sn_xfer_t **out, uint32_t *nout)
{
    sn_xfer_t *list = malloc((size_t)n * sizeof *list);
    uint32_t i, m = 0;

    if (!list) { *out = NULL; *nout = 0; return; }

    for (i = 0; i < n; i++) {
        int isw = 0;
        uint16_t data = 0;
        sn_reg_t reg = sn_decode(w[i], &isw, &data);

        if (reg == SN_REG_NONE) continue;

        /*
         * Uma entrada por acesso. Nao se colapsam palavras de dados
         * iguais: `find_packet_at` precisa de as contar uma a uma para
         * saber quantas o host enviou, e e' esse numero que responde a
         * pergunta A (0x71: 6 ou 1012 bytes).
         */
        list[m].reg      = reg;
        list[m].is_write = isw;
        list[m].data     = data;
        m++;
    }
    *out = list;
    *nout = m;
}

/*
 * Procura o PRIMEIRO packet completo (0xA0 seguido de 6 escritas em
 * DATA) a partir de `from`. Devolve o indice do COMMAND em `pkt_at`
 * e o primeiro byte do packet em `cmd`.
 *
 * O packet tem de vir sem interrupcoes: qualquer acesso que nao seja
 * escrita em DATA entre o COMMAND e a sexta palavra invalida-o. Numa
 * captura real o host escreve as 6 palavras de rajada, por isso isto
 * nao rejeita nada legitimo; rejeita e' ruido e capturas truncadas.
 */
static int find_packet_at(const sn_xfer_t *x, uint32_t n, uint32_t from,
                          uint32_t *pkt_at, uint8_t *cmd)
{
    uint32_t i;
    for (i = from; i < n; i++) {
        if (x[i].is_write && x[i].reg == SN_REG_STATUS && x[i].data == GD_CMD_PACKET) {
            uint8_t p[GD_PKT_SIZE];
            uint32_t got = 0, j;
            for (j = i + 1; j < n && got < GD_PKT_WORDS; j++) {
                if (x[j].is_write && x[j].reg == SN_REG_DATA) {
                    p[got * 2]     = (uint8_t)(x[j].data >> 8);
                    p[got * 2 + 1] = (uint8_t)(x[j].data & 0xff);
                    got++;
                } else {
                    break;   /* outra coisa no meio: nao e' um packet */
                }
            }
            if (got == GD_PKT_WORDS) {
                *pkt_at = i;
                *cmd = p[0];
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Mede o tamanho da resposta a um packet cujo COMMAND esta em `at`.
 *
 * A medicao directa e' contar as palavras lidas no registo DATA entre
 * este COMMAND e o seguinte. E' o que o host fez, medido, sem depender
 * de o Byte Count ter sido escrito antes ou depois. Limitar ao
 * intervalo ate' ao proximo COMMAND e' essencial: sem isso, as leituras
 * de um packet seguinte contaminavam a medida deste.
 *
 * Se nao houver leituras DATA no intervalo, a resposta foi por DMA: os
 * dados vao pelo canal do SH4 e este modo nao os ve. Nesse caso mede-se
 * o Byte Count escrito no mesmo intervalo, que e' o que o host usou
 * para saber o tamanho.
 */
static uint32_t response_bytes(const sn_xfer_t *x, uint32_t n, uint32_t at,
                               int *measured)
{
    uint32_t words = 0, i, end = n;
    uint32_t bc = 0;
    int have_lo = 0, have_hi = 0;
    uint32_t lo = 0, hi = 0;

    for (i = at + 1; i < n; i++) {
        if (x[i].is_write && x[i].reg == SN_REG_STATUS) { end = i; break; }
    }
    for (i = at + 1; i < end; i++) {
        if (!x[i].is_write && x[i].reg == SN_REG_DATA) words++;
    }
    if (words) { *measured = 1; return words * 2u; }

    /* Sem leituras DATA: veio por DMA. Mede o Byte Count do intervalo. */
    for (i = at + 1; i < end; i++) {
        if (!x[i].is_write) continue;
        if (x[i].reg == SN_REG_BYTECOUNTL) { lo = x[i].data; have_lo = 1; break; }
    }
    for (i = at + 1; i < end; i++) {
        if (!x[i].is_write) continue;
        if (x[i].reg == SN_REG_BYTECOUNTH) { hi = x[i].data; have_hi = 1; break; }
    }
    if (have_lo && have_hi) {
        bc = (hi << 8) | lo;
        *measured = 1;
        return bc;
    }

    *measured = 0;
    return 0;
}

/* Devolve o indice do proximo COMMAND (escrita em STATUS) a partir de
 * `from`, ou `n` se nao houver. */
static uint32_t next_command(const sn_xfer_t *x, uint32_t n, uint32_t from)
{
    uint32_t i;
    for (i = from; i < n; i++)
        if (x[i].is_write && x[i].reg == SN_REG_STATUS) return i;
    return n;
}

void sn_analyse(const uint32_t *words, uint32_t n, sn_report_t *out)
{
    sn_xfer_t *x;
    uint32_t m = 0, i;
    int saw_dma = 0;
    int saw_71 = 0, saw_a1 = 0;

    memset(out, 0, sizeof *out);
    if (n == 0 || !words) {
        add(out, SN_Q_71_LEN, "0x71 responde com 6 bytes ou 1012?",
            "sem dados", 0, "captura vazia");
        return;
    }
    collect(words, n, &x, &m);
    if (!x || m == 0) {
        free(x);
        add(out, SN_Q_71_LEN, "0x71 responde com 6 bytes ou 1012?",
            "sem dados", 0, "nenhuma transaccao descodificavel");
        return;
    }

    /* ---- A: tamanho da resposta ao 0x71 ------------------------- */
    for (i = 0; i < m; i++) {
        uint8_t cmd;
        uint32_t pkt_at = 0;
        int measured = 0;
        uint32_t bytes;
        if (!find_packet_at(x, m, i, &pkt_at, &cmd)) break;
        if (cmd != GD_SPI_CMD71) continue;

        bytes = response_bytes(x, m, pkt_at, &measured);
        if (!measured) {
            add(out, SN_Q_71_LEN, "0x71 responde com 6 bytes ou 1012?",
                "a resposta foi por DMA e este modo nao ve o bus de dados",
                0, "o Byte Count e' a unica pista, e o trace nao a liga ao packet");
            saw_71 = 1;
            break;
        }
        {
            char ans[80];
            snprintf(ans, sizeof ans, "%u bytes", (unsigned)bytes);
            add(out, SN_Q_71_LEN, "0x71 responde com 6 bytes ou 1012?",
                (bytes == 6 || bytes == 1012) ? ans : "outro valor",
                1, "medido nas palavras DATA lidas depois do packet");
            saw_71 = 1;
        }

        break;
    }
    if (!saw_71) {
        add(out, SN_Q_71_LEN, "0x71 responde com 6 bytes ou 1012?",
            "sem dados", 0, "o host nao emitiu 0x71 nesta captura");
    }

    /* ---- B: o 0xA1 aborta e devolve 80 bytes? --------------------- */
    for (i = 0; i < m; i++) {
        if (x[i].is_write && x[i].reg == SN_REG_STATUS && x[i].data == GD_CMD_IDDEV) {
            /* A leitura seguinte de ERROR diz se houve abort. Na
             * spec (gd_spec.h) o Error traz a Sense Key no nibble alto
             * e o bit ABRT (0x04) quando o comando foi abortado. */
            uint32_t j;
            for (j = i + 1; j < m && j < i + 64; j++) {
                if (!x[j].is_write && x[j].reg == SN_REG_ERROR) {
                    char ans[80];
                    snprintf(ans, sizeof ans,
                             "Error = 0x%02x (sense key %u)",
                             (unsigned)x[j].data, (unsigned)(x[j].data >> 4));
                    add(out, SN_Q_A1_LEN, "0xA1 aborta e devolve 80 bytes?",
                        (x[j].data & GD_ERR_ABRT) ? ans : "sem abort",
                        1, "o registo Error lido depois do comando");
                    saw_a1 = 1;
                    break;
                }
            }
            if (!saw_a1) {
                /* Sem leitura de Error: conta os dados lidos ate ao
                 * comando seguinte (ou ao fim da captura, se o 0xA1
                 * for o ultimo comando). */
                uint32_t k, nwords = 0, next = next_command(x, m, i + 1);
                for (k = i + 1; k < next; k++)
                    if (!x[k].is_write && x[k].reg == SN_REG_DATA) nwords++;
                if (nwords) {
                    {
                        char ans[64];
                        snprintf(ans, sizeof ans, "%u bytes de dados", (unsigned)(nwords * 2u));
                        add(out, SN_Q_A1_LEN, "0xA1 aborta e devolve 80 bytes?",
                            ans, 1, "palavras DATA lidas entre o comando e o seguinte");
                        saw_a1 = 1;
                    }
                }
            }
            break;
        }
    }
    if (!saw_a1) {
        add(out, SN_Q_A1_LEN, "0xA1 aborta e devolve 80 bytes?",
            "sem dados", 0, "o host nao emitiu 0xA1 nesta captura");
    }

    /* ---- C: o host pediu DMA? ----------------------------------- */
    /*
     * So' conta um FEATURES com bit 0 cujo comando seguinte seja um
     * PACKET (0xA0). Um SET FEATURES (0xEF) com Features = 0x03 tambem
     * tem o bit 0 aceso e nao e' um pedido de DMA: e' a programacao do
     * modo de transferencia. Sem esta guarda, qualquer programacao de
     * modo dava um falso positivo.
     */
    for (i = 0; i < m; i++) {
        if (x[i].is_write && x[i].reg == SN_REG_ERROR && (x[i].data & GD_FEAT_DMA)) {
            uint32_t nc = next_command(x, m, i + 1);
            if (nc < m && x[nc].data == GD_CMD_PACKET) { saw_dma = 1; break; }
        }
    }
    add(out, SN_Q_DMA, "o host real usa DMA?",
        saw_dma ? "SIM: Features bit 0 foi posto a 1" : "NAO, nesta captura",
        1, saw_dma ? "escrita em FEATURES com bit 0 aceso"
                   : "nenhuma escrita em FEATURES com bit 0 aceso");

    /* ---- E: o lead-out no GET_TOC ------------------------------- */
    {
        int saw_toc = 0;
        for (i = 0; i < m; i++) {
            uint8_t cmd;
            uint32_t pkt_at = 0;
            if (!find_packet_at(x, m, i, &pkt_at, &cmd)) break;
            if (cmd == GD_SPI_GET_TOC) { saw_toc = 1; break; }
        }
        add(out, SN_Q_LEADOUT, "o lead-out do GET_TOC e' sempre 549300?",
            "precisa do modo de captura de dados", 0,
            saw_toc ? "o GET_TOC responde com 408 bytes, que este modo nao guarda"
                    : "o host nao pediu GET_TOC nesta captura");
    }

    /* ---- F: variante do CRC ------------------------------------- */
    add(out, SN_Q_CRC, "CRC do subcode: XMODEM ou a variante complementada?",
        "precisa do modo de captura de dados", 0,
        "o Q-subcode so aparece nos dados de GET_SCD, que este modo nao guarda");

    free(x);
}

void sn_report_print(const sn_report_t *r)
{
    int i;
    printf("\n=== sniffer: o que a captura permite responder ===\n\n");
    for (i = 0; i < r->n; i++) {
        printf("[%c] %s\n", r->f[i].confident ? 'x' : ' ', r->f[i].question);
        printf("      resposta: %s\n", r->f[i].answer);
        if (r->f[i].evidence) printf("      porque:   %s\n", r->f[i].evidence);
        printf("\n");
    }
    printf("legenda: [x] respondida com a captura; [ ] nao respondida (ver o motivo)\n");
}
