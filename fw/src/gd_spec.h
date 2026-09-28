/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_spec.h - Constantes e tipos da GD-ROM Protocol SPI
 *             (Sega Packet Interface) Specifications Ver.1.30
 *
 * Fonte primaria: SEGA Enterprises, "GD-ROM Protocol SPI (Sega Packet
 * Interface) Specifications Ver.1.30", 12 January 1999.
 *
 * Cada constante abaixo indica a seccao da spec de onde vem. Nada aqui foi
 * inventado: se nao esta na spec, esta commentado como tal.
 *
 * NOTA DE LICENCA: estes valores foram transcritos da especificacao da SEGA,
 * que e um documento proprietario. A implementacao de uma especificacao e
 * legitima; a redistribuicao do texto da especificacao pode nao ser.
 */
#ifndef GD_SPEC_H
#define GD_SPEC_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Secao 3.4, Tabela 3.1 - Enderecamento dos registos                   */
/* ------------------------------------------------------------------ */
/*
 * Os registos nao sao enderecados por offset: sao seleccionados por
 * /CS0-, /CS1- e DA2..DA0.  Define-se aqui o valor de controlo que o host
 * coloca nos pinos, empacotado assim:
 *
 *     bits 6 = W (1 = escrita, 0 = leitura)
 *     bit  5 = CS1-
 *     bit  4 = CS0-
 *     bits 3..1 = DA2, DA1, DA0
 *     bit  0 = reservado / sempre 0 no mapeamento
 *
 * Esta e a codificacao usada pelo Dreamdrive (registerIndex_map[128]) e
 * mantida aqui pelo mesmo motivo: torna o hot path um indexamento sem
 * ramos.
 */

#define GD_CTRL(w, cs1, cs0, a2, a1, a0) \
    ((((w) ? 1u : 0u) << 6) | ((cs1) << 5) | ((cs0) << 4) | \
     ((a2) << 3) | ((a1) << 2) | ((a0) << 1))

/* Control Block: CS0- = N (1), CS1- = A (0)  ->  DA2 DA1 DA0 = 1 1 0 */
#define GD_REG_ALTSTATUS_R   GD_CTRL(0, 0, 1, 1, 1, 0)
#define GD_REG_DEVCONTROL_W  GD_CTRL(1, 0, 1, 1, 1, 0)

/* Command Block: CS0- = A (0), CS1- = N (1) ->  DA2 DA1 DA0 */
#define GD_REG_DATA_R        GD_CTRL(0, 1, 0, 0, 0, 0)
#define GD_REG_DATA_W        GD_CTRL(1, 1, 0, 0, 0, 0)
#define GD_REG_ERROR_R       GD_CTRL(0, 1, 0, 0, 0, 1)
#define GD_REG_FEATURES_W    GD_CTRL(1, 1, 0, 0, 0, 1)
#define GD_REG_INTREASON_R   GD_CTRL(0, 1, 0, 0, 1, 0)
#define GD_REG_SECTORNR_R    GD_CTRL(0, 1, 0, 0, 1, 1)
#define GD_REG_BYTECOUNTL_R  GD_CTRL(0, 1, 0, 1, 0, 0)
#define GD_REG_BYTECOUNTL_W  GD_CTRL(1, 1, 0, 1, 0, 0)
#define GD_REG_BYTECOUNTH_R  GD_CTRL(0, 1, 0, 1, 0, 1)
#define GD_REG_BYTECOUNTH_W  GD_CTRL(1, 1, 0, 1, 0, 1)
#define GD_REG_DRIVESEL_R    GD_CTRL(0, 1, 0, 1, 1, 0)
#define GD_REG_DRIVESEL_W    GD_CTRL(1, 1, 0, 1, 1, 0)
#define GD_REG_STATUS_R      GD_CTRL(0, 1, 0, 1, 1, 1)
#define GD_REG_COMMAND_W     GD_CTRL(1, 1, 0, 1, 1, 1)

/* ------------------------------------------------------------------ */
/* Secao 2.3 - Campo a campo do registo Status                         */
/* ------------------------------------------------------------------ */
#define GD_ST_BSY    0x80  /* drive acede ao command block                 */
#define GD_ST_DRDY   0x40  /* pronto para responder a comando ATA         */
#define GD_ST_DF     0x20  /* drive fault                                  */
#define GD_ST_DSC    0x10  /* seek completo                               */
#define GD_ST_DRQ    0x08  /* transfer ready                               */
#define GD_ST_CORR   0x04  /* erro corrigivel                             */
#define GD_ST_CHECK  0x01  /* erro na ultima execucao                      */

/* Secao 2.3 - Interrupt Reason (read-only) */
#define GD_IR_COD    0x01  /* 0 = dados, 1 = comando                       */
#define GD_IR_IO     0x02  /* 1 = device->host, 0 = host->device          */

