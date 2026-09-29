/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_crc.c - CRC do subcode do CD, conforme o Katana SDK.
 *
 * O QUE E' ISTO
 * -------------
 * O GD-ROM anexa um CRC aos blocos de subcode que devolve no comando
 * SPI GET_SCD (0x40). O Katana SDK implementa esse calculo em
 * fmcrc_.obj.elf, funcao `_fmcalccrc`, que e' a mesma que
 * `_gdDecSubcode` (objecto gdDec_.obj.elf) usa. Sem a fonte, isto foi
 * lido do binario.
 *
 * As duas caracteristicas que a rotina tem, e que o algoritmo tem de
 * reproduzir, estao no desensamblado [docs/20 §3]:
 *
 *   offset 0x04:  mov.l  0xffff,r6     -> acc inicializado a 0xFFFF
 *   offset 0x2c:  not    r0,r0        -> resultado complementado
 *   offset 0x2e:  extu.w r0,r0        -> mascarado a 16 bits
 *
 * O polinomio e' 0x1021 (CRC-CCITT), MSB-first, sem reflexao: a tabela
 * extraida e' a geracao canonica desse polinomio, 256/256.
 *
 * [INFERRED: o NOT e' aplicado ao acumulador de 16 bits antes do
 *  mascaramento] e' lido do par not+extu.w. Uma leitura alternativa
 *  (NOT de 8 bits) nao distinguivel do desensamblado e daria outro
 *  resultado -- ver docs/20 §3.1 e a nota de validacao.
 *
 * ONDE ISTO AINDA NAO ESTA PROVADO
 * --------------------------------
 * [UNKNOWN: hardware] Nao foi validado contra um GD-ROM real. O
 * docs/16 §8 continua a ser o caminho. O que esta em docs/20 §3.1 e'
 * leitura de codigo, nao medicao. Este ficheiro esta pronto para um
 * teste de host (test_crc.c) mas NAO e' ainda ligado ao firmware:
 * falta-o chamar, e falta confirmar contra hardware.
 *
 * A TABELA (gd_crctbl_sega) esta verificada 256/256. Este algoritmo esta
 * derivado da leitura e tem as propriedades estruturais de um CRC
 * (casos 97-101), mas nao foi medido.
 *
 * NAO ESTA NO CMakeLists.txt de proposito. Ligar isto ao firmware antes
 * de o medir seria transformar uma leitura de codigo propietario num
 * comportamento assumido. Ver gd_crctbl.h.
 */
#include "gd_crc.h"

/*
 * Um passo. `acc` e' o acumulador de 16 bits; o indice e' o byte ALTO
 * do acc XOR o byte de entrada — o `shll8` do offset 0x16 mais o
 * `xor` do offset 0x1c na rotina original.
 */
uint16_t gd_crc_step(uint16_t acc, uint8_t byte)
{
    unsigned idx = ((acc >> 8) ^ byte) & 0xFFu;
    return (uint16_t)(((acc << 8) ^ gd_crctbl_sega[idx]) & 0xFFFFu);
}

/*
 * CRC de um buffer, com o init e o complemento que o GD-ROM usa.
 * A entrada vazia devolve 0x0000, que e' o que `~0xFFFF` da.
 */
uint16_t gd_crc_subcode(const uint8_t *data, size_t len)
{
    uint16_t acc = 0xFFFFu;
    size_t i;
    for (i = 0; i < len; i++) {
        acc = gd_crc_step(acc, data[i]);
    }
    return (uint16_t)(~acc);
}

/* CRC-CCITT puro, para comparar: sem init especial, sem complemento. */
uint16_t gd_crc_ccitt(const uint8_t *data, size_t len)
{
    uint16_t acc = 0x0000u;
    size_t i;
    for (i = 0; i < len; i++) {
        acc = gd_crc_step(acc, data[i]);
    }
    return acc;
}
