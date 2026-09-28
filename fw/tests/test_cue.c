/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_cue.c - Leitor de CUE: Multi-Cue do Redump e TOSEC.
 *
 * O caso mais importante e' o 64: um CUE TOSEC com PREGAP e' indecodificavel
 * pelo Flycast, que nao tem handler para PREGAP nem marca o disco como
 * GD-ROM. E' uma das razoes pelas quais este parser existe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gd_cue.h"
#include "gd_gdi.h"

static int g_fail, g_pass, g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) { printf("  FAIL  " fmt "\n", ##__VA_ARGS__); g_fail++; } \
        else             { g_pass++; }                                       \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

static const char *g_dir = "/tmp/cuetest";

static void wr_text(const char *name, const char *text)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (!f) { printf("  FAIL  nao criei %s\n", path); g_fail++; return; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static void make_bin(const char *name, uint32_t n)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (!f) { printf("  FAIL  nao criei %s\n", path); g_fail++; return; }
    for (i = 0; i < n * 2352u; i++) fputc((int)(i & 0xff), f);
    fclose(f);
}

/* O Multi-Cue canonico do Redump, 5 tracks. */
static const char *redump_cue =
"REM SINGLE-DENSITY AREA\n"
"FILE \"Game (Track 1).bin\" BINARY\n"
"  TRACK 01 MODE1/2352\n"
"    INDEX 01 00:00:00\n"
"FILE \"Game (Track 2).bin\" BINARY\n"
"  TRACK 02 AUDIO\n"
"    INDEX 00 00:00:00\n"
"    INDEX 01 00:02:00\n"
"REM HIGH-DENSITY AREA\n"
"FILE \"Game (Track 3).bin\" BINARY\n"
"  TRACK 03 MODE1/2352\n"
"    INDEX 01 00:00:00\n"
"FILE \"Game (Track 4).bin\" BINARY\n"
"  TRACK 04 AUDIO\n"
"    INDEX 00 00:00:00\n"
"    INDEX 01 00:02:00\n"
"FILE \"Game (Track 5).bin\" BINARY\n"
"  TRACK 05 MODE1/2352\n"
"    INDEX 00 00:00:00\n"
"    INDEX 01 00:03:00\n";

int main(void)
{
    gd_fs_t fs;
    gd_cue_t c;
    gd_gdi_t g;
    const char *err = "";

    if (system("rm -rf /tmp/cuetest && mkdir -p /tmp/cuetest") != 0) {
        printf("  FAIL  nao criei o directorio\n"); return 1;
    }
    fs = gd_fs_stdio(g_dir);
    make_bin("Game (Track 1).bin", 600);
    make_bin("Game (Track 2).bin", 400);
    make_bin("Game (Track 3).bin", 500);
    make_bin("Game (Track 4).bin", 300);
    make_bin("Game (Track 5).bin", 900);

    /* ---------------------------------------------------------------- */
    CASE(58, "Parse de tempo mm:ss:ff");
    {
        uint32_t fr;
        CHECK(gd_cue_parse_time("00:02:00", &fr) == 0 && fr == 150,
              "00:02:00 = %u, esperado 150", fr);
        CHECK(gd_cue_parse_time("10:00:00", &fr) == 0 && fr == 45000,
              "10:00:00 = %u, esperado 45000", fr);
        CHECK(gd_cue_parse_time("10:02:00", &fr) == 0 && fr == 45150,
              "10:02:00 = %u, esperado 45150", fr);
        CHECK(gd_cue_parse_time("03:45:37", &fr) == 0 && fr == (3*60+45)*75 + 37,
              "03:45:37 = %u", fr);
        CHECK(gd_cue_parse_time("lixo", &fr) != 0, "'lixo' tem de falhar");
    }

    /* ---------------------------------------------------------------- */
    CASE(59, "Multi-Cue do Redump: 5 tracks, 2 areas");
    wr_text("redump.cue", redump_cue);
    CHECK(gd_cue_parse(&fs, "redump.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(c.ntracks == 5, "ntracks = %d, esperado 5", c.ntracks);
    CHECK(c.is_gdrom, "o disco tem de ser reconhecido como GD-ROM");
    CHECK(c.saw_high_density, "o marcador REM HIGH-DENSITY AREA tem de ser visto");
    CHECK(strcmp(c.track[0].path, "Game (Track 1).bin") == 0,
          "track 1 path = '%s'", c.track[0].path);
    CHECK(c.track[0].sector == GD_SECT_MODE1_2352, "track 1 sector mode");
    CHECK(c.track[0].ctrl == GD_CTRL_DATA, "track 1 CTRL");
    CHECK(c.track[1].sector == GD_SECT_AUDIO, "track 2 tem de ser AUDIO");
    CHECK(c.track[1].ctrl == GD_CTRL_AUDIO, "track 2 CTRL");
    CHECK(c.track[1].index0 == 0, "track 2 INDEX 00 = %u", c.track[1].index0);
    CHECK(c.track[1].index1 == 150, "track 2 INDEX 01 = %u", c.track[1].index1);
    /*
     * O parse NAO calcula posicoes: isso depende dos tamanhos dos
     * ficheiros e e' feito em gd_cue_to_gdi. Ver o caso 68.
     */
    /* A track 3 comeca no FAD 45150, NAO no 45000. */
    CHECK(c.track[2].start_fad == GD_HD_TRACK3_FAD,
          "track 3 start_fad = %u, esperado %u",
          c.track[2].start_fad, GD_HD_TRACK3_FAD);
    CHECK(c.track[2].sector == GD_SECT_MODE1_2352, "track 3 sector mode");
    CHECK(c.track[2].ctrl == GD_CTRL_DATA, "track 3 CTRL");
    CHECK(gd_cue_validate(&c, &err) == 0, "validate: %s", err);

    /* ---------------------------------------------------------------- */
    CASE(60, "A track 3 de um Multi-Cue aponta para FAD 45150, nao 45000");
    CHECK(c.track[2].start_fad != GD_HD_TRACK3_INDEX00_FAD,
          "45000 e' o INDEX 00 (pause); os dados comecam em 45150");
    /* E o GDI de saida tem de dizer LBA 45000, porque e' LBA = FAD-150. */
    CHECK(gd_cue_to_gdi(&fs, &c, &g, &err) == 0, "cue2gdi: %s", err);
    CHECK(g.ntracks == 5, "GDI com %d tracks", g.ntracks);
    CHECK(g.track[2].lba == 45000,
          "track 3 no GDI tem LBA %u, esperado 45000", g.track[2].lba);
    CHECK(g.track[2].start_fad == 45150, "track 3 StartFAD = %u", g.track[2].start_fad);

    /* ---------------------------------------------------------------- */
    CASE(61, "CUE TOSEC: so a track de alta densidade, com PREGAP");
    /*
     * Formato que o Flycast nao decodifica. O ficheiro NAO contem o
     * pregap: o leitor tem de o sintetizar.
     */
    make_bin("track03.bin", 700);
    wr_text("tosec.cue",
        "FILE \"track03.bin\" BINARY\n"
        "  TRACK 03 MODE1/2352\n"
        "    PREGAP 10:00:00\n"
        "    INDEX 01 00:00:00\n"
        "    POSTGAP 00:02:00\n");
    CHECK(gd_cue_parse(&fs, "tosec.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(c.ntracks == 1, "ntracks = %d", c.ntracks);
    CHECK(c.is_gdrom, "um TOSEC com PREGAP 10:00:00 tem de marcar GD-ROM");
    CHECK(c.track[0].start_fad == GD_HD_TRACK3_FAD,
          "a track 3 comeca no FAD %u, esperado %u",
          c.track[0].start_fad, GD_HD_TRACK3_FAD);
    CHECK(c.track[0].pregap_sectors == 150,
          "pregap a sintetizar = %u sectores", c.track[0].pregap_sectors);
    CHECK(c.track[0].postgap_sectors == 150,
          "postgap = %u sectores", c.track[0].postgap_sectors);
    /* O ficheiro NAO tem os 150 sectores de pause, logo o GDI tem de
     * apontar para o INDEX 01 e nao inventar um INDEX 00. */
    CHECK(gd_cue_to_gdi(&fs, &c, &g, &err) == 0, "cue2gdi: %s", err);
    /* O padding de tracks ausentes reordena, por isso procura-se pelo
     * numero e nao pelo indice. */
    {
        const gd_gdi_track_t *tr3 = NULL;
        int i;
        for (i = 0; i < g.ntracks; i++)
            if (g.track[i].track == 3) { tr3 = &g.track[i]; break; }
        CHECK(tr3 != NULL, "a track 3 tem de existir no GDI gerado");
        if (tr3) {
            CHECK(tr3->lba == 45000, "LBA = %u, esperado 45000", tr3->lba);
            CHECK(tr3->start_fad == 45150, "StartFAD = %u, esperado 45150",
                  tr3->start_fad);
        }
        /* As tracks 1 e 2 synthetizadas como ausentes. */
        CHECK(g.ntracks == 3, "um dump TOSEC da 3 tracks no GDI, obtive %d",
              g.ntracks);
        for (i = 0; i < g.ntracks; i++) {
            if (g.track[i].track == 1 || g.track[i].track == 2)
                CHECK(g.track[i].missing,
                      "a track %d tem de estar marcada como ausente",
                      g.track[i].track);
        }
    }

    /* ---------------------------------------------------------------- */
    CASE(62, "PREGAP 10:02:00 (variante Redump) tambem e' aceite");
    wr_text("tosec2.cue",
        "FILE \"track03.bin\" BINARY\n"
        "  TRACK 03 MODE1/2352\n"
        "    PREGAP 10:02:00\n"
        "    INDEX 01 00:00:00\n");
    CHECK(gd_cue_parse(&fs, "tosec2.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(c.track[0].start_fad == GD_HD_TRACK3_FAD,
          "StartFAD = %u", c.track[0].start_fad);
    CHECK(c.track[0].pregap_sectors == 0,
          "com 10:02:00 o pregap ja esta incluido: %u", c.track[0].pregap_sectors);

    /* ---------------------------------------------------------------- */
    CASE(63, "Um CUE sem marcador de area nao e' reconhecido como GD");
    wr_text("plain.cue",
        "FILE \"a.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    CHECK(gd_cue_parse(&fs, "plain.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(!c.is_gdrom,
          "sem REM ... AREA nem PREGAP 10:xx, nao da' para saber que e' GD");
    CHECK(gd_cue_validate(&c, &err) != 0,
          "a validacao tem de recusar um CUE que nao se sabe o que e'");

    /* ---------------------------------------------------------------- */
    CASE(64, "Modos de sector e respectivos tamanhos");
    {
        struct { const char *mode; uint32_t size; int audio; } t[] = {
            { "AUDIO",     2352, 1 },
            { "MODE1/2048", 2048, 0 },
            { "MODE1/2352", 2352, 0 },
            { "MODE2/2336", 2336, 0 },
            { "MODE2/2352", 2352, 0 },
            { "CDI/2336",   2336, 0 },
            { "CDI/2352",   2352, 0 },
        };
        size_t k;
        for (k = 0; k < sizeof t / sizeof t[0]; k++) {
            char cue[256];
            uint32_t ss = 0;
            gd_cue_sector_t st;
            snprintf(cue, sizeof cue,
                     "REM HIGH-DENSITY AREA\n"
                     "FILE \"track03.bin\" BINARY\n"
                     "  TRACK 03 %s\n    INDEX 01 00:00:00\n", t[k].mode);
            wr_text("m.cue", cue);
            CHECK(gd_cue_parse(&fs, "m.cue", &c, &err) == 0,
                  "%s: parse falhou (%s)", t[k].mode, err);
            st = c.track[0].sector;
            ss = c.track[0].sector_size;
            CHECK(ss == t[k].size, "%s: tamanho %u, esperado %u",
                  t[k].mode, ss, t[k].size);
            CHECK((c.track[0].ctrl == GD_CTRL_AUDIO) == (t[k].audio != 0),
                  "%s: CTRL errado", t[k].mode);
            (void)st;
        }
    }

    /* ---------------------------------------------------------------- */
    CASE(65, "Modo de sector desconhecido e' erro, nao silencioso");
    wr_text("bad.cue",
        "REM HIGH-DENSITY AREA\n"
        "FILE \"track03.bin\" BINARY\n"
        "  TRACK 03 MODE9/9999\n"
        "    INDEX 01 00:00:00\n");
    CHECK(gd_cue_parse(&fs, "bad.cue", &c, &err) != 0,
          "um modo desconhecido tem de fazer o parser falhar");

    /* ---------------------------------------------------------------- */
    CASE(66, "Um .bin em falta no CUE e' erro nao silencioso");
    wr_text("missing.cue",
        "REM HIGH-DENSITY AREA\n"
        "FILE \"nao_existe.bin\" BINARY\n"
        "  TRACK 03 MODE1/2352\n"
        "    INDEX 01 00:00:00\n");
    CHECK(gd_cue_parse(&fs, "missing.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(gd_cue_to_gdi(&fs, &c, &g, &err) != 0,
          "a conversao tem de falhar se o .bin nao existe: %s", err);

    /* ---------------------------------------------------------------- */
    CASE(67, "CRLF e directivas desconhecidas nao perturbam o parse");
    wr_text("crlf.cue",
        "REM HIGH-DENSITY AREA\r\n"
        "REM COMMENT isto e' um comentario qualquer\r\n"
        "FILE \"track03.bin\" BINARY\r\n"
        "  TRACK 03 MODE1/2352\r\n"
        "    CATALOG 1234567890123\r\n"
        "    ISRC ABCDE1234567\r\n"
        "    INDEX 01 00:00:00\r\n");
    CHECK(gd_cue_parse(&fs, "crlf.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(c.track[0].start_fad == GD_HD_TRACK3_FAD,
          "StartFAD = %u", c.track[0].start_fad);
    CHECK(c.catalog_len == 1, "CATALOG = '%s'", c.catalog);

    /* ---------------------------------------------------------------- */
    CASE(68, "Posicao das tracks: acumular tamanhos de ficheiro");
    /*
     * Regressao. A posicao de uma track so se sabe depois de medir os
     * ficheiros anteriores. Uma versao anterior fixava start_fad no
     * parse e as tracks 2, 4 e 5 saiam com LBA 4294967146 (= -150, um
     * underflow silencioso) que produzia um GDI inutil.
     */
    wr_text("geo.cue", redump_cue);
    CHECK(gd_cue_parse(&fs, "geo.cue", &c, &err) == 0, "parse: %s", err);
    CHECK(gd_cue_to_gdi(&fs, &c, &g, &err) == 0, "cue2gdi: %s", err);
    {
        /* 600 sectores na track 1 -> a track 2 arranca em LBA 600. */
        int t2 = -1, t4 = -1;
        int k;
        for (k = 0; k < g.ntracks; k++) {
            if (g.track[k].track == 2) t2 = g.track[k].lba;
            if (g.track[k].track == 4) t4 = g.track[k].lba;
        }
        CHECK(t2 == 600, "track 2 LBA = %d, esperado 600", t2);
        /* Track 3 salta para 45000; 500 sectores depois, a 4 fica 45500. */
        CHECK(t4 == 45500, "track 4 LBA = %d, esperado 45500", t4);
    }
    /* Nenhum LBA pode ser enorme: seria um underflow. */
    {
        int k, bad = 0;
        for (k = 0; k < g.ntracks; k++)
            if (g.track[k].lba > 600000u) bad++;
        CHECK(bad == 0, "%d tracks com LBA absurdo (underflow de FAD)", bad);
    }

    /* ---------------------------------------------------------------- */
    CASE(69, "Round-trip: CUE -> GDI -> reler o GDI, mesma geometria");
    {
        gd_gdi_t g2;
        const char *e2 = "";
        gd_fs_t fs2 = gd_fs_stdio("/tmp/cuetest");
        char path[512];
        FILE *fp;
        int mismatch = 0, k;

        CHECK(gd_cue_to_gdi(&fs, &c, &g, &err) == 0, "cue2gdi: %s", err);
        snprintf(path, sizeof path, "%s/rt.gdi", g_dir);
        fp = fopen(path, "wb");
        CHECK(fp != NULL, "abrir rt.gdi");
        if (fp) {
            CHECK(gd_gdi_write(&g, fp) == 0, "escrever rt.gdi");
            fclose(fp);
        }
        CHECK(gd_gdi_parse(&fs2, "rt.gdi", &g2, &e2) == 0,
              "reler o GDI que escrevemos: %s", e2);
        CHECK(gd_gdi_validate(&g2, &e2) == 0, "validar o relido: %s", e2);
        CHECK(g2.ntracks == g.ntracks, "%d tracks != %d", g2.ntracks, g.ntracks);
        for (k = 0; k < g.ntracks && k < g2.ntracks; k++) {
            if (g.track[k].track   != g2.track[k].track)   mismatch++;
            if (g.track[k].lba     != g2.track[k].lba)     mismatch++;
            if (g.track[k].ctrl    != g2.track[k].ctrl)    mismatch++;
            if (g.track[k].sector_size != g2.track[k].sector_size) mismatch++;
            if (g.track[k].offset != g2.track[k].offset)   mismatch++;
            if (strcmp(g.track[k].path, g2.track[k].path)) mismatch++;
        }
        CHECK(mismatch == 0, "%d campos diferentes no round-trip", mismatch);
    }

    /* ---------------------------------------------------------------- */
    CASE(70, "TOSEC: so' a track 3 tem de passar a validacao e a conversao");
    {
        /*
         * Regressao. Um dump TOSEC / httpd-ack so' contem a track de
         * alta densidade, porque um dump de alta densidade nao tem
         * audio. O `gd_cue_validate` exigia 3 tracks e o `gd_cue_to_gdi`
         * voltava a exigir 3 no fim, o que bloqueava um formato que a
         * doc 15 documenta como suportado.
         *
         * Este teste so' passa se os dois pontos de validação aceitarem
         * uma imagem de uma so track.
         */
        gd_cue_t tc;
        gd_gdi_t tg;
        const char *te = "";
        gd_fs_t tfs = gd_fs_stdio(g_dir);
        char tp[512];
        FILE *fp;

        memset(&tc, 0, sizeof tc);
        make_bin("tosec.bin", 20);

        snprintf(tp, sizeof tp, "%s/tosec.cue", g_dir);
        fp = fopen(tp, "wb");
        CHECK(fp != NULL, "abrir tosec.cue");
        if (fp) {
            fputs("FILE \"tosec.bin\" BINARY\n"
                  "  TRACK 03 MODE1/2352\n"
                  "    PREGAP 10:00:00\n"
                  "    INDEX 01 00:00:00\n", fp);
            fclose(fp);
        }
        CHECK(gd_cue_parse(&tfs, "tosec.cue", &tc, &te) == 0,
              "parse do CUE TOSEC: %s", te);
        CHECK(tc.ntracks == 1, "TOSEC tem %d tracks, esperava 1", tc.ntracks);
        CHECK(gd_cue_validate(&tc, &te) == 0,
              "validate tem de aceitar um CUE de uma so track: %s", te);

        CHECK(gd_cue_to_gdi(&tfs, &tc, &tg, &te) == 0,
              "cue2gdi de um TOSEC: %s", te);
        CHECK(tg.ntracks == 3, "a imagem tem %d tracks, esperava 3",
              tg.ntracks);

        /* A track 3 tem de estar no sitio certo: LBA 45000, MODE1. */
        CHECK(tg.track[2].track == 3, "a terceira track e' a %d",
              tg.track[2].track);
        CHECK(tg.track[2].lba == 45000, "LBA = %u, esperava 45000",
              tg.track[2].lba);
        /* No GDI o campo e' o tamanho em bytes, nao o tipo ATA.
         * MODE1/2352 da-se a ler 2352. */
        CHECK(tg.track[2].sector_size == 2352,
              "sector_size = %u, esperava 2352", tg.track[2].sector_size);
        CHECK(strcmp(tg.track[2].path, "tosec.bin") == 0,
              "caminho = %s", tg.track[2].path);

        /* Tracks 1 e 2 sintetizadas, marcadas como ausentes. */
        CHECK(tg.track[0].missing == 1, "a track 1 devia estar marcada ausente");
        CHECK(tg.track[1].missing == 1, "a track 2 devia estar marcada ausente");

        /* E o resultado tem de sobreviver a uma ida e volta pelo GDI. */
        {
            gd_gdi_t rg2;
            const char *re2 = "";
            char rp[512];
            fp = fopen((snprintf(rp, sizeof rp, "%s/tosec.gdi", g_dir), rp), "wb");
            CHECK(fp != NULL, "abrir tosec.gdi");
            if (fp) { CHECK(gd_gdi_write(&tg, fp) == 0, "escrever"); fclose(fp); }
            CHECK(gd_gdi_parse(&tfs, "tosec.gdi", &rg2, &re2) == 0,
                  "reler: %s", re2);
            CHECK(rg2.ntracks == 3, "o relido tem %d tracks", rg2.ntracks);
            CHECK(rg2.track[2].lba == 45000, "LBA apos round-trip = %u",
                  rg2.track[2].lba);
        }
    }

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
