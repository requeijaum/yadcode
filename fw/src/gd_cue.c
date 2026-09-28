/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gd_cue.h"

#define CUE_MAX_BYTES 65536   /* um CUE de Dreamcast tem <64 KB */

int gd_cue_parse_time(const char *s, uint32_t *frames)
{
    unsigned m = 0, sec = 0, f = 0;
    int n = 0;
    if (sscanf(s, "%u:%u:%u%n", &m, &sec, &f, &n) != 3) return -1;
    if (n == 0) return -1;
    *frames = m * 60u * 75u + sec * 75u + f;
    return 0;
}

/*
 * Extrai o argumento seguinte de uma linha de CUE: uma string entre
 * aspas, ou um token sem espacos. Avanca o ponteiro para o proximo.
 *
 * Nao se pode usar sscanf para isto: `FILE "Game (Track 1).bin" BINARY`
 * parte-se em a1="\"Game" e a2="(Track", e o nome do ficheiro perde-se.
 * O Redump usa sempre aspas precisamente por causa dos espacos.
 */
static int arg_of(const char **sp, char *out, size_t cap)
{
    const char *s = *sp;
    size_t n = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return -1;

    if (*s == '"') {
        s++;
        while (*s && *s != '"') { if (n + 1 < cap) out[n++] = *s; s++; }
        if (*s == '"') s++;
    } else {
        while (*s && *s != ' ' && *s != '\t') { if (n + 1 < cap) out[n++] = *s; s++; }
    }
    out[n] = '\0';
    *sp = s;
    return 0;
}

static gd_cue_sector_t sector_of(const char *mode, uint32_t *size)
{
    if (!strcmp(mode, "AUDIO"))     { *size = 2352; return GD_SECT_AUDIO; }
    if (!strcmp(mode, "MODE1/2048")) { *size = 2048; return GD_SECT_MODE1_2048; }
    if (!strcmp(mode, "MODE1/2352")) { *size = 2352; return GD_SECT_MODE1_2352; }
    if (!strcmp(mode, "MODE2/2336")) { *size = 2336; return GD_SECT_MODE2_2336; }
    if (!strcmp(mode, "MODE2/2352")) { *size = 2352; return GD_SECT_MODE2_2352; }
    if (!strcmp(mode, "CDI/2336"))   { *size = 2336; return GD_SECT_CDI_2336; }
    if (!strcmp(mode, "CDI/2352"))   { *size = 2352; return GD_SECT_CDI_2352; }
    /* CDG aparece como modo de sector em alguns sheets. */
    if (!strcmp(mode, "CDG"))        { *size = 2352; return GD_SECT_AUDIO; }
    *size = 0;
    return GD_SECT_UNKNOWN;
}

