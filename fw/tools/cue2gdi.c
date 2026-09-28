/* SPDX-License-Identifier: Apache-2.0 */
/*
 * cue2gdi - Converte um dump Redump (CUE + BIN) em GDI.
 *
 * POR QUE ISTO EXISTE
 * -------------------
 * O GDI e' o formato de runtime dos ODEs: e' uma lista de LBA por track,
 * e o mapeamento FAD -> ficheiro e' directo. O CUE guarda pregap e
 * indices, que o GDI nao sabe representar.
 *
 * Portanto CUE entra, GDI sai, e o utilizador fica com um conjunto de
 * ficheiros que o GDEMU, o MODE, o iceGDROM e este ODE leem.
 *
 * A alternativa e usar o gdidrop (BSD-2) ou o RedumpCUE2GDI (BSD-2).
 * Esta ferramenta existe para o fluxo ser fechad com uma so dependencia
 * e, mais importante, porque suporta o formato TOSEC com PREGAP, que o
 * gdidrop nao trata e o Flycast nao descodifica.
 *
 * USO
 * ---
 *   cue2gdi <ficheiro.cue> [-o saida.gdi] [-v]
 *
 * O GDI de saida e' escrito na pasta do CUE, para que os caminhos
 * relativos dos .bin continuem a resolver-se.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gd_cue.h"
#include "gd_gdi.h"
#include "gd_fs.h"

static void usage(const char *argv0)
{
    fprintf(stderr,
        "uso: %s <ficheiro.cue> [-o saida.gdi] [-v]\n"
        "\n"
        "  Converte um dump Redump (CUE + BIN) em GDI.\n"
        "  Suporta Multi-Cue do Redump e TOSEC com PREGAP.\n"
        "\n"
        "  -o  nome do GDI de saida (por omissao, mesmo nome com .gdi)\n"
        "  -v  mostra o que foi convertido\n", argv0);
}

/* "pasta/ficheiro.cue" -> "pasta" */
static void dirname_of(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, cap, "."); return; }
    {
        size_t n = (size_t)(slash - path);
        if (n == 0) { snprintf(out, cap, "/"); return; }
        if (n + 1 > cap) n = cap - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    }
}

static void basename_of(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    snprintf(out, cap, "%s", slash ? slash + 1 : path);
}

int main(int argc, char **argv)
{
    const char *cue_path = NULL, *out_path = NULL;
    int verbose = 0, i;
    char dir[512], base[256], out[1024];
    gd_fs_t fs;
    gd_cue_t c;
    gd_gdi_t g;
    const char *err = "";
    FILE *f;
    int k;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        } else if (argv[i][0] == '-') { usage(argv[0]); return 2; }
        else cue_path = argv[i];
    }
    if (!cue_path) { usage(argv[0]); return 2; }

    dirname_of(cue_path, dir, sizeof dir);
    fs = gd_fs_stdio(dir);

    basename_of(cue_path, base, sizeof base);
    if (gd_cue_parse(&fs, base, &c, &err) != 0) {
        fprintf(stderr, "cue2gdi: %s: %s\n", cue_path, err);
        return 1;
    }
    if (!c.is_gdrom) {
        fprintf(stderr,
            "cue2gdi: %s nao esta marcado como GD-ROM.\n"
            "  Sem `REM SINGLE-DENSITY AREA` nem `REM HIGH-DENSITY AREA`,\n"
            "  nem um PREGAP 10:00:00 / 10:02:00, nao ha como saber que e'\n"
            "  um GD-ROM e nao um CD normal. Nao vou adivinhar.\n", cue_path);
        return 1;
    }
    if (gd_cue_validate(&c, &err) != 0) {
        fprintf(stderr, "cue2gdi: %s: %s\n", cue_path, err);
        return 1;
    }
    if (gd_cue_to_gdi(&fs, &c, &g, &err) != 0) {
        fprintf(stderr, "cue2gdi: %s: %s\n", cue_path, err);
        return 1;
    }

    basename_of(cue_path, base, sizeof base);
    {
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
    }
    if (out_path) snprintf(out, sizeof out, "%s", out_path);
    else          snprintf(out, sizeof out, "%s/%s.gdi", dir, base);

    f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "cue2gdi: nao consegui escrever %s\n", out); return 1; }
    /* A escrita vive na biblioteca, para a ferramenta e os testes
     * partilharem exactamente o mesmo formato. */
    if (gd_gdi_write(&g, f) != 0) {
        fprintf(stderr, "cue2gdi: falha de escrita em %s\n", out);
        fclose(f);
        return 1;
    }
    fclose(f);

    for (k = 0; k < g.ntracks; k++) {
        const gd_gdi_track_t *t = &g.track[k];
        if (verbose) {
            if (t->missing) {
                printf("  %-3d ausente\n", t->track);
            } else {
                printf("  %-3d LBA %-7u FAD %-7u %-5s %5u  %s\n",
                       t->track, t->lba, t->start_fad,
                       t->ctrl == GD_CTRL_DATA ? "dados" : "audio",
                       t->sector_size, t->path);
            }
        }
    }

    printf("cue2gdi: %s -> %s (%d tracks)\n", cue_path, out, g.ntracks);
    if (g.track[0].missing || g.ntracks < 3) {
        printf("  nota: tracks 1 e 2 estao ausentes. O dump e' so de alta\n"
               "  densidade; o boot e' possivel mas a area de baixa densidade\n"
               "  fica sem dados.\n");
    }
    return 0;
}
