/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gd_fs.h"

/*
 * O contexto (`ctx`) e' a raiz contra a qual os caminhos relativos do
 * GDI/CUE sao resolvidos. Nao e' preciso nenhum global: o chamador faz
 *
 *     gd_fs_t fs = gd_fs_stdio("/home/user/jogo");
 *     fs.open(fs.ctx, "track03.bin", &f);
 */
struct gd_file {
    FILE *fp;
    long  size;
};

static char *join(const char *root, const char *path)
{
    size_t n;
    char *out;
    if (!root || !*root) return strdup(path);
    n = strlen(root) + strlen(path) + 2;
    out = malloc(n);
    if (!out) return NULL;
    snprintf(out, n, "%s/%s", root, path);
    return out;
}

static int s_open(void *ctx, const char *path, gd_file_t **out)
{
    char *full = join((const char *)ctx, path);
    struct gd_file *f;
    FILE *fp;

    if (!full) return -1;
    fp = fopen(full, "rb");
    free(full);
    if (!fp) return -1;

    f = calloc(1, sizeof *f);
    if (!f) { fclose(fp); return -1; }
    f->fp = fp;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); free(f); return -1; }
    f->size = ftell(fp);
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); free(f); return -1; }

    *out = f;
    return 0;
}

static void s_close(gd_file_t *f)
{
    if (!f) return;
    fclose(f->fp);
    free(f);
}

static long s_size(gd_file_t *f) { return f ? f->size : -1; }

static int s_read(gd_file_t *f, uint32_t off, void *dst, uint32_t n)
{
    if (!f) return -1;
    if (fseek(f->fp, (long)off, SEEK_SET) != 0) return -1;
    if (fread(dst, 1, n, f->fp) != n) return -1;
    return 0;
}

static int s_exists(void *ctx, const char *path)
{
    char *full = join((const char *)ctx, path);
    FILE *fp;
    if (!full) return 0;
    fp = fopen(full, "rb");
    free(full);
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static const struct {
    int  (*open)(void *, const char *, gd_file_t **);
    void (*close)(gd_file_t *);
    long (*size)(gd_file_t *);
    int  (*read)(gd_file_t *, uint32_t, void *, uint32_t);
    int  (*exists)(void *, const char *);
} stdio_ops = { s_open, s_close, s_size, s_read, s_exists };

gd_fs_t gd_fs_stdio(const char *root)
{
    gd_fs_t fs;
    fs.ctx    = (void *)root;
    fs.open   = stdio_ops.open;
    fs.close  = stdio_ops.close;
    fs.size   = stdio_ops.size;
    fs.read   = stdio_ops.read;
    fs.exists = stdio_ops.exists;
    return fs;
}