int gd_cue_parse(const gd_fs_t *fs, const char *cue_path,
                 gd_cue_t *out, const char **err)
{
    gd_file_t *f = NULL;
    static char buf[CUE_MAX_BYTES + 1];
    char *line, *save;
    long size;
    char cur_file[GD_PATH_MAX] = "";
    int cur_track = -1;         /* indice em out->track[] */
    int in_track = 0;
    uint32_t cur_fad = 0;       /* FAD corrente, construido ao longo */

    memset(out, 0, sizeof *out);
    out->catalog_len = 0;
    {
        int i;
        for (i = 0; i < GD_MAX_TRACKS; i++) out->track[i].index0 = UINT32_MAX;
    }

    if (fs->open(fs->ctx, cue_path, &f) != 0) {
        *err = "nao consegui abrir o ficheiro .cue";
        return -1;
    }
    size = fs->size(f);
    if (size < 0 || size >= CUE_MAX_BYTES) {
        fs->close(f);
        *err = "ficheiro .cue com tamanho invalido";
        return -1;
    }
    if (fs->read(f, 0, buf, (uint32_t)size) != 0) {
        fs->close(f);
        *err = "falha de leitura do .cue";
        return -1;
    }
    fs->close(f);
    buf[size] = '\0';

    /* Troca CRLF por LF para o strtok_r nao deixar \r nos tokens. */
    {
        char *p;
        for (p = buf; *p; p++) if (*p == '\r') *p = '\n';
    }

    for (line = strtok_r(buf, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char cmd[32] = "", a1[GD_PATH_MAX] = "", a2[64] = "";
        const char *rest = line;

        /* Keyword: primeiro token sem aspas; `rest` fica a seguir. */
        if (arg_of(&rest, cmd, sizeof cmd) != 0) continue;

        if (!strcmp(cmd, "REM")) {
            if (!strstr(line, "HIGH-DENSITY") && !strstr(line, "HIGH_DENSITY")) {
                if (strstr(line, "SINGLE-DENSITY") ||
                    strstr(line, "SINGLE_DENSITY")) {
                    /* A area de baixa densidade comeca em LBA 0 =
                     * FAD 150. */
                    cur_fad = GD_LD_TRACK1_FAD + 150u;
                    out->is_gdrom = 1;
                }
                /* Um REM Session num GD e' errado; ignoramos. */
            } else {
                out->saw_high_density = 1;
                out->is_gdrom = 1;
                cur_fad = GD_HD_TRACK3_FAD;   /* 45150, o INDEX 01 */
            }
            continue;
        }

        if (!strcmp(cmd, "FILE")) {
            if (arg_of(&rest, a1, sizeof a1) != 0) { *err = "FILE sem nome"; return -1; }
            snprintf(cur_file, sizeof cur_file, "%s", a1);
            in_track = 0;
            continue;
        }

        if (!strcmp(cmd, "CATALOG")) {
            /* O catalogo tem 128 bytes: parseia-se directamente para
             * um buffer desse tamanho para o snprintf nunca truncar. */
            char cat[sizeof out->catalog];
            if (arg_of(&rest, cat, sizeof cat) != 0) continue;
            snprintf(out->catalog, sizeof out->catalog, "%s", cat);
            out->catalog_len = 1;
            continue;
        }

        if (!strcmp(cmd, "TRACK")) {
            uint32_t ss = 0;
            gd_cue_sector_t st;
            int num;
            if (arg_of(&rest, a1, sizeof a1) != 0) { *err = "TRACK sem numero"; return -1; }
            if (arg_of(&rest, a2, sizeof a2) != 0) { *err = "TRACK sem modo"; return -1; }
            st = sector_of(a2, &ss);
            num = atoi(a1);
            if (st == GD_SECT_UNKNOWN) {
                *err = "modo de sector desconhecido no .cue";
                return -1;
            }
            if (num < 1 || num > GD_MAX_TRACKS) {
                *err = "numero de track fora de 1..99";
                return -1;
            }
            if (out->ntracks >= GD_MAX_TRACKS) {
                *err = "demasiadas tracks";
                return -1;
            }
            cur_track = out->ntracks++;
            memset(&out->track[cur_track], 0, sizeof out->track[0]);
            out->track[cur_track].index0 = UINT32_MAX;
            out->track[cur_track].track = num;
            out->track[cur_track].sector = st;
            out->track[cur_track].sector_size = ss;
            out->track[cur_track].ctrl =
                (st == GD_SECT_AUDIO) ? GD_CTRL_AUDIO : GD_CTRL_DATA;
            out->track[cur_track].file_size = -1;
            snprintf(out->track[cur_track].path, GD_PATH_MAX, "%s", cur_file);
            in_track = 1;
            continue;
        }

        if (!in_track || cur_track < 0) continue;

        if (!strcmp(cmd, "INDEX")) {
            uint32_t fr = 0;
            if (arg_of(&rest, a1, sizeof a1) != 0) continue;
            if (arg_of(&rest, a2, sizeof a2) != 0) continue;
            if (gd_cue_parse_time(a2, &fr) != 0) continue;
            if (!strcmp(a1, "00")) out->track[cur_track].index0 = fr;
            else if (!strcmp(a1, "01")) {
                out->track[cur_track].index1 = fr;
                /*
                 * start_fad NAO se resolve aqui. A posicao de uma track
                 * depende do tamanho dos ficheiros anteriores, que so
                 * se sabe depois de todos estarem lidos. Fica a marca da
                 * area; a aritmetica e' feita em gd_cue_to_gdi.
                 */
                out->track[cur_track].start_fad =
                    (cur_fad == GD_HD_TRACK3_FAD) ? GD_HD_TRACK3_FAD
                                                 : 0u;
            }
            continue;
        }

        if (!strcmp(cmd, "PREGAP")) {
            uint32_t fr = 0;
            if (arg_of(&rest, a1, sizeof a1) != 0) continue;
            if (gd_cue_parse_time(a1, &fr) != 0) continue;
            /*
             * TOSEC: o ficheiro NAO contem o pregap, e o leitor tem de o
             * sintetizar. Aceitamos 10:00:00 (45000, a head da track) e
             * 10:02:00 (45150, o INDEX 01). Nos dois casos o INDEX 01
             * fica em FAD 45150.
             */
            if (fr == 45000u || fr == 45150u) {
                out->track[cur_track].start_fad = GD_HD_TRACK3_FAD;
                out->track[cur_track].pregap_sectors =
                    GD_HD_TRACK3_FAD - fr;
                cur_fad = GD_HD_TRACK3_FAD;
                out->is_gdrom = 1;
                out->saw_high_density = 1;
            } else {
                out->track[cur_track].pregap_sectors = fr;
            }
            continue;
        }

        if (!strcmp(cmd, "POSTGAP")) {
            uint32_t fr = 0;
            if (arg_of(&rest, a1, sizeof a1) != 0) continue;
            if (gd_cue_parse_time(a1, &fr) != 0) continue;
            out->track[cur_track].postgap_sectors = fr;
            continue;
        }

        /* Qualquer outra directiva e' ignorada, como faz o Flycast. */
    }

    return 0;
}

int gd_cue_validate(const gd_cue_t *c, const char **err)
{
    int i;
    int hd;   /* a imagem tem area de alta densidade? */

    if (c->ntracks < 1) { *err = "o .cue nao tem tracks"; return -1; }
    if (!c->is_gdrom)   { *err = "o .cue nao marca o disco como GD-ROM"; return -1; }

    for (i = 0; i < c->ntracks; i++) {
        if (c->track[i].sector == GD_SECT_UNKNOWN) {
            *err = "track com modo de sector desconhecido"; return -1;
        }
    }

    /*
     * Ha dois formatos de origem, e nao e' seguro assumir o primeiro.
     *
     * A. Redump / dump completo: tem track 1 (audio), track 2 (audio) e
     *    track 3 (dados) a comecar no FAD 45150. O `gd_cue_to_gdi`
     *    sintetiza as tracks que faltam, mas so' faz sentido se a track
     *    3 existir e for de dados.
     *
     * B. TOSEC / httpd-ack: so' tem a track 3, porque um dump de
     *    alta densidade nao contem audio. Rejeitar isto como
     *    "menos de 3 tracks" partia a conversao de um formato que a
     *    doc 15 documenta como suportado.
     *
     * Distingue-se pela presenca da track 3, nao por `saw_high_density`:
     * um CUE TOSEC pode nao ter o marcador REM, e o que o identifica
     * e' o PREGAP da track 3.
     */
    hd = 0;
    for (i = 0; i < c->ntracks; i++)
        if (c->track[i].track == 3) { hd = 1; break; }

    if (!hd) {
        *err = "o .cue nao tem track 3, que e' a unica que o leitor precisa";
        return -1;
    }

    /* A track 3 tem de ser de dados e comecar onde a spec manda. */
    for (i = 0; i < c->ntracks; i++) {
        if (c->track[i].track != 3) continue;
        if (c->track[i].ctrl != GD_CTRL_DATA) {
            *err = "a track 3 tem de ser de dados"; return -1;
        }
        /* 45150 e' o inicio real dos dados de alta densidade;
         * 45000 e' o LBA depois de subtrair os 150 sectores de Pause. */
        if (c->track[i].start_fad != GD_HD_TRACK3_FAD &&
            c->track[i].start_fad != GD_HD_TRACK3_FAD - 150) {
            *err = "a track 3 tem de comecar no FAD 45150 (LBA 45000)";
            return -1;
        }
        break;
    }

    /* Se a track 2 existir, tem de ser audio: e' o que diz a spec. */
    for (i = 0; i < c->ntracks; i++)
        if (c->track[i].track == 2 && c->track[i].ctrl != GD_CTRL_AUDIO) {
            *err = "a track 2 tem de ser de audio"; return -1;
        }

    return 0;
}

/* ------------------------------------------------------------------ */
/* CUE -> GDI                                                          */
/* ------------------------------------------------------------------ */

int gd_cue_to_gdi(const gd_fs_t *fs, const gd_cue_t *c,
                  gd_gdi_t *gdi, const char **err)
{
    int i, n = 0;
    int srcidx[GD_MAX_TRACKS];   /* gdi->track[k] veio de c->track[srcidx[k]] */

    memset(gdi, 0, sizeof *gdi);

    for (i = 0; i < c->ntracks; i++) {
        const gd_cue_track_t *t = &c->track[i];
        gd_gdi_track_t *o;
        gd_file_t *f = NULL;
        long size;

        if (t->ctrl == GD_CTRL_AUDIO && i == 0) continue;  /* casos degenerados */

        if (t->ctrl == GD_CTRL_AUDIO && i == 0) continue;  /* casos degenerados */

        o = &gdi->track[n];
        o->track       = t->track;
        o->ctrl        = t->ctrl;
        o->adr         = GD_ADR_Q;
        o->sector_size = t->sector_size;
        o->offset      = 0;
        snprintf(o->path, sizeof o->path, "%s", t->path);

        /*
         * O GDI guarda o FAD do INDEX 00 como LBA, e o Redump embebe
         * os 150 sectores de Pause no inicio do ficheiro. Assim, para
         * uma track de dados, FAD = LBA + 150, e o ficheiro comeca no
         * INDEX 00.
         *
         * Se o CUE nao tem INDEX 00 (TOSEC), o ficheiro comeca no
         * INDEX 01 e por isso o LBA tem de ser o do INDEX 01.
         */
        if (fs->open(fs->ctx, t->path, &f) != 0) {
            *err = "nao consegui abrir um dos .bin referidos no .cue";
            return -1;
        }
        size = fs->size(f);
        fs->close(f);
        if (size <= 0) { *err = "ficheiro .bin vazio"; return -1; }
        o->end_fad = 0;   /* recalculado por gd_gdi_open_disc */

        o->lba       = 0;   /* resolvido abaixo, acumulando tamanhos */
        o->start_fad = 0;
        srcidx[n] = i;
        n++;
    }

    /*
     * Posicao de cada track: acumulando os tamanhos dos ficheiros.
     *
     * A semente e' 150 na area de baixa densidade e 45150 na de alta,
     * conforme o ultimo marcador REM / PREGAP visto. O ficheiro da
     * track N contem tudo desde o seu INDEX 00 ate ao INDEX 00 da track
     * N+1 (e, no Redump, com os 150 sectores de pause da seguinte
     * embebidos no fim), por isso o avanco e' simplesmente o tamanho.
     */
    {
        uint32_t cur = 150u;   /* primeira track da area de baixa densidade */
        int have_hd = 0;

        for (i = 0; i < n; i++) {
            const gd_cue_track_t *t = &c->track[srcidx[i]];
            gd_gdi_track_t *o = &gdi->track[i];
            gd_file_t *fp = NULL;
            long sz = 0;

            /* A track que arranca a area de alta densidade salta para
             * 45150 em vez de continuar a contagem. */
            if (t->track >= GD_AREA_HD_FIRST_TRACK && !have_hd) {
                cur = GD_HD_TRACK3_FAD;
                have_hd = 1;
            }

            o->start_fad = cur;
            o->lba       = (cur >= 150u) ? GD_FAD_TO_LBA(cur) : 0u;

            if (fs->open(fs->ctx, t->path, &fp) == 0) {
                sz = fs->size(fp);
                fs->close(fp);
            }
            if (sz > 0 && t->sector_size)
                cur += (uint32_t)((unsigned long long)sz / t->sector_size);
        }
    }

    /*
     * Um dump TOSEC so tem as tracks de alta densidade: o ficheiro tem
     * 1 linha, mas um GDI tem de ter pelo menos 3 (e um GD-ROM tem
     * sempre as tracks 1 e 2, ainda que vazias).
     *
     * Preenchemos as faltantes como "ausentes", a convencao que o
     * nullDC usava: `SSIZE 0` e ficheiro `none`. E' assim que o GDI do
     * Redump representa um dump parcial, e e' o que o GDEMU e o MODE
     * comem a dizer.
     */
    {
        int want = 0, k;
        for (k = 0; k < n; k++)
            if (gdi->track[k].track > want) want = gdi->track[k].track;
        if (want < 3) want = 3;

        if (n < want) {
            int found[GD_MAX_TRACKS];
            int num = 0;
            for (k = 0; k < n; k++) found[num++] = gdi->track[k].track;
            {
                gd_gdi_track_t tmp[GD_MAX_TRACKS];
                int t = 0;
                for (k = 1; k <= want; k++) {
                    int got = 0, j;
                    for (j = 0; j < num; j++)
                        if (found[j] == k) { tmp[t++] = gdi->track[j]; got = 1; break; }
                    if (got) continue;
                    memset(&tmp[t], 0, sizeof tmp[t]);
                    tmp[t].track = k;
                    tmp[t].ctrl  = (k == 2) ? GD_CTRL_AUDIO : GD_CTRL_DATA;
                    tmp[t].sector_size = 0;
                    tmp[t].missing = 1;
                    tmp[t].lba = 0;
                    tmp[t].start_fad = 150;
                    snprintf(tmp[t].path, GD_PATH_MAX, "none");
                    t++;
                }
                memcpy(gdi->track, tmp, (size_t)t * sizeof tmp[0]);
                n = t;
            }
        }
        gdi->ntracks = n;
    }

    /*
     * Nao se exige 3 tracks aqui. Um dump TOSEC so' tem a track 3, e o
     * bloco acima sintetiza as tracks 1 e 2 com `missing = 1` para que
     * o leitor tenha os indices completos. Exigir 3 aqui desfazia esse
     * trabalho e voltava a bloquear o formato de origem mais comum.
     */
    if (gdi->ntracks < 1) {
        *err = "a imagem convertida ficou sem tracks";
        return -1;
    }
    return 0;
}
