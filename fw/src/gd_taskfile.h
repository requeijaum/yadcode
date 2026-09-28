/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_taskfile.h - Camada L2: os registos do task file e a maquina de
 *                 estados do fluxo de comandos da spec, seca 7.1.
 */
#ifndef GD_TASKFILE_H
#define GD_TASKFILE_H

#include <stdint.h>
#include "gd_spec.h"
#include "gd_hal.h"
#include "gd_disc.h"
#include "gd_cdda.h"

/* Indices dos registos de 8 bits. O registo Data e separado porque e
 * o unico de 16 bits (spec seca 3.4: "Except for the data register, all
 * registers are read and written in byte units (8 bits). The data
 * register is always accessed in 16-bit words."). */
enum {
    GD_R_ERROR = 0,
    GD_R_FEATURES,
    GD_R_INTREASON,
    GD_R_SECTORNUM,
    GD_R_BYTECOUNTL,
    GD_R_BYTECOUNTH,
    GD_R_DRIVESEL,
    GD_R_STATUS,
    GD_R_COUNT
};

typedef enum {
    GD_PHASE_IDLE = 0,     /* a espera de comando                       */
    GD_PHASE_PACKET_RECV,  /* CoD=1, DRQ=1: o host escreve 6 palavras  */
    GD_PHASE_DATA_IN       /* IO=1, DRQ=1: o host le o Device          */
} gd_phase_t;

/*
 * Produtor de dados de saida. A camada SPI instala um destes para
 * responder; o task file puxa dele em blocos.
 *
 * No RP2350 isto mapeia para o PIO/FIFO. No simulador de host e um
 *_callback_. O protocolo nao sabe a diferenca.
 */
typedef struct {
    /* Prepara a resposta e devolve o Byte Count em *bytecount. */
    int (*open)(gd_device_t *dev, uint16_t *bytecount);
    /* Produz ate `nwords` palavras. Devolve o numero produzido. */
    uint32_t (*read)(gd_device_t *dev, uint16_t *dst, uint32_t nwords);
    /* Liberta recursos. Idempotente. */
    void (*close)(gd_device_t *dev);
} gd_source_t;

struct gd_device {
    const gd_hal_t *hal;
    gd_disc_t      *disc;

    /* Os 8 registos de 8 bits. */
    uint8_t reg[GD_R_COUNT];
    uint8_t altstatus;
    uint8_t devcontrol;

    /* O registo Data: unico de 16 bits. */
    uint16_t data;

    gd_phase_t phase;
    uint8_t    packet[GD_PKT_SIZE];
    uint8_t    packet_words;      /* quantas das 6 palavras ja chegaram */
    uint8_t    pending_command;   /* o comando que precede o 0xA0 */

    /* Fonte de dados da fase de saida, se alguma. */
    const gd_source_t *src;
    uint32_t remaining;           /* bytes por transferir */
    uint32_t transferred;         /* bytes ja transferidos */

    /* Sink de dados de entrada (SET_MODE: o unico comando com dados
     * host->device, spec seca 7.2). */
    uint8_t  *sink;
    uint32_t  sink_len;
    uint32_t  sink_got;
    void    (*sink_done)(gd_device_t *dev);

    /*
     * Estado de erro que sobrevive a comandos. A spec so tem o registo
     * Error, mas o device mantem tambem ASC/ASCQ para o REQ_ERROR, e o
     * FAD corrente para o REQ_STAT.
     */
    uint8_t  sensekey;
    uint8_t  asc;
    uint8_t  ascq;
    uint32_t cur_fad;

    /* L4: reproducao de CD-DA. Ver gd_cdda.h. */
    gd_cdda_t  cdda;

    int irq;
};

/* Estado da unidade, extraido de Sector Number bits 3..0 (sec. 2.3) */
uint8_t gd_device_state(const gd_device_t *dev);
void    gd_device_set_state(gd_device_t *dev, uint8_t state);

/* Empurra a fase actual para os registos Status / Interrupt Reason. */
void gd_taskfile_sync_status(gd_device_t *dev);

/* Chamado pela camada SPI para iniciar uma resposta. */
void gd_taskfile_begin_response(gd_device_t *dev, const gd_source_t *src,
                                uint8_t sensekey);

/* Resposta sem dados (TEST_UNIT, CD_OPEN, ...): spec seca 7.4. */
void gd_taskfile_finish_nodata(gd_device_t *dev, uint8_t sensekey);

/* Fim da transferencia de dados, spec seca 7.1 passo 9. */
void gd_taskfile_end_transfer(gd_device_t *dev);

/* Puxa palavras do host. Usado pelo backend. */
uint16_t gd_taskfile_take_data(gd_device_t *dev);
void     gd_taskfile_put_data(gd_device_t *dev, uint16_t w);

/* Puxa ate `nwords` palavras do produtor de dados. Fecha a fonte e
 * sinaliza o fim da transferencia quando o Byte Count se esgota. */
uint32_t gd_taskfile_read_data(gd_device_t *dev, uint16_t *dst, uint32_t nwords);

/* 1 se ainda ha dados por transferir na fase actual. */
static inline int gd_taskfile_data_pending(const gd_device_t *dev)
{
    return dev->phase == GD_PHASE_DATA_IN && dev->remaining > 0;
}

/* Power-on / hard reset (spec seca 3.3.1.1). */
void     gd_taskfile_reset(gd_device_t *dev);

/* Chamados pelo backend quando o host acede a um registo. */
void     gd_taskfile_write_reg(gd_device_t *dev, uint8_t reg, uint8_t value);
uint8_t  gd_taskfile_read_reg(gd_device_t *dev, uint8_t reg);
void     gd_taskfile_command(gd_device_t *dev, uint8_t cmd);

/*
 * Leitura de Alternate Status. A diferenca entre os dois nao e
 * cosmetica: a spec seca 3.4 diz que o AltStatus "does not clear DMA
 * status information when it is accessed". O emulador Flycast vai mais
 * longe e so limpa o INTRQ em GD_STATUS (0x5F709C), nunca em
 * GD_ALTSTAT (0x5F7018) - e jogos dependem dessa diferenca para sondar
 * o estado sem cancelar o IRQ.
 */
uint8_t  gd_taskfile_read_altstatus(gd_device_t *dev);

/* Limpa o INTRQ, como faz a leitura de Status. */
void     gd_taskfile_clear_irq(gd_device_t *dev);

/* Põe ou limpa um bit do registo Status. */
void     gd_taskfile_set_status(gd_device_t *dev, uint8_t bit, int on);

/* Marca um erro: regista o Sense Key e poe CHECK. */
void     gd_taskfile_set_error(gd_device_t *dev, uint8_t sensekey);

/* Limpa o estado de erro. REQ_ERROR chama isto depois de responder. */
void     gd_taskfile_clear_error(gd_device_t *dev);

#endif /* GD_TASKFILE_H */
