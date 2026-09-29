/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_crctbl.h - Tabela de CRC do CD, extraida do Katana SDK.
 *
 * ORIGEM (nao e' original deste projecto):
 *   shinobi.elf.lib -> fmcrc_.obj.elf, secao CSG (offset 0x70, 0x200 bytes)
 *   simbolo _crctbl (NOTYPE GLOBAL), ficheiro de origem "fmcrc_.c"
 *   SeGa, Katana SDK, 1999-11-09. Codigo propietario.
 *
 * REGRA: comportamento e constantes numericas, com citacao da origem.
 * Nada foi transcrito. Ver docs/20-shinobi-crc-subcode.md e a politica
 * em ref/README.md.
 *
 * ---------------------------------------------------------------------
 * ESTADO: TABELA VERIFICADA, ALGORITMO DERIVADO, NADA VALIDADO EM HW.
 *
 * A tabela tem 256 entradas distintas, extraidas byte a byte da secao do
 * ELF32 (o `objdump -s` mostra-os em endianness trocada — ver
 * docs/20 §6.3) e confirmadas 256/256 contra a geracao canonica de
 * 0x1021. O algoritmo em gd_crc.c tem as propriedades estruturais de um
 * CRC, verificadas nos casos 97-101.
 *
 * [UNKNOWN: hardware] Nada disto foi medido num GD-ROM real. Ver
 * docs/16 §8. Os valores de referencia que um scope tem de produzir
 * estao no caso 101.
 *
 * NAO ESTA NO FIRMWARE. gd_crc.c e gd_crctbl.c nao estao no
 * CMakeLists.txt de proposito: ligar o CRC antes de o validar seria
 * codificar uma leitura de binario como se fosse medicao.
 * ---------------------------------------------------------------------
 */
#ifndef GD_CRCTBL_H
#define GD_CRCTBL_H

#include <stdint.h>

/*
 * _crctbl do Katana SDK, 256 palavras de 16 bits, verbatim da secao CSG.
 *
 * E' a tabela canonica do CRC-CCITT: polinomio 0x1021, MSB-first, sem
 * reflexao. Verificado 256/256 contra a geracao directa (caso 97).
 * `crctbl[1] == 0x1021`, o proprio polinomio, como e' proprio de uma
 * tabela de CRC MSB-first.
 */
extern const uint16_t gd_crctbl_sega[256];

#endif /* GD_CRCTBL_H */
