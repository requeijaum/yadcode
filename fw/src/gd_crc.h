/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_crc.h - CRC do subcode do CD, conforme o Katana SDK.
 *
 * Ver gd_crc.c para a proveniencia e o estado. Em resumo: polinomio
 * 0x1021 (CRC-CCITT, MSB-first), acc inicial a 0xFFFF, resultado
 * complementado. Documentado em docs/20-shinobi-crc-subcode.md.
 */
#ifndef GD_CRC_H
#define GD_CRC_H

#include <stdint.h>
#include <stddef.h>

/* Tabela extraida do Katana SDK (SeGa, 1999-11-09). Ver gd_crctbl.h. */
extern const uint16_t gd_crctbl_sega[256];

/* Um passo do CRC sobre `byte`, com o acc corrente. */
uint16_t gd_crc_step(uint16_t acc, uint8_t byte);

/*
 * CRC de `len` bytes como o GD-ROM o faz: acc a 0xFFFF, resultado
 * complementado. A entrada vazia devolve 0x0000.
 */
uint16_t gd_crc_subcode(const uint8_t *data, size_t len);

/* O mesmo, mas sem o init nem o complemento — para comparação. */
uint16_t gd_crc_ccitt(const uint8_t *data, size_t len);

#endif /* GD_CRC_H */
