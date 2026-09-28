/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_fs.h - Abstracao de sistema de ficheiros.
 *
 * O parser de imagem tem de correr em tres sitios: o simulador de host
 * (PC, testes), a ferramenta de conversao (PC) e o firmware (RP2350 com
 * SDIO). Se amarrarmos a stdio, o firmware fica impossivel.
 *
 * Por isso o acesso a ficheiros e' um vtable, e ha' um backend para
 * stdio que o PC usa e outro para o SD que o firmware provide.
 */
#ifndef GD_FS_H
#define GD_FS_H

#include <stdint.h>
#include <stddef.h>

typedef struct gd_file gd_file_t;

typedef struct {
    void *ctx;

    /* Abre para leitura. Devolve 0 em sucesso, -1 em erro. */
    int  (*open)(void *ctx, const char *path, gd_file_t **out);
    void (*close)(gd_file_t *f);

    /* Tamanho em bytes, ou -1. */
    long (*size)(gd_file_t *f);

    /* Le `n` bytes a partir de `off`. Devolve 0 em sucesso. */
    int  (*read)(gd_file_t *f, uint32_t off, void *dst, uint32_t n);

    /* Existe? Devolve 1 se existe. */
    int  (*exists)(void *ctx, const char *path);
} gd_fs_t;

/*
 * Backend sobre stdio (PC). `root` e' a pasta a partir da qual os
 * caminhos relativos do GDI/CUE sao resolvidos. O contexto fica em
 * `fs.ctx`, por isso o chamador faz:
 *
 *     gd_fs_t fs = gd_fs_stdio("/jogo");
 *     fs.open(fs.ctx, "track03.bin", &f);
 */
gd_fs_t gd_fs_stdio(const char *root);

#endif /* GD_FS_H */