/* Secao 2.3 - Error: bits 7..4 sao a Sense Key */
#define GD_ERR_SENSEKEY_MASK  0xf0
#define GD_ERR_SENSEKEY_SHIFT 4
#define GD_ERR_MCR  0x08    /* media change requested / ejected            */
#define GD_ERR_ABRT 0x04    /* drive not ready, comando invalido           */
#define GD_ERR_EOM  0x02
#define GD_ERR_ILI  0x01

/* Secao 2.3 - Device Control */
#define GD_DC_SRST  0x04    /* spec 3.3.1.3: "not used in the current
                              protocol" - nao implementar                */
#define GD_DC_NIEN  0x02    /* 1 = INTRQ em hi-Z                          */

/* Secao 2.3 - Features */
#define GD_FEAT_DMA 0x01    /* bit 0: 1 = transferir em modo DMA          */
#define GD_FEAT_XFERMODE 0x03 /* escrever 3 = modo no Sector Count        */

/* Secao 2.3 - Sector Count bits 7..3 (modo) e 2..0 (valor) */
#define GD_SC_MODE_MASK  0xf8
#define GD_SC_MODE_PIO_DEFAULT   (0x00 << 3)
#define GD_SC_MODE_PIO_FLOW      (0x01 << 3)
#define GD_SC_MODE_DMA_SINGLE    (0x02 << 3)
#define GD_SC_MODE_DMA_MULTI     (0x04 << 3)

/* Secao 2.3 - Sector Number bits 7..4 = Disc Format, 3..0 = Status */
#define GD_SN_FORMAT_MASK 0xf0
#define GD_SN_FORMAT_GDROM 0x80          /* GD-ROM = 8 */
#define GD_SN_STATUS_MASK 0x0f

/* Secao 2.4 - Task file apos power-on / hard reset */
#define GD_RESET_STATUS     0x00
#define GD_RESET_ERROR      0x01
#define GD_RESET_SECTORCNT  0x01
#define GD_RESET_SECTORNUM  0x01
#define GD_RESET_CYLLO      0x14
#define GD_RESET_CYLHI      0xeb
#define GD_RESET_DRIVEHEAD  0x00

/* Secao 5 - "Bit 7 (BSY) becomes valid 400 ns after a command is
 * received" - o unico parametro temporal oficial do G1.               */
#define GD_BSY_VALID_NS 400

/* ------------------------------------------------------------------ */
/* Secao 3 / Tabela 3.3 - Comandos ATA (task file)                     */
/* ------------------------------------------------------------------ */
#define GD_CMD_NOP        0x00
#define GD_CMD_SOFTRESET  0x08
#define GD_CMD_EXECDIAG   0x90
#define GD_CMD_PACKET     0xa0
#define GD_CMD_IDDEV      0xa1
#define GD_CMD_SETFEATURE 0xef

/* Tabela 3.4 - codigos de erro de Execute Device Diagnostic */
#define GD_DIAG_NORMAL       0x01
#define GD_DIAG_DATA_BUFFER  0x03
#define GD_DIAG_ODC          0x04
#define GD_DIAG_CPU          0x05
#define GD_DIAG_DSC          0x06
#define GD_DIAG_OTHER        0x07

/* ------------------------------------------------------------------ */
/* Secao 8.1 - Command Packet Format: 12 bytes exactos, sem cabecalho  */
/* ------------------------------------------------------------------ */
#define GD_PKT_SIZE 12
#define GD_PKT_WORDS 6      /* 6 palavras de 16 bits */

/* Allocation length: 1 byte excepto GET_TOC e GET_SCD (2 bytes, MSB em
 * Byte 3). Ver Tabela 6.1. */
#define GD_ALLOCLEN_BYTE  1
#define GD_ALLOCLEN_WORD  2

/* ------------------------------------------------------------------ */
/* Secao 4 / Tabela 6.1 - Comandos SPI                                 */
/* ------------------------------------------------------------------ */
#define GD_SPI_TEST_UNIT 0x00
#define GD_SPI_REQ_STAT  0x10
#define GD_SPI_REQ_MODE  0x11
#define GD_SPI_SET_MODE  0x12
#define GD_SPI_REQ_ERROR 0x13
#define GD_SPI_GET_TOC   0x14
#define GD_SPI_REQ_SES   0x15
#define GD_SPI_CD_OPEN   0x16
#define GD_SPI_CD_PLAY   0x20
#define GD_SPI_CD_SEEK   0x21
#define GD_SPI_CD_SCAN   0x22
#define GD_SPI_CD_READ   0x30
#define GD_SPI_CD_READ2  0x31
#define GD_SPI_GET_SCD   0x40

/* Nao existe na Tabela 6.1. Usado apenas pelos testes para exercitar o
 * caminho de comando desconhecido. */
#define GD_SPI_READ_SECTOR 0x5a

