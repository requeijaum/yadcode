/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_l1.h - Camada L1: PHY G1 device-side (PIO + GPIO).
 *
 * ESTADO: ESQUELETO. Compila para host e para RP2350, corre nos testes
 * de host, nunca correu em silicio. Foi escrito por inferencia a partir
 * dos documentos; cada afirmacao traz a sua proveniencia para que a
 * correccao com hardware seja cirurgica.
 *
 * CONVENCAO DE TAGS (usada em gd_l1.h/.c, gd_l1_hw.c e g1_dev.pio):
 *
 *   [DOC-01 §x]  vem da spec Sega / docs/01 (seccao indicada).
 *   [DOC-02 ..]  idem para os outros documentos (02 pinout, 03 timing
 *                ATA-3, 06b DMA, 08 arquitetura, 11 validacao, 13 Flycast).
 *   [INFERRED: motivo]  deducao; o motivo esta' escrito para se poder
 *                discordar sem adivinhar o raciocinio.
 *   [UNKNOWN: ...]  chute ou lacuna; diz exactamente o que o hardware
 *                (scope / analisador / Dreamcast real) ha-de decidir.
 *   [TODO-L1]  trabalho pendente com dono claro.
 *
 * O QUE E' L1 E O QUE NAO E'
 * --------------------------
 * L1 e' a implementacao RP2350 de `gd_hal_t` (ver gd_hal.h): recebe
 * acessos do host no barramento G1 e chama o nucleo (L2/L3). Nao sabe
 * protocolo — so' pinos, strobes e tempos. A regra de dependencia da
 * arquitetura [DOC-08 §1] e': L3 nao sabe se L1 e' PIO, e L1 nao sabe
 * o que e' um packet.
 */
#ifndef GD_L1_H
#define GD_L1_H

#include <stdint.h>
#include "gd_hal.h"

/* FRENTE DE ONDA: so' estes dois ficheiros tocam em hardware. O resto
 * de gd_l1.* compila e testa no PC. */
struct gd_device;

/* ------------------------------------------------------------------ */
/* Descodificacao (pura, testavel no PC)                               */
/* ------------------------------------------------------------------ */

/*
 * Registos do ponto de vista do fio. ERROR/FEATURES e STATUS/COMMAND
 * partilham o endereco e distinguem-se pela direccao [DOC-01 Tabela
 * 3.1]; por isso o enum separa-os e o `is_write` decide.
 */
typedef enum {
    L1R_DATA = 0,
    L1R_ERROR,          /* leitura de DA=001 */
    L1R_FEATURES,       /* escrita de DA=001 */
    L1R_INTREASON,
    L1R_SECTORNUM,
    L1R_BYTECOUNTL,
    L1R_BYTECOUNTH,
    L1R_DRIVESEL,
    L1R_STATUS,         /* leitura de DA=111 */
    L1R_COMMAND,        /* escrita de DA=111 */
    L1R_ALTSTATUS,
    L1R_DEVCONTROL,
    L1R_NONE
} gd_l1_reg_t;

/*
 * Descodifica um acesso a partir dos NIVEIS dos pinos (0 = asserted,
 * porque /CS0, /CS1, /RD e /WR sao activo-baixo [DOC-02]).
 * `da` sao os 3 bits DA2..DA0. Devolve o registo e poe `*is_write`
 * a 1 se for escrita. L1R_NONE = sem acesso (nada asserted, os dois
 * strobes juntos, ou combinacao invalida como CS0+CS1).
 */
gd_l1_reg_t gd_l1_decode(unsigned cs0, unsigned cs1, unsigned da,
                         unsigned rd, unsigned wr, int *is_write);

/* Largura do acesso em bits: 16 para DATA, 8 para os de controlo,
 * 0 para NONE [DOC-01 §2.1]. */
unsigned gd_l1_width(gd_l1_reg_t reg);

/* ------------------------------------------------------------------ */
/* Despacho para o nucleo (puro, testavel no PC)                       */
/* ------------------------------------------------------------------ */

/*
 * Uma escrita do host ja' descodificada. `value` traz 16 bits; os
 * registos de controlo usam os 8 baixos [DOC-01 §2.1].
 */
void gd_l1_write(gd_device_t *dev, gd_l1_reg_t reg, uint16_t value);

/* Uma leitura do host ja' descodificada. Devolve 16 bits; os registos
 * de controlo respondem nos 8 baixos. */
uint16_t gd_l1_read(gd_device_t *dev, gd_l1_reg_t reg);

/*
 * RESET pelo host (/RESET asserted, GPIO 27). [DOC-01 §3.3.1.1]:
 * task file com os valores de reset. O L1 ainda desasserta as suas
 * saidas via `hw` (INTRQ, IORDY, DMARQ).
 */
void gd_l1_reset(gd_device_t *dev);

/* ------------------------------------------------------------------ */
/* Hardware (implementado em gd_l1_hw.c, so' no firmware)              */
/* ------------------------------------------------------------------ */

/*
 * Operacoes que tocam silicio. No PC usam-se fakes (ver test_l1.c);
 * no RP2350 vivem em gd_l1_hw.c. Separar aqui e' o que deixa testar
 * o despacho sem hardware e trocar de placa sem tocar no protocolo.
 */
typedef struct {
    void (*init_pins)(void);
    void (*set_intrq)(int asserted);   /* 1 = asserted */
    void (*set_iordy)(int asserted);   /* 1 = asserted (drive pronto) */
    void (*set_dmarq)(int asserted);   /* 1 = asserted (so' com DMA) */
    uint32_t (*micros)(void);
} gd_l1_hw_t;

/* Instala as operacoes. Sem isto, stubs que nao fazem nada. */
void gd_l1_set_hw(const gd_l1_hw_t *hw);

/*
 * Pinos do device que NAO estao no bus. O bus (0..27) vem da doc 02
 * e e' partilhado com sniffer.h; estes sao escolha de placa.
 *
 * [UNKNOWN: placa] Numeros abaixo sao placeholders acima do bus para
 * nao colidir com ele. Quem desenhar a PCB troca com -D ou aqui, e
 * apaga este paragrafo. O teste (caso 95) fixa so' a regra "fora do
 * bus", nao o numero: o numero vai mudar de proposito.
 */
#ifndef GD_L1_PIN_INTRQ
#define GD_L1_PIN_INTRQ  28    /* saida: INTRQ ao host */
#endif

/*
 * O `gd_hal_t` que o firmware poe em `dev.hal`. Cada membro e' uma
 * linha para gd_l1_write/read com o mapeamento documentado, excepto
 * os que tocam silicio (vão por `hw`).
 */
extern const gd_hal_t gd_hal_rp2350;

/*
 * Arranque no RP2350 (gd_l1_hw.c, so' firmware): instala `hw`,
 * configura GPIO+PIO+IRQ e liga `dev` a' ISR. Devolve 0 sem SM livre.
 * `gd_l1_hw_poll()` e' o sitio do trabalho diferido futuro.
 */
int gd_l1_hw_start(struct gd_device *dev);
void gd_l1_hw_poll(void);

#endif /* GD_L1_H */
