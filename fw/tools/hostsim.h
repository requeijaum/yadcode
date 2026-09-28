/* SPDX-License-Identifier: Apache-2.0 */
/*
 * hostsim.h - Simulador de host G1.
 *
 * Reproduz o lado do HOLLY: coloca os sinais CS0/CS1/DA e strobes RD/WR
 * nos registos do device, e respeita a sequencia da spec seca 7.1.
 *
 * Este e o instrumento que substitui ter um Dreamcast na bancada para
 * validar a camada de protocolo. Nao substitui hardware para validar
 * timing electrico -- para isso ver doc 03.
 */
#ifndef HOSTSIM_H
#define HOSTSIM_H

#include <stdint.h>
#include "gd_taskfile.h"
#include "gd_spec.h"

#define HOSTSIM_MAX_TRACE 4096

typedef struct {
    uint8_t  op;        /* 'W' control, 'R' control, 'W' data, 'R' data, 'C' command */
    uint8_t  reg;
    uint16_t value;
    uint16_t bytecount; /* Byte Count registado neste ponto */
    uint32_t micros;
} hostsim_ev_t;

typedef struct {
    gd_device_t  dev;
    const gd_hal_t *hal;

    /* Instrumentacao */
    hostsim_ev_t trace[HOSTSIM_MAX_TRACE];
    int          ntrace;
    uint32_t     now_us;
    int          irq_seen;
    /*
     * Quantas vezes o INTRQ foi assertado. Necessario porque
     * hostsim_spi_command() le o Status no fim - o que limpa o IRQ - e
     * portanto o nivel corrente nao diz se houve assert.
     */
    int          irq_asserts;

    /* Contadores */
    int          n_packets;
    int          n_unknown_cmd;
} hostsim_t;

/* Cria o simulador com o device apontado para `disc`. */
void hostsim_init(hostsim_t *hs, gd_disc_t *disc);

/* Acoes de um host. Todas devolvem o valor lido (quando aplicavel). */
uint8_t  hostsim_write_control(hostsim_t *hs, uint8_t reg, uint8_t value);
uint8_t  hostsim_read_control(hostsim_t *hs, uint8_t reg);

/*
 * Le o Alternate Status. Ao contrario de Status, NAO limpa o INTRQ -
 * a spec diz que o AltStatus "does not clear DMA status information when
 * it is accessed", e varios jogos dependem de sondar o estado sem
 * cancelar o IRQ.
 */
uint8_t  hostsim_read_altstatus(hostsim_t *hs);
void     hostsim_write_data(hostsim_t *hs, uint16_t w);
uint16_t hostsim_read_data(hostsim_t *hs);
void     hostsim_write_command(hostsim_t *hs, uint8_t cmd);

/*
 * Executa um packet SPI completo: escreve 0xA0, envia as 6 palavras, e
 * consome a resposta se houver. Devolve o numero de bytes de resposta
 * (0 se nao houve). `out` recebe a resposta; pode ser NULL.
 *
 * E' este o que os testes usam para a maioria dos casos.
 */
uint32_t hostsim_spi_command(hostsim_t *hs, const uint8_t pkt[GD_PKT_SIZE],
                             uint8_t *out, uint32_t outcap);

/* Imprime o trace. */
void hostsim_dump(const hostsim_t *hs);

#endif /* HOSTSIM_H */
