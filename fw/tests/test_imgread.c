/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_imgread.c - Leitor de GDI.
 *
 * Fixtures derivados de GDI reais publicados:
 *   - Sonic Adventure (3 tracks), de libretro/flycast#711
 *   - Crazy Taxi, de dreamcast.wiki
 * A geometria e' a que os emuladores aceitam; o objectivo e' apanhar
 * erros de parsing, nao validar dumps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gd_gdi.h"
#include "gd_format.h"
#include "gd_cdda.h"

static int g_fail, g_pass, g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) { printf("  FAIL  " fmt "\n", ##__VA_ARGS__); g_fail++; } \
        else             { g_pass++; }                                       \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

/* ------------------------------------------------------------------ */
/* Materializa uma imagem de teste no disco                             */
/* ------------------------------------------------------------------ */
static const char *g_dir = "/tmp/gdtest";

static void wr(const char *name, const void *data, size_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (!f) { printf("  FAIL  nao consegui criar %s\n", path); g_fail++; return; }
    if (n) fwrite(data, 1, n, f);
    fclose(f);
}

static void wr_text(const char *name, const char *text)
{
    wr(name, text, strlen(text));
}

/* Cria `n` sectores de 2352 bytes com um padrao reconhecavel. */
static void make_bin(const char *name, uint32_t n, uint32_t first_fad)
{
    uint32_t i, j;
    uint8_t *buf = malloc((size_t)n * 2352);
    for (i = 0; i < n; i++) {
        uint8_t *s = buf + (size_t)i * 2352;
        uint32_t fad = first_fad + i;
        /* sync pattern */
        for (j = 0; j < 12; j++) s[j] = 0;
        s[0] = 0x00; s[1] = 0xFF; s[2] = 0xFF; s[10] = 0x00; s[11] = 0x00;
        /* header */
        s[12] = 0x00; s[13] = 0x02; s[14] = 0x02; s[15] = 0x01; /* mode 1 */
        /* data: o FAD nos primeiros 4 bytes, para deteccao de offset */
        s[16] = (uint8_t)(fad >> 24); s[17] = (uint8_t)(fad >> 16);
        s[18] = (uint8_t)(fad >> 8);  s[19] = (uint8_t)fad;
        for (j = 20; j < 2352; j++) s[j] = (uint8_t)(j & 0xff);
    }
    wr(name, buf, (size_t)n * 2352);
    free(buf);
}

static int rd_sectors(void *ctx, uint32_t fad, uint32_t n,
                      uint32_t ss, void *dst)
{
    (void)ctx;
    (void)fad; (void)n; (void)ss; (void)dst;
    return 0;
}

int main(void)
{
    gd_fs_t fs;
    gd_gdi_disc_t d;
    const char *err = "";

    if (system("rm -rf /tmp/gdtest && mkdir -p /tmp/gdtest") != 0) {
        printf("  FAIL  nao consegui criar o directorio de teste\n");
        return 1;
    }
    fs = gd_fs_stdio(g_dir);

    /* ---------------------------------------------------------------- */
    CASE(48, "Constantes da spec: Tabela 4-1 fecha com a aritmetica");
    /* LBA 0 e' FAD 150, nao FAD 0: o FAD 0 nao tem LBA. */
    CHECK(GD_FAD_TO_LBA(150) == 0, "FAD 150 -> LBA %u", GD_FAD_TO_LBA(150));
    CHECK(!GD_FAD_HAS_LBA(0), "o FAD 0 nao devia ter LBA");
    CHECK(GD_FAD_HAS_LBA(150), "o FAD 150 devia ter LBA");
    CHECK(GD_LBA_TO_FAD(45000) == GD_HD_TRACK3_FAD,
          "LBA 45000 -> FAD %u, esperado %u", GD_LBA_TO_FAD(45000), GD_HD_TRACK3_FAD);
    CHECK(GD_HD_TRACK3_INDEX00_FAD == 45000, "INDEX 00 = %u", GD_HD_TRACK3_INDEX00_FAD);
    CHECK(GD_HD_TRACK3_FAD == 45150, "INDEX 01 = %u", GD_HD_TRACK3_FAD);
    CHECK(GD_HD_SYSTEM_ID1_FAD == 45150, "System ID 1 = %u", GD_HD_SYSTEM_ID1_FAD);
    CHECK(GD_HD_PVD1_FAD == 45166, "PVD 1 = %u", GD_HD_PVD1_FAD);
    CHECK(GD_HD_LEADOUT_FAD == 549300, "lead-out = %u", GD_HD_LEADOUT_FAD);
    CHECK(GD_HD_LAST_SECTOR_FAD == 549299, "ultimo sector = %u", GD_HD_LAST_SECTOR_FAD);
    /* 45000 + 504300 = 549300. A spec fecha. */
    CHECK(GD_HD_TRACK3_INDEX00_FAD + GD_HD_SECTORS_MAX == GD_HD_LEADOUT_FAD,
          "45000 + 504300 = %u, lead-out %u",
          GD_HD_TRACK3_INDEX00_FAD + GD_HD_SECTORS_MAX, GD_HD_LEADOUT_FAD);
    /* 10 min * 60 * 75 = 45000. */
    CHECK(10u * 60u * 75u == 45000u, "10:00:00 em sectores = %u", 10u * 60u * 75u);
    /* 10:02:00 = 45000 + 150 = 45150 */
    CHECK(10u * 60u * 75u + 150u == GD_HD_TRACK3_FAD,
          "10:02:00 em sectores = %u, esperado %u",
          10u * 60u * 75u + 150u, GD_HD_TRACK3_FAD);

    /* ---------------------------------------------------------------- */
    CASE(49, "Parse de um GDI de 3 tracks (Sonic Adventure)");
    make_bin("tr1.bin", 11361, 150);
    make_bin("tr2.bin", 3000, 11361 + 150);
    make_bin("tr3.bin", 1000, 45150);
    wr_text("sa.gdi",
        "3\n"
        "1 0 4 2352 \"tr1.bin\" 0\n"
        "2 11361 0 2352 \"tr2.bin\" 0\n"
        "3 45000 4 2352 \"tr3.bin\" 0\n");

    CHECK(gd_gdi_parse(&fs, "sa.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(d.gdi.ntracks == 3, "ntracks = %d", d.gdi.ntracks);
    CHECK(d.gdi.track[0].track == 1, "track[0].track = %d", d.gdi.track[0].track);
    CHECK(d.gdi.track[0].lba == 0, "track 1 LBA = %u", d.gdi.track[0].lba);
    CHECK(d.gdi.track[0].start_fad == 150,
          "track 1 StartFAD = %u, esperado 150", d.gdi.track[0].start_fad);
    CHECK(d.gdi.track[0].ctrl == GD_CTRL_DATA, "track 1 CTRL = %u", d.gdi.track[0].ctrl);
    CHECK(d.gdi.track[0].sector_size == 2352, "track 1 SSIZE = %u", d.gdi.track[0].sector_size);
    CHECK(strcmp(d.gdi.track[0].path, "tr1.bin") == 0,
          "track 1 path = '%s'", d.gdi.track[0].path);
    CHECK(d.gdi.track[2].start_fad == 45150,
          "track 3 StartFAD = %u, esperado 45150", d.gdi.track[2].start_fad);
    CHECK(gd_gdi_validate(&d.gdi, &err) == 0, "validate: %s", err);

    /* ---------------------------------------------------------------- */
    CASE(50, "Nome de ficheiro com e' sem aspas");
    wr_text("quotes.gdi",
        "3\n"
        "1 0 4 2352 unquoted.bin 0\n"
        "2 11361 0 2352 \"quoted name.bin\" 0\n"
        "3 45000 4 2352 unquoted.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "quotes.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(strcmp(d.gdi.track[0].path, "unquoted.bin") == 0,
          "sem aspas: '%s'", d.gdi.track[0].path);
    CHECK(strcmp(d.gdi.track[1].path, "quoted name.bin") == 0,
          "com aspas: '%s'", d.gdi.track[1].path);

    /* ---------------------------------------------------------------- */
    CASE(51, "end_fad vem do tamanho do ficheiro");
    CHECK(gd_gdi_open_disc(&fs, "sa.gdi", &d, &err) == 0, "open_disc: %s", err);
    CHECK(d.gdi.track[0].end_fad == 150 + 11361 - 1,
          "track 1 end_fad = %u, esperado %u",
          d.gdi.track[0].end_fad, 150 + 11361 - 1);
    CHECK(d.gdi.track[1].end_fad == (11361 + 150) + 3000 - 1,
          "track 2 end_fad = %u", d.gdi.track[1].end_fad);
    CHECK(d.gdi.track[2].end_fad == 45150 + 1000 - 1,
          "track 3 end_fad = %u", d.gdi.track[2].end_fad);

    /* ---------------------------------------------------------------- */
    CASE(52, "As duas areas tem lead-outs diferentes");
    {
        uint8_t toc[GD_TOC_SIZE];
        uint32_t ld, hd;
        d.rd.read_sectors = rd_sectors; d.rd.ctx = NULL;
        d.disc.priv = &d;

        d.disc.fill_toc(&d.disc, GD_AREA_SINGLE_DENSITY, toc);
        ld = ((uint32_t)toc[405] << 16) | ((uint32_t)toc[406] << 8) | toc[407];
        /* Area LD: lead-out = EndFAD(track 2) + 1 */
        CHECK(ld == d.gdi.track[1].end_fad + 1,
              "lead-out LD = %u, esperado %u", ld, d.gdi.track[1].end_fad + 1);

        /* Primeira/ultima track: a area LD tem so tracks 1..2 */
        CHECK(((toc[397] << 8) | toc[398]) == 1, "primeira track LD = %d",
              (toc[397] << 8) | toc[398]);
        CHECK(((toc[401] << 8) | toc[402]) == 2, "ultima track LD = %d",
              (toc[401] << 8) | toc[402]);
        /* ADR forcado a 1, CTRL 4 */
        CHECK(toc[0] == 0x41, "Control/ADR da track 1 = 0x%02x, esperado 0x41", toc[0]);
        /* Track 3 nao existe na area LD: fica 0xFF */
        CHECK(toc[2 * 4] == 0xff, "track 3 na area LD = 0x%02x, esperado 0xFF", toc[2 * 4]);

        d.disc.fill_toc(&d.disc, GD_AREA_HIGH_DENSITY, toc);
        hd = ((uint32_t)toc[405] << 16) | ((uint32_t)toc[406] << 8) | toc[407];
        CHECK(hd == GD_HD_LEADOUT_FAD, "lead-out HD = %u, esperado %u",
              hd, GD_HD_LEADOUT_FAD);
        CHECK(((toc[397] << 8) | toc[398]) == 3, "primeira track HD = %d",
              (toc[397] << 8) | toc[398]);
        CHECK(((toc[401] << 8) | toc[402]) == 3, "ultima track HD = %d",
              (toc[401] << 8) | toc[402]);
    }

    /* ---------------------------------------------------------------- */
    CASE(53, "O gap entre as duas areas nao pertence a nenhuma track");
    {
        /* uint32_t: um FAD nao cabe num uint8_t. Ja foi bug. */
        uint32_t gap = d.gdi.track[1].end_fad + 1;
        CHECK(d.gdi.track[1].end_fad < gap, "pre-condicao");
        CHECK(gap < d.gdi.track[2].start_fad,
              "esperado um gap entre %u e %u", d.gdi.track[1].end_fad,
              d.gdi.track[2].start_fad);
        /* E' por isso que track_of_fad devolve 0xAA la no meio. */
        CHECK(d.disc.track_of_fad(&d.disc, gap) == 0xaa,
              "FAD %u no gap devolveu %u, esperado 0xAA", gap,
              d.disc.track_of_fad(&d.disc, gap));
        CHECK(d.disc.track_of_fad(&d.disc, d.gdi.track[2].start_fad) == 3,
              "o primeiro sector da track 3 tem de pertence a track 3");
    }

    /* ---------------------------------------------------------------- */
    CASE(54, "Rejeicao de GDI invalidos");
    wr_text("bad_count.gdi", "2\n1 0 4 2352 tr1.bin 0\n3 45000 4 2352 tr3.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "bad_count.gdi", &d.gdi, &err) != 0,
          "um GDI com 2 tracks tem de ser rejeitado");

    wr_text("bad_lba3.gdi",
        "3\n1 0 4 2352 tr1.bin 0\n2 11361 0 2352 tr2.bin 0\n3 44000 4 2352 tr3.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "bad_lba3.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(gd_gdi_validate(&d.gdi, &err) != 0,
          "track 3 a comecar no LBA 44000 tem de ser rejeitado");

    wr_text("bad_ctrl.gdi",
        "3\n1 0 4 2352 tr1.bin 0\n2 11361 2 2352 tr2.bin 0\n3 45000 4 2352 tr3.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "bad_ctrl.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(gd_gdi_validate(&d.gdi, &err) != 0, "CTRL = 2 tem de ser rejeitado");

    wr_text("bad_tr1.gdi",
        "3\n1 0 0 2352 tr1.bin 0\n2 11361 0 2352 tr2.bin 0\n3 45000 4 2352 tr3.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "bad_tr1.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(gd_gdi_validate(&d.gdi, &err) != 0, "track 1 de audio tem de ser rejeitada");

    wr_text("missing_file.gdi",
        "3\n1 0 4 2352 nao_existe.bin 0\n2 11361 0 2352 tr2.bin 0\n3 45000 4 2352 tr3.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "missing_file.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(gd_gdi_open_disc(&fs, "missing_file.gdi", &d, &err) != 0,
          "um .bin em falta tem de ser erro nao silencioso: %s", err);

    /* ---------------------------------------------------------------- */
    CASE(55, "SSIZE = 0 significa 'track ausente' (dumps nullDC)");
    wr_text("sparse.gdi",
        "3\n"
        "1 0 0 0 none 0\n"
        "2 0 0 0 none 0\n"
        "3 45000 4 2352 tr3.bin 0\n");
    CHECK(gd_gdi_parse(&fs, "sparse.gdi", &d.gdi, &err) == 0, "parse: %s", err);
    CHECK(d.gdi.track[0].missing, "track 1 devia estar marcada como ausente");
    CHECK(d.gdi.track[2].missing == 0, "track 3 nao devia estar ausente");

    /* ---------------------------------------------------------------- */
    CASE(56, "Mapa FAD -> posicao no ficheiro, com o sync em Offset 0");
    {
        /*
         * O ponto que mais se erra: OFFSET 0 aponta para o INICIO do
         * sector 2352, com o sync, nao para o user data de 2048.
         * byte_pos(FAD) = OFFSET + (FAD - (LBA + 150)) * SSIZE
         */
        gd_file_t *f = NULL;
        uint8_t head[16];
        uint32_t pos = 0;  /* FAD 150 = primeiro sector da track 1 */
        CHECK(fs.open(fs.ctx, "tr1.bin", &f) == 0, "abrir tr1.bin");
        CHECK(fs.read(f, pos, head, 16) == 0, "ler 16 bytes");
        CHECK(head[0] == 0x00 && head[1] == 0xff && head[2] == 0xff,
              "os primeiros bytes tem de ser o sync pattern: %02x %02x %02x",
              head[0], head[1], head[2]);
        fs.close(f);
    }

    /* ---------------------------------------------------------------- */
    CASE(57, "REQ_SES: 2 areas, nao 2 sessoes");
    {
        uint8_t ses[6];
        /* Reabrir: um open_disc falhado poe a estrutura a zeros, e
         * chamar um callback nulo rebenta. E' comportamento correcto -
         * quem falhou nao pode usar a estrutura. */
        CHECK(gd_gdi_open_disc(&fs, "sa.gdi", &d, &err) == 0, "reabrir: %s", err);
        d.disc.priv = &d;
        d.disc.fill_session_info(&d.disc, 0, ses);
        CHECK(ses[2] == 2, "numero de areas = %u", ses[2]);
        CHECK(((ses[3] << 16) | (ses[4] << 8) | ses[5]) == GD_HD_LEADOUT_FAD,
              "EndFAD da area 0 = %u",
              (unsigned)((ses[3] << 16) | (ses[4] << 8) | ses[5]));

        d.disc.fill_session_info(&d.disc, 1, ses);
        CHECK(ses[2] == 1, "a area de baixa densidade comeca na track %u", ses[2]);
        CHECK(((ses[3] << 16) | (ses[4] << 8) | ses[5]) == 150,
              "a area LD comeca no FAD %u, esperado 150",
              (unsigned)((ses[3] << 16) | (ses[4] << 8) | ses[5]));

        d.disc.fill_session_info(&d.disc, 2, ses);
        CHECK(ses[2] == 3, "a area de alta densidade comeca na track %u", ses[2]);
        CHECK(((ses[3] << 16) | (ses[4] << 8) | ses[5]) == 45150,
              "a area HD comeca no FAD %u, esperado 45150",
              (unsigned)((ses[3] << 16) | (ses[4] << 8) | ses[5]));
    }

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
