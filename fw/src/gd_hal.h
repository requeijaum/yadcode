/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_hal.h - Interface de hardware, portavel.
 *
 * O nucleo do emulador (task file + SPI Sega) nao sabe se corre num RP2350
 * ou num PC. Tudo o que toca no mundo exterior passa por esta interface.
 *
 * Esto e deliberado: e o que permite correr a maquina de estados da spec
 * seca 7.1 em qualquer PC, e e o que permite trocar o backend PIO por um
 * loopback sem tocar no protocolo.
 */
#ifndef GD_HAL_H
#define GD_HAL_H

#include <stdint.h>
#include "gd_spec.h"

typedef struct gd_device gd_device_t;

/*
 * Funcoes implementadas pelo backend.
 *
 * Notacao do ciclo de vida de um comando, da spec seca 7.1:
 *
 *   1. host escreve 0xA0 no registo Command
 *   2. -> o backend chama gd_on_command()
 *   3. o nucleo sinaliza "aceito o packet" pondo CoD=1, IO=0, DRQ=1
 *   4. -> o backend chama gd_on_packet() com as 6 palavras
 *   5. o nucleo sinaliza "tenho dados" pondo IO=1, CoD=0, DRQ=1 e
 *      carregando o Byte Count
 *   6. -> o backend chama gd_on_data_in() repetidamente para ir buscar os
 *      dados, e gd_on_data_in_done() no fim
 */
typedef struct {
    /* Relogio monotonico em microssegundos. Usado para timeouts. */
    uint32_t (*micros)(gd_device_t *dev);

    /* O host escreveu um dos 7 registos de controlo. */
    void (*write_control)(gd_device_t *dev, uint8_t reg, uint8_t value);

    /* O host escreveu no registo Data (16 bits). */
    void (*write_data)(gd_device_t *dev, uint16_t value);

    /*
     * O host escreveu no registo Command. O nucleo decide o que fazer.
     * Pode ser chamado com o device em BSY (ex. NOP ou Soft Reset, que a
     * spec seca 3.3.1.2 permite explicitamente).
     */
    void (*write_command)(gd_device_t *dev, uint8_t command);

    /* O host leu um registo. Leitura de Status limpa o INTRQ. */
    uint8_t (*read_control)(gd_device_t *dev, uint8_t reg);

    /* O host leu do registo Data. */
    uint16_t (*read_data)(gd_device_t *dev);

    /* Disparado pelo nucleo quando o comando esta pronto. */
    void (*assert_irq)(gd_device_t *dev, int on);

    /* Disparado pelo nucleo quando ha uma fase de dados de entrada. */
    void (*start_data_in)(gd_device_t *dev, uint16_t bytecount);

    /* Pedido de `n` palavras de 16 bits ao backend. */
    void (*read_data_block)(gd_device_t *dev, uint16_t *dst, uint32_t n);

    /* Notifica o fim da fase de dados. */
    void (*end_data_in)(gd_device_t *dev);

    /* O nucleo precisa de zeroar um buffer. */
    void (*memzero)(void *dst, size_t n);
} gd_hal_t;

#endif /* GD_HAL_H */
