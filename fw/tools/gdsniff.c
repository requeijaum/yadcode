/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gdsniff - analisador de capturas do sniffer, para o PC.
 *
 * LEITURA DE UM FICHEIRO
 * ----------------------
 * A placa despeja a captura pela UART em texto, linha a linha:
 *
 *   # commentarios e estado inicial sao ignorados
 *   <16 digitos hex> <contador>
 *
 * onde os 16 digitos sao o estado dos 28 pinos. O contador e' o numero
 * de ordem do evento, util para confirmar que nao faltou nenhum.
 *
 * FORMATO
 * -------
 * Texto e' deliberado. Um ficheiro binario seria mais compacto, mas
 * quem vai ler isto e' uma pessoa a depurar um cabo, e `less` e
 * `grep` resolvem o caso. A taxa da UART e' lenta o suficiente para
 * que o volume nao seja um problema.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "sniffer.h"
#include "gd_spec.h"

/* ------------------------------------------------------------------ */
/* Leitura                                                             */
/* ------------------------------------------------------------------ */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Le um ficheiro inteiro em memoria. Devolve NULL se nao conseguir. */
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);

    buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = '\0';
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

/* Converte o texto em palavras de 32 bits. */
static uint32_t *parse(char *text, size_t *nwords, unsigned *lines_bad,
                       unsigned *gaps, unsigned long *last_counter)
{
    uint32_t *w;
    char *p = text;
    size_t cap = 1024, n = 0;

    w = malloc(cap * sizeof *w);
    if (!w) { *nwords = 0; return NULL; }
    *lines_bad = 0;
    *gaps = 0;
    *last_counter = 0;

    while (*p) {
        uint32_t v = 0;
        int i, ok = 1;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '\0') break;
        if (*p == '#') {                 /* comentario ate fim de linha */
            while (*p && *p != '\n') p++;
            continue;
        }

        for (i = 0; i < 16; i++) {
            int d = hexval((unsigned char)p[i]);
            if (d < 0) { ok = 0; break; }
            v = (v << 4) | (uint32_t)d;
        }
        if (!ok) {
            (*lines_bad)++;
            /* Salta a linha para nao ficar em ciclo. */
            while (*p && *p != '\n') p++;
            continue;
        }
        p += 16;

        /*
         * O contador e' opcional, mas quando existe serve para detectar
         * perda: se saltar, a placa perdeu eventos entre dois polls.
         * Nao e' um aviso: sem ele nao ha forma de distinguir "a
         * placa perdeu" de "o host nao gerou nada".
         */
        while (*p == ' ' || *p == '\t') p++;
        if (*p >= '0' && *p <= '9') {
            unsigned long ctr = 0;
            while (*p >= '0' && *p <= '9') {
                ctr = ctr * 10u + (unsigned long)(*p - '0');
                p++;
            }
            if (n > 0 && ctr > *last_counter + 1u)
                *gaps += (unsigned)(ctr - *last_counter - 1u);
            *last_counter = ctr;
        }

        if (n == cap) {
            uint32_t *nw = realloc(w, cap * 2 * sizeof *w);
            if (!nw) { *nwords = n; return w; }   /* buffer antigo fica com o caller */
            w = nw; cap *= 2;
        }
        w[n++] = v;
    }

    *nwords = n;
    return w;
}

/* ------------------------------------------------------------------ */
/* Relatorio                                                           */
/* ------------------------------------------------------------------ */

static void print_transfers(const uint32_t *w, size_t n)
{
    uint32_t i, shown = 0;

    printf("\n=== transaccoes ===\n");
    printf("%-6s %-4s %-8s %6s %s\n", "evento", "dir", "registo", "dados", "notas");

    for (i = 0; i < n && shown < 80; i++) {
        int isw = 0;
        uint16_t d = 0;
        sn_reg_t r = sn_decode(w[i], &isw, &d);
        char nota[64] = "";

        if (r == SN_REG_NONE) continue;

        if (isw && r == SN_REG_STATUS) {
            switch (d) {
            case GD_CMD_PACKET:    strcpy(nota, "inicio de packet"); break;
            case GD_CMD_IDDEV:     strcpy(nota, "IDENTIFY DEVICE"); break;
            case GD_CMD_SETFEATURE: strcpy(nota, "SET FEATURES"); break;
            default:
                if (d >= 0xA0 && d <= 0xAF) snprintf(nota, sizeof nota, "SPI 0x%02X", d);
                break;
            }
        }
        if (isw && r == SN_REG_ERROR && (d & 0x01)) strcpy(nota, "FEATURES: DMA pedido");
        if (!isw && r == SN_REG_ERROR) snprintf(nota, sizeof nota, "sense key %u", d >> 4);

        printf("%-6u %-4s %-8s 0x%04x %s\n",
               i, isw ? "WR" : "RD", sn_reg_name(r), d, nota);
        shown++;
    }
    if (shown == 0) printf("(nenhuma transaccao descodificavel)\n");
    if (i < n) printf("... (%zu eventos omitidos)\n", n - i);
}

static void usage(const char *me)
{
    printf("uso: %s <captura.txt>\n\n", me);
    printf("  Analisa uma captura do sniffer G1 e responde ao que for\n");
    printf("  possivel responder a partir dela.\n\n");
    printf("  As perguntas respondidas (doc 13 §7):\n");
    printf("    A  0x71 responde com 6 bytes ou 1012?\n");
    printf("    B  0xA1 aborta e devolve 80 bytes?\n");
    printf("    C  o host real usa DMA?\n");
    printf("    E  o lead-out e' sempre 549300?\n");
    printf("    F  o CRC do subcode e' XMODEM ou a variante complementada?\n");
}

int main(int argc, char **argv)
{
    size_t len;
    char *text;
    uint32_t *w;
    size_t n = 0;
    unsigned bad = 0, gaps = 0;
    unsigned long last = 0;
    sn_report_t rep;

    if (argc != 2) { usage(argv[0]); return 2; }

    text = slurp(argv[1], &len);
    if (!text) { fprintf(stderr, "%s: nao consegui ler\n", argv[1]); return 1; }

    w = parse(text, &n, &bad, &gaps, &last);
    free(text);

    if (bad) fprintf(stderr, "aviso: %u linha(s) ignorada(s) por formato\n", bad);
    if (n == 0 || w == NULL) {
        fprintf(stderr, "%s: nenhum evento\n", argv[1]);
        free(w);
        return 1;
    }
    if (n > 0xffffffffu) {
        fprintf(stderr, "%s: captura grande demais (%zu eventos)\n", argv[1], n);
        free(w);
        return 1;
    }

    printf("=== captura ===\n");
    printf("ficheiro:   %s\n", argv[1]);
    printf("eventos:    %zu\n", n);
    if (gaps) {
        printf("PERDIDOS:   %u evento(s) - o contador saltou. A placa nao\n"
               "            acompanhou o bus; a captura tem buracos.\n", gaps);
    }

    print_transfers(w, n);
    sn_analyse(w, (uint32_t)n, &rep);
    sn_report_print(&rep);

    printf("\n");
    printf("O que FAZER com este relatorio\n");
    printf("--------------------------------\n");
    printf("As perguntas marcadas [ ] precisam de uma captura diferente:\n");
    printf("o lead-out e o CRC vivem nos 408 bytes de resposta do\n");
    printf("GET_TOC e do GET_SCD, e este modo so' guarda transaccoes de\n");
    printf("registo. Para os ver e' preciso o modo de captura de dados,\n");
    printf("que regista o bus DD em vez dos strobes.\n");

    free(w);
    return 0;
}
