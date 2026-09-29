/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_systable.c - Localiza a tabela de syscalls GDC num 1ST_READ.BIN.
 *
 * Ferramenta de estudo, nao do firmware. Existe porque o docs/23 afirma
 * coisas byte-a-byte sobre a tabela, e o docs/24 §6 manda que venham de um
 * re-executavel e nao da memoria: ja houve duas leituras erradas ao nivel
 * do byte nesta sessao (o objdump em endianness trocada, e uma contagem de
 * constantes sem alinhamento).
 *
 * O que procura: os 13 thunks de 20 bytes, cada um com o vector de syscall
 * do SH4 a 0x8C0000BC e o numero do syscall no literal a +0x00.
 *
 * Uso:
 *   gd_systable <ficheiro> [...]
 *
 * Saida: uma linha por ficheiro, com o offset, os numeros, e o sha256 da
 * tabela para comparar entre discos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* O vector de trap do SH4 que o BIOS intercepta, em LE. */
static const uint8_t VECTOR[4] = { 0xBC, 0x00, 0x00, 0x8C };

#define NTHUNK   13
#define TUNKSZ   20

/* Os nomes, pela ordem de gdc_lib_ (docs/21 §2). */
static const char *NOME[NTHUNK] = {
    "gdGdcReqCmd",      "gdGdcGetCmdStat",  "gdGdcExecServer",
    "gdGdcInitSystem",  "gdGdcGetDrvStat",  "gdGdcG1DmaEnd",
    "gdGdcReqDmaTrans", "gdGdcCheckDmaTrans", "gdGdcReadAbort",
    "gdGdcReset",       "gdGdcChangeDataType",
    "gdBtGdcReInitEntry","gdBtGdcAddDesc"
};

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); fprintf(stderr, "%s: vazio\n", path); return NULL; }
    rewind(f);
    uint8_t *b = malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s: leitura incompleta\n", path);
        free(b); fclose(f); return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return b;
}

/* Procura o primeiro offset onde a tabela comeca. Um thunk comeca 4 bytes
 * antes do vector, porque o numero vem no literal inicial. */
/*
 * A tabela comeca 4 bytes antes do primeiro vector, porque o numero do
 * syscall vem no literal inicial do thunk. Procura-se o vector, e
 * confirma-se que o thunk seguinte (a TUNKSZ bytes) tem o mesmo vector.
 */
static long find_table(const uint8_t *d, size_t len)
{
    size_t v;                       /* posicao do VECTOR dentro do ficheiro */
    for (v = 8; v + 8 <= len; v++) {
        if (memcmp(d + v, VECTOR, 4) != 0)
            continue;
        if (v + TUNKSZ + 4 <= len &&
            memcmp(d + v, d + v + TUNKSZ, 4) == 0)
            return (long)(v - 4);   /* o numero esta 4 bytes antes */
    }
    return -1;
}

static void sha256_of(const uint8_t *d, size_t n, char out[65]);

int main(int argc, char **argv)
{
    int i;
    if (argc < 2) {
        printf("uso: %s <1ST_READ.BIN> [...]\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        size_t len = 0;
        uint8_t *d = slurp(argv[i], &len);
        if (!d) { printf("%-14s ERRO\n", argv[i]); continue; }

        long off = find_table(d, len);
        if (off < 0) {
            printf("%-14s sem tabela de syscalls\n", argv[i]);
            free(d);
            continue;
        }
        size_t nbytes = (size_t)NTHUNK * TUNKSZ;
        char hex[65];
        sha256_of(d + off, nbytes, hex);
        printf("%-14s off=0x%06lX  sha256=%.16s\n", argv[i], off, hex);
        printf("   syscalls:");
        for (int k = 0; k < NTHUNK; k++) {
            uint16_t v = (uint16_t)(d[off + k * TUNKSZ] |
                                    (d[off + k * TUNKSZ + 1] << 8));
            printf(" %u", v);
        }
        printf("\n   ");
        for (int k = 0; k < NTHUNK; k++) printf("%s ", NOME[k]);
        printf("\n");
        free(d);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SHA-256, so para comparar tabelas entre discos. Implementacao
 * directa, sem dependencias, para a ferramenta correr em qualquer sitio. */
static const uint32_t K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))

static void sha256_of(const uint8_t *d, size_t n, char out[65])
{
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t total = n + 1 + 8;
    while (total % 64) total++;
    uint8_t *m = calloc(total, 1);
    if (!m) { out[0] = 0; return; }
    memcpy(m, d, n);
    m[n] = 0x80;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) m[total - 1 - k] = (uint8_t)(bits >> (8 * k));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[64];
        for (int k = 0; k < 16; k++)
            w[k] = ((uint32_t)m[off+k*4]<<24)|((uint32_t)m[off+k*4+1]<<16)|
                   ((uint32_t)m[off+k*4+2]<<8)|m[off+k*4+3];
        for (int k = 16; k < 64; k++) {
            uint32_t s0 = ROR(w[k-15],7) ^ ROR(w[k-15],18) ^ (w[k-15]>>3);
            uint32_t s1 = ROR(w[k-2],17) ^ ROR(w[k-2],19)  ^ (w[k-2]>>10);
            w[k] = w[k-16] + s0 + w[k-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],e=h[4],f=h[5],g=h[6],hh=h[7];
        uint32_t d2=h[3];
        for (int k = 0; k < 64; k++) {
            uint32_t S1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[k] + w[k];
            uint32_t S0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d2+t1; d2=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d2; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(m);
    static const char hx[] = "0123456789abcdef";
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 4; j++) {
            uint8_t byte = (uint8_t)(h[k] >> (24 - 8 * j));
            out[k*8 + j*2]     = hx[byte >> 4];
            out[k*8 + j*2 + 1] = hx[byte & 15];
        }
    out[64] = 0;
}
