/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gd_gdi.h"
#include "gd_disc.h"

/* ------------------------------------------------------------------ */
/* Leitura de um GDI de memoria (o ficheiro e' pequeno: <16 KB)        */
/* ------------------------------------------------------------------ */

#define GD_GDI_MAX_BYTES 16384

/* Remove aspas e espacos nas pontas. Devolve o comprimento. */
static int unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
    return (int)strlen(s);
}

int gd_gdi_parse(const gd_fs_t *fs, const char *gdi_path,
                 gd_gdi_t *out, const char **err)
{
    gd_file_t *f = NULL;
    static char buf[GD_GDI_MAX_BYTES + 1];
    char *p, *end;
    long size;
    int count = 0, i;

    memset(out, 0, sizeof *out);

    if (fs->open(fs->ctx, gdi_path, &f) != 0) {
        *err = "nao consegui abrir o ficheiro .gdi";
        return -1;
    }
    size = fs->size(f);
    if (size < 0 || size >= GD_GDI_MAX_BYTES) {
        fs->close(f);
        *err = "ficheiro .gdi com tamanho invalido";
        return -1;
    }
    if (fs->read(f, 0, buf, (uint32_t)size) != 0) {
        fs->close(f);
        *err = "falha de leitura do .gdi";
        return -1;
    }
    fs->close(f);
    buf[size] = '\0';

    /* Token 1: numero de tracks. */
    count = (int)strtol(buf, &end, 10);
    if (end == buf) { *err = "o .gdi nao comeca por um numero de tracks"; return -1; }
    if (count < 3 || count > GD_MAX_TRACKS) {
        *err = "numero de tracks fora de 3..99";
        return -1;
    }
    p = end;

    for (i = 0; i < count; i++) {
        gd_gdi_track_t *t = &out->track[i];
        char namebuf[GD_PATH_MAX];
        long long lba, ssize, off, ctrl;
        long trk;

        trk = strtol(p, &end, 10); if (end == p) { *err = "falta TRACK"; return -1; } p = end;
        lba = strtoll(p, &end, 10); if (end == p) { *err = "falta LBA"; return -1; } p = end;
        ctrl = strtoll(p, &end, 10);
        if (end == p) { *err = "falta CTRL"; return -1; }
        p = end;
        ssize = strtoll(p, &end, 10); if (end == p) { *err = "falta SSIZE"; return -1; } p = end;

        /* Nome do ficheiro: ate whitespace, ou entre aspas. */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') {
            char *q = ++p;
            while (*q && *q != '"') q++;
            {
                size_t n = (size_t)(q - p);
                if (n >= sizeof namebuf) n = sizeof namebuf - 1;
                memcpy(namebuf, p, n);
                namebuf[n] = '\0';
            }
            p = (*q == '"') ? q + 1 : q;
        } else {
            char *q = p;
            while (*q && !isspace((unsigned char)*q)) q++;
            {
                size_t n = (size_t)(q - p);
                if (n >= sizeof namebuf) n = sizeof namebuf - 1;
                memcpy(namebuf, p, n);
                namebuf[n] = '\0';
            }
            p = q;
        }
        unquote(namebuf);

        off = strtoll(p, &end, 10); if (end == p) { *err = "falta OFFSET"; return -1; } p = end;

        t->track       = (int)trk;
        t->ctrl        = (uint8_t)ctrl;
        t->lba         = (uint32_t)lba;
        t->start_fad   = GD_LBA_TO_FAD(t->lba);
        t->sector_size = (uint32_t)ssize;
        t->offset      = (int32_t)off;
        t->missing     = (ssize == 0);
        snprintf(t->path, sizeof t->path, "%s", namebuf);
    }

    out->ntracks = count;
    return 0;
}

