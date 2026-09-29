/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_crc.c - Verifica a tabela e o algoritmo do CRC do subcode.
 *
 * Casos 97+. A tabela vem do Katana SDK (ver gd_crctbl.h); o algoritmo
 * vem de leitura do binario (ver gd_crc.c). O que este teste prova e' a
 * COERENCIA INTERNA: que a tabela e' a geracao canonica de 0x1021, e que
 * o algoritmo tem as propriedades estruturais de um CRC. O que NAO prova
 * e' que o GD-ROM real faca isto — para isso, docs/16 §8.
 */
#include <stdio.h>
#include <string.h>
#include "gd_crc.h"
#include "gd_spec.h"

static int g_case, checks, fails;
#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)
#define CHECK(cond, ...) do {                                           \
        checks++;                                                        \
        if (cond) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
        else      { fails++; printf("  FAIL "); printf(__VA_ARGS__);     \
                    printf("   [%s:%d]\n", __FILE__, __LINE__); }        \
    } while (0)

/* ------------------------------------------------------------------ */

/* A tabela extraida tem de ser a geracao canonica de 0x1021. */
static void test_tabela(void)
{
    unsigned i, bad = 0;
    uint16_t t;
    for (i = 0; i < 256; i++) {
        t = 0;
        t = (uint16_t)((uint16_t)(i << 8));
        for (unsigned b = 0; b < 8; b++) {
            t = (t & 0x8000u) ? (uint16_t)((t << 1) ^ 0x1021u)
                               : (uint16_t)(t << 1);
        }
        if (t != gd_crctbl_sega[i]) bad++;
    }
    CHECK(bad == 0, "tabela = CRC-CCITT 0x1021 MSB-first (%u divergentes de 256)", bad);
    CHECK(gd_crctbl_sega[0] == 0x0000, "crctbl[0] = 0x%04x", gd_crctbl_sega[0]);
    CHECK(gd_crctbl_sega[1] == 0x1021, "crctbl[1] = 0x%04x (o polinomio)", gd_crctbl_sega[1]);
}

/* O que o desensamblado obriga: init 0xFFFF e NOT final. */
static void test_init_e_not(void)
{
    static const uint8_t zero[1] = { 0x00 };
    /* acc inicial 0xFFFF, um passo: (0xFFFF<<8 ^ crctbl[0xFF]) & 0xFFFF */
    uint16_t acc = gd_crc_step(0xFFFFu, 0x00);
    uint16_t want = (uint16_t)(((uint16_t)(0xFFFFu << 8)) ^ gd_crctbl_sega[0xFF]);
    CHECK(acc == want,
          "1 passo com 0x00 = (0xFF00 ^ crctbl[0xFF]) = 0x%04x", acc);
    CHECK(acc == 0xE1F0, "1 passo com 0x00 = 0x%04x (valor explicito)", acc);
    /* entrada vazia: ~0xFFFF = 0x0000 */
    CHECK(gd_crc_subcode(zero, 0) == 0x0000,
          "entrada vazia = 0x%04x (~0xFFFF)", gd_crc_subcode(zero, 0));
    /* o NOT e' o que distingue: sem ele o resultado seria 0xFFFF */
    CHECK(gd_crc_subcode(zero, 1) != 0xFFFF,
          "1 zero nao devolve 0xFFFF (o NOT e' aplicado)");
}

/* Propriedades estruturais: um CRC tem de as ter. */
static void test_propriedades(void)
{
    uint8_t buf[200];
    uint16_t seen[200];
    int n, distinct = 1, i, j;
    uint8_t a[16], b[16];

    /* 1. inverter 1 bit tem de mudar o resultado */
    for (i = 0; i < 16; i++) a[i] = 0x41;
    memcpy(b, a, 16); b[0] ^= 0x01;
    CHECK(gd_crc_subcode(a, 16) != gd_crc_subcode(b, 16),
          "inverter 1 bit muda o CRC (0x%04x vs 0x%04x)",
          gd_crc_subcode(a, 16), gd_crc_subcode(b, 16));

    /* 2. entradas de comprimento diferente nao podem dar o mesmo valor
     *    repetidamente — so existe se nao houver ciclo curto */
    for (n = 0; n < 200; n++) { memset(buf, 0, (size_t)n); seen[n] = gd_crc_subcode(buf, (size_t)n); }
    for (i = 0; i < 200; i++)
        for (j = i + 1; j < 200; j++)
            if (seen[i] == seen[j]) { distinct = 0; break; }
    CHECK(distinct, "200 entradas de zeros, 200 valores distintos (sem ciclo curto)");

    for (n = 0; n < 200; n++) { memset(buf, 0xFF, (size_t)n); seen[n] = gd_crc_subcode(buf, (size_t)n); }
    distinct = 1;
    for (i = 0; i < 200; i++)
        for (j = i + 1; j < 200; j++)
            if (seen[i] == seen[j]) { distinct = 0; break; }
    CHECK(distinct, "200 entradas de 0xFF, 200 valores distintos");
}

/* O subcode sao 96 bytes. Este e' o caso que o firmware vai ter de
 * responder, e o valor tem de ser estavel. */
static void test_subcode96(void)
{
    uint8_t sc[96];
    uint16_t c;
    memset(sc, 0, sizeof sc);
    c = gd_crc_subcode(sc, sizeof sc);
    CHECK(c == 0x75D3, "96 zeros -> 0x%04x (valor de referencia)", c);

    memset(sc, 0xFF, sizeof sc);
    CHECK(gd_crc_subcode(sc, sizeof sc) != 0x75D3,
          "96 0xFF != 96 zeros (0x%04x)", gd_crc_subcode(sc, sizeof sc));
}

/* Valores de referencia para comparar com um drive real. Nao ha
 * golden vector aqui: nao temos hardware. [UNKNOWN: hardware] */
static void test_valores_referencia(void)
{
    static const uint8_t nine[9] = { '1','2','3','4','5','6','7','8','9' };
    printf("       GD-ROM(0xFFFF+NOT)  \"123456789\" = 0x%04x\n", gd_crc_subcode(nine, 9));
    printf("       CCITT puro          \"123456789\" = 0x%04x\n", gd_crc_ccitt(nine, 9));
    printf("       -> registar estes dois ao lado do que o scope mostrar\n");
    checks++;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    CASE(97, "A tabela e' a geracao canonica do CRC-CCITT 0x1021");
    test_tabela();
    CASE(98, "init 0xFFFF e NOT final, como no desensamblado");
    test_init_e_not();
    CASE(99, "Propriedades estruturais de um CRC");
    test_propriedades();
    CASE(100, "96 bytes de subcode: valores de referencia");
    test_subcode96();
    CASE(101, "Valores a registar quando houver hardware");
    test_valores_referencia();

    printf("\n%d checks passados, %d falhados (caso %d)\n", checks - fails, fails, g_case);
    return fails ? 1 : 0;
}