/* NAO documentados na spec, mas obrigatorios para o boot. Confirmados em
 * iceGDROM (rv32/source/ide.c, do_cmd71()) e em MAME
 * (src/devices/bus/ata/gdrom.cpp, GDROM_Cmd71_Reply[]).              */
#define GD_SPI_CMD70     0x70
#define GD_SPI_CMD71     0x71

/* ------------------------------------------------------------------ */
/* Secao 8.2 - CD_READ: selecao de dados em Byte1[7:4]                */
/* ------------------------------------------------------------------ */
#define GD_READ_SEL_HEADER    0x80
#define GD_READ_SEL_SUBHEADER 0x40
#define GD_READ_SEL_DATA      0x20
#define GD_READ_SEL_OTHER     0x10
#define GD_READ_SEL_MASK      0xf0

/* Byte1[3:1] - Expected Data Type */
#define GD_READ_TYPE_ANY        0
#define GD_READ_TYPE_CDDA_2352  1
#define GD_READ_TYPE_MODE1_2048 2
#define GD_READ_TYPE_MODE2_2336 3
#define GD_READ_TYPE_M2F1_2048  4
#define GD_READ_TYPE_M2F2_2324  5
#define GD_READ_TYPE_M2NONXA   6
#define GD_READ_TYPE_MASK       0x0e
#define GD_READ_TYPE_SHIFT      1

/* Byte1[0] - Parameter Type: 0 = FAD, 1 = MSF */
#define GD_READ_PARAM_MSF 0x01

/* ------------------------------------------------------------------ */
/* Secao 6.1 / Tabela 4.1 - estado da unidade (campo "Status")         */
/* ------------------------------------------------------------------ */
#define GD_STATE_BUSY    0x0
#define GD_STATE_PAUSE   0x1
#define GD_STATE_STANDBY 0x2
#define GD_STATE_PLAY    0x3
#define GD_STATE_SEEK    0x4
#define GD_STATE_SCAN    0x5
#define GD_STATE_OPEN    0x6
#define GD_STATE_NODISC  0x7
#define GD_STATE_RETRY   0x8
#define GD_STATE_ERROR   0x9
#define GD_STATE_FATAL   0xa

/* Secao 2.3 - Disc Format em REQ_STAT Byte1[7:4] */
#define GD_FORMAT_CDDA  0x0
#define GD_FORMAT_CDROM 0x1
#define GD_FORMAT_XA    0x2
#define GD_FORMAT_CDI   0x3
#define GD_FORMAT_GDROM 0x8

/* ------------------------------------------------------------------ */
/* Tabela 3.2 - Sense Key                                              */
/* ------------------------------------------------------------------ */
#define GD_SK_NOSE       0x0
#define GD_SK_RECOVERED  0x1
#define GD_SK_NOTREADY   0x2
#define GD_SK_MEDIUMERR  0x3
#define GD_SK_HARDWARE   0x4
#define GD_SK_ILLEGALREQ 0x5
#define GD_SK_UNITATT    0x6
#define GD_SK_DATAPROT   0x7
#define GD_SK_ABORTED    0xb

/* ------------------------------------------------------------------ */
/* Secao 8.2 - GET_SCD: formato do subcode                            */
/* ------------------------------------------------------------------ */
#define GD_SCD_RAW 0x00   /* 96 bytes P..W  -> resposta de 100 bytes     */
#define GD_SCD_Q   0x01   /* 9 bytes Q      -> resposta de  14 bytes     */
#define GD_SCD_UPC 0x02
#define GD_SCD_ISRC 0x03

/* Secao 8.2 - Audio status em GET_SCD Byte1 */
#define GD_AUD_UNSUP  0x00
#define GD_AUD_PLAY   0x11
#define GD_AUD_PAUSE  0x12
#define GD_AUD_DONE   0x13
#define GD_AUD_ABEND  0x14
#define GD_AUD_NONE   0x15  /* default                                    */

/* Secao 8.2 - GET_TOC: 102 entradas de 4 bytes + 3 entradas de 4 bytes */
#define GD_TOC_ENTRIES 102
#define GD_MAX_TRACKS 99   /* tracks 1..99 na TOC, per Tabela 6.1 */
#define GD_TOC_SIZE    408

/* Req_Mode: 32 bytes, standby time default 0x00B4 = 180 s */
#define GD_REQMODE_SIZE 32
#define GD_DEFAULT_STANDBY 0x00b4
/* Os bytes 0..9 do bloco de hardware info sao gravaveis por SET_MODE;
 * os bytes 10..31 (nome, versao, data) sao imutaveis. */
#define GD_HARDINFO_WRITABLE 10
#define GD_DEFAULT_RETRY   0x08

/* Req_Stat: 10 bytes. Req_Error: 10 bytes. Req_Ses: 6 bytes. */
#define GD_REQSTAT_SIZE 10
#define GD_REQERROR_SIZE 10
#define GD_REQSES_SIZE 6

#endif /* GD_SPEC_H */