int gd_gdi_validate(const gd_gdi_t *g, const char **err)
{
    int i;
    for (i = 0; i < g->ntracks; i++) {
        if (g->track[i].track < 1 || g->track[i].track > g->ntracks) {
            *err = "numero de track fora de 1..track_count"; return -1;
        }
    }
    /* Um GD-ROM comeca sempre com audio, dados, dados. */
    for (i = 0; i < g->ntracks; i++) {
        uint8_t c = 0;
        /* CTRL foi lido acima e validado contra 0 e 4. */
        c = g->track[i].ctrl;
        if (c != GD_CTRL_AUDIO && c != GD_CTRL_DATA) {
            *err = "CTRL tem de ser 0 (audio) ou 4 (dados)"; return -1;
        }
    }
    if (g->track[0].track == 1 && g->track[0].ctrl != GD_CTRL_DATA) {
        *err = "a track 1 tem de ser de dados"; return -1;
    }
    if (g->track[1].track == 2 && g->track[1].ctrl != GD_CTRL_AUDIO) {
        *err = "a track 2 tem de ser de audio"; return -1;
    }
    if (g->track[2].track == 3) {
        if (g->track[2].ctrl != GD_CTRL_DATA) {
            *err = "a track 3 tem de ser de dados"; return -1;
        }
        if (g->track[2].lba != 45000) {
            *err = "a track 3 tem de comecar no LBA 45000"; return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Calculo de end_fad e construcao da gd_disc_t                       */
/* ------------------------------------------------------------------ */

/*
 * EndFAD = StartFAD + (fileSize - OFFSET) / SSIZE - 1
 *
 * Atencao ao OFFSET: pode ser negativo, e `fileSize - OFFSET` tem de ser
 * feito em aritmetica assinada antes de se dividir.
 */
static int compute_ends(const gd_fs_t *fs, gd_gdi_t *g, const char **err)
{
    int i;
    for (i = 0; i < g->ntracks; i++) {
        gd_gdi_track_t *t = &g->track[i];
        gd_file_t *f = NULL;
        long size;
        long long nsect;

        if (t->missing) { t->end_fad = t->start_fad; continue; }

        if (fs->open(fs->ctx, t->path, &f) != 0) {
            *err = "nao consegui abrir um dos ficheiros .bin referidos no .gdi";
            return -1;
        }
        size = fs->size(f);
        fs->close(f);
        if (size <= 0) { *err = "ficheiro .bin vazio"; return -1; }

        nsect = ((long long)size - (long long)t->offset) / (long long)t->sector_size;
        if (nsect <= 0) { *err = "ficheiro .bin menor que o offset declarado"; return -1; }
        t->end_fad = t->start_fad + (uint32_t)(nsect - 1);
    }
    return 0;
}

/* Mapa de lead-out por area, como o hardware faz. Spec, e o GetToc do
 * Flycast: area LD devolve EndFAD(track 2) + 1; area HD devolve 549300. */
static uint32_t leadout_of_area(const gd_gdi_t *g, int area)
{
    if (area == GD_AREA_HIGH_DENSITY) return GD_HD_LEADOUT_FAD;
    if (g->ntracks >= 2) return g->track[1].end_fad + 1;
    return GD_LD_LEADOUT_FAD;
}

static int gdi_first_track(const gd_gdi_t *g, int area)
{
    (void)g;
    return (area == GD_AREA_HIGH_DENSITY) ? GD_AREA_HD_FIRST_TRACK
                                          : GD_AREA_LD_FIRST_TRACK;
}

static int gdi_last_track(const gd_gdi_t *g, int area)
{
    if (area == GD_AREA_SINGLE_DENSITY) return GD_AREA_LD_LAST_TRACK;
    return g->ntracks;
}

static void gdi_fill_toc(gd_disc_t *d, int area, uint8_t *out)
{
    const gd_gdi_t *g = &((gd_gdi_disc_t *)d->priv)->gdi;
    int lo = gdi_first_track(g, area);
    int hi = gdi_last_track(g, area);
    int i;

    memset(out, 0xff, GD_TOC_SIZE);

    for (i = lo; i <= hi && i <= GD_MAX_TRACKS; i++) {
        const gd_gdi_track_t *t = &g->track[i - 1];
        uint8_t *p = out + (i - 1) * 4;
        p[0] = (uint8_t)(((t->ctrl & 0x0f) << 4) | GD_ADR_Q);
        p[1] = (uint8_t)(t->start_fad >> 16);
        p[2] = (uint8_t)(t->start_fad >> 8);
        p[3] = (uint8_t)(t->start_fad);
    }

    /* Primeira e ultima track: o campo FAD leva o NUMERO da track. */
    out[396] = 0x01;
    out[397] = (uint8_t)(lo >> 8);
    out[398] = (uint8_t)lo;
    out[399] = 0x00;
    out[400] = 0x01;
    out[401] = (uint8_t)(hi >> 8);
    out[402] = (uint8_t)hi;
    out[403] = 0x00;

    {
        uint32_t lo_fad = leadout_of_area(g, area);
        out[404] = (uint8_t)(((GD_CTRL_DATA & 0x0f) << 4) | GD_ADR_Q);
        out[405] = (uint8_t)(lo_fad >> 16);
        out[406] = (uint8_t)(lo_fad >> 8);
        out[407] = (uint8_t)(lo_fad);
    }
}

static void gdi_fill_ses(gd_disc_t *d, int session, uint8_t *out)
{
    const gd_gdi_t *g = &((gd_gdi_disc_t *)d->priv)->gdi;
    out[0] = 0x02;   /* substituido pelo estado da unidade */
    out[1] = 0x00;
    if (session == 0) {
        out[2] = 2;   /* ver gd_format.h: 2 AREAS, nao 2 sessoes */
        {
            uint32_t e = GD_HD_LEADOUT_FAD;
            out[3] = (uint8_t)(e >> 16);
            out[4] = (uint8_t)(e >> 8);
            out[5] = (uint8_t)e;
        }
    } else if (session == 1) {
        out[2] = GD_AREA_LD_FIRST_TRACK;
        out[3] = (uint8_t)(g->track[0].start_fad >> 16);
        out[4] = (uint8_t)(g->track[0].start_fad >> 8);
        out[5] = (uint8_t)(g->track[0].start_fad);
    } else {
        out[2] = GD_AREA_HD_FIRST_TRACK;
        out[3] = (uint8_t)(g->track[2].start_fad >> 16);
        out[4] = (uint8_t)(g->track[2].start_fad >> 8);
        out[5] = (uint8_t)(g->track[2].start_fad);
    }
}

static uint8_t gdi_track_of_fad(gd_disc_t *d, uint32_t fad)
{
    const gd_gdi_t *g = &((gd_gdi_disc_t *)d->priv)->gdi;
    int i;
    for (i = 0; i < g->ntracks; i++) {
        if (fad >= g->track[i].start_fad && fad <= g->track[i].end_fad)
            return g->track[i].track;
    }
    return 0xaa;
}

static void gdi_track_control(gd_disc_t *d, uint8_t track,
                              uint8_t *control, uint8_t *adr)
{
    const gd_gdi_t *g = &((gd_gdi_disc_t *)d->priv)->gdi;
    if (track >= 1 && track <= g->ntracks) {
        *control = g->track[track - 1].ctrl;
        *adr     = GD_ADR_Q;
    } else {
        *control = GD_CTRL_DATA;
        *adr     = GD_ADR_Q;
    }
}

int gd_gdi_read_sectors(gd_disc_t *d, uint32_t fad, uint32_t n,
                        uint32_t sector_size, void *dst)
{
    gd_gdi_disc_t *self = (gd_gdi_disc_t *)d->priv;
    return self->rd.read_sectors(self->rd.ctx, fad, n, sector_size, dst);
}

int gd_gdi_open_disc(const gd_fs_t *fs, const char *gdi_path,
                     gd_gdi_disc_t *out, const char **err)
{
    const char *e = "";
    int i;

    memset(out, 0, sizeof *out);

    if (gd_gdi_parse(fs, gdi_path, &out->gdi, &e) != 0) { *err = e; return -1; }
    if (gd_gdi_validate(&out->gdi, &e) != 0)             { *err = e; return -1; }
    if (compute_ends(fs, &out->gdi, &e) != 0)            { *err = e; return -1; }

    out->disc.present      = 1;
    out->disc.disc_format  = GD_FORMAT_GDROM;
    out->disc.leadout_fad  = GD_HD_LEADOUT_FAD;
    out->disc.num_tracks   = (uint8_t)out->gdi.ntracks;
    out->disc.num_sessions = 2;
    out->disc.fill_toc       = gdi_fill_toc;
    out->disc.fill_session_info = gdi_fill_ses;
    out->disc.track_of_fad  = gdi_track_of_fad;
    out->disc.track_control = gdi_track_control;
    out->disc.read_sectors  = gd_gdi_read_sectors;
    out->disc.priv          = out;

    for (i = 0; i < out->gdi.ntracks; i++) {
        out->disc.toc[i].fad     = out->gdi.track[i].start_fad;
        out->disc.toc[i].control = out->gdi.track[i].ctrl;
        out->disc.toc[i].adr     = GD_ADR_Q;
    }
    return 0;
}

int gd_gdi_write(const gd_gdi_t *g, FILE *f)
{
    int i;
    if (!f) return -1;
    if (fprintf(f, "%d\n", g->ntracks) < 0) return -1;
    for (i = 0; i < g->ntracks; i++) {
        const gd_gdi_track_t *t = &g->track[i];
        int n;
        if (t->missing) {
            /* Convencao nullDC: SSIZE 0 e ficheiro `none`. E' assim
             * que um dump parcial se representa sem inventar dados. */
            n = fprintf(f, "%d 0 0 0 none 0\n", t->track);
        } else {
            n = fprintf(f, "%d %u %u %u \"%s\" %d\n",
                        t->track, t->lba, t->ctrl, t->sector_size,
                        t->path, t->offset);
        }
        if (n < 0) return -1;
    }
    return 0;
}
