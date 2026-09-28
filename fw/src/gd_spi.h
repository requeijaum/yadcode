/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_spi.h - Camada L3: os comandos da Sega Packet Interface
 *            (spec, Tabela 6.1).
 */
#ifndef GD_SPI_H
#define GD_SPI_H

#include <stdint.h>
#include "gd_taskfile.h"

/* Executa um packet de 12 bytes recebido. Chamado pela camada L2. */
void gd_spi_execute(gd_device_t *dev, const uint8_t *pkt);

/* Comando ATA 0xA1 (Identify Device) - sempre PIO. */
void gd_spi_identify(gd_device_t *dev);

/* Le o Byte Count carregado pelo host, Little-Endian, e devolve o
 * Allocation Length do packet. Ver Tabela 6.1. */
uint16_t gd_spi_alloc_len(const uint8_t *pkt, int wide);

/* Converte MSF (min, seg, frame) para FAD. Spec seca 8.2. */
uint32_t gd_spi_msf_to_fad(uint8_t m, uint8_t s, uint8_t f);

/* Le o FAD de um packet CD_READ, respeitando o Parameter Type. */
uint32_t gd_spi_cdread_fad(const uint8_t *pkt);

/* Acesso ao estado que REQ_STAT e CD-DA precisam. */
int      gd_spi_disc_readable(const gd_device_t *dev);
void     gd_spi_clear_error(gd_device_t *dev);
uint32_t gd_spi_current_fad(const gd_device_t *dev);
uint8_t  gd_spi_track_of_fad(const gd_disc_t *d, uint32_t fad);

/* Estado do produtor de CD_READ, exposto para os testes. */
typedef struct {
    uint32_t fad;        /* proximo FAD a servir                     */
    uint32_t remaining;  /* sectores por servir                      */
    uint32_t sector_size;
} gd_cdread_state_t;

gd_cdread_state_t *gd_spi_cdread_state(void);

/* Buffers internos expostos para os testes. */
extern uint8_t gd_spi_buf[GD_TOC_SIZE];
extern uint32_t gd_spi_buf_len;

#endif /* GD_SPI_H */
