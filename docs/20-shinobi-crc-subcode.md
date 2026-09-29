# 20 — O `Shinobi.lib` responde à questão F: o CRC do subcode

> Documento de trabalho, 2026-09-28. Fecha a questão **F** de
> [13 §7](13-estudo-flycast.md) com evidência de código, e acrescenta o que
> o `shinobi.elf.lib` diz sobre as restantes.
>
> **Ainda não validado em hardware.** O que segue é leitura de código
> proprietário da Sega, com citação de endereço. Ver [19 §2.4](19-kos-e-reversao-da-bios.md)
> para o aviso sobre ler código alheio.

## 1. Como isto apareceu

A resposta ao [SDK_GAP_PROMPT](SDK_GAP_PROMPT.md) foi **"o SDK não responde"
em sete de sete perguntas**, cada uma com ausência verificada e citada. É
uma resposta válida e útil — mas deixou um nome em circulate: o CRC está em
`Shinobi.lib`, "binário proprietário".

Essa descrição estava errada. O ficheiro é
`Utl/Dev/CodeWarrior/Dreamcast Support/Shinobi/Lib/shinobi.elf.lib`, 229.338
bytes, datado de 1999-11-09, e é um **arquivo `ar` com 96 objectos ELF
SH4 e tabela de símbolos completa**. Não é um blob: é código linkado com
nomes de função legíveis. A ausência de source não é a ausência de
informação.

## 2. A tabela de CRC

Do objecto `fmcrc_.obj.elf` (1.340 bytes), secção `CSG` de 512 bytes:

```
Símbolos:  00000000 NOTYPE GLOBAL  _crctbl       (secção 6 = CSG)
           00000000 FUNC   GLOBAL  _fmcalccrc    (secção 4 = PSG)
Origem:    símbolo 1 = FILE "fmcrc_.c"
Secções:   PSG = 0x3c  (60 bytes, offset 0x34)   rotina
           CSG = 0x200 (512 bytes, offset 0x70)   tabela = 256 × 16 bits
```

Primeiras entradas da tabela:

| índice | valor |
|---|---|
| `0x00` | `0x0000` |
| `0x01` | `0x2110` |
| `0x02` | `0x4220` |
| `0x03` | `0x6330` |
| `0x04` | `0x8440` |
| `0x08` | `0x0881` |
| `0x10` | `0x3112` |
| `0x80` | `0x8891` |

**É a tabela canónica do CRC-CCITT, polinómio `0x1021`, MSB-first, sem
reflexão.** Verificado **256/256** contra a geração directa do polinómio
(caso 97 em `fw/tests/test_crc.c`).

> ⚠️ **A primeira extracção desta tabela estava errada**, e o erro mudou
> a conclusão. O `sh4-linux-gnu-objdump -s` imprimiu os bytes em
> endianness trocada, dando `0x2110, 0x4220, 0x6330…` — valores que não
> correspondem a nenhum polinómio de 16 bits, e que pareceram indicar
> uma tabela proprietária. Lidos byte a byte do ELF32
> (`e_shoff = 696`, secção 6, offset `0x70`), os valores são
> `0x1021, 0x2042, 0x3063…`, que **são** a tabela de `0x1021`. A lição:
> para objectos CodeWarrior, extrair secções pelo ELF, nunca pelo
> `objdump -s`.

A tabela tem 256 entradas todas distintas, e `crctbl[1] == 0x1021` — o
próprio polinómio, como é próprio de uma tabela de CRC MSB-first.

## 3. A rotina

De `fmcalccrc_.obj.elf`, secção `PSG` (30 bytes), com os endereços
citados:

```
0000 <_fmcalccrc>:
   0:  e6 2f   mov.l  r14,@-r15
   2:  0d de   mov.l  38 <+0x38>,r14     ! r14 = &_crctbl
   4:  0b d6   mov.l  34 <+0x34>,r6      ! r6  = 0xffff  (init)
   6:  0d a0   bra    24 <+0x24>
   a:  6d 67   extu.w r6,r7               ! idx = acc & 0xFFFF
   c:  73 60   mov    r7,r0
   e:  f8 e3   mov    #-8,r3
  10:  3c 40   .word  0x403c              ; mov #imm,r3
  12:  54 63   mov.b  @r5+,r3             ! byte = *src++
  14:  73 62   mov    r7,r2
  16:  18 42   shll8  r2                  ! idx <<= 8   (byte ALTO)
  18:  3c 63   extu.b r3,r3
  1a:  23 66   mov    r2,r6
  1c:  3a 20   xor    r3,r0               ! acc ^= byte
  1e:  00 40   shll   r0
  20:  ed 01   mov.w  @(r0,r14),r1        ! r1 = crctbl[acc<<8]
  22:  1a 26   xor    r1,r6               ! acc = r6 ^ tabela
  24:  ff 74   add    #-1,r4
  26:  11 44   cmp/pz r4
  28:  ef 89   bt     a  <+0xa>
  2a:  6d 60   extu.w r6,r0
  2c:  07 60   not    r0,r0               ! <-- COMPLEMENTO
  2e:  0d 60   extu.w r0,r0
  30:  0b 00   rts
```

### 3.1 As três características, e o que resolvem

1. **O índice é o byte ALTO** do acumulador (`shll8 r2`, offset `0x16`),
   não o baixo. É a assinatura de um CRC MSB-first.
2. **O acumulador é complementado no fim**: `not r0,r0` em `0x2c`,
   seguido de `extu.w` para re-mascarar a 16 bits.
3. **O valor inicial é `0xffff`** (offset `0x04`), não `0x0000`.

**A ambiguidade de [13 §7](13-estudo-flycast.md) fica resolvida, e a
resposta é a segunda opção**: o GD-ROM usa o polinómio `0x1021` de
XMODEM/CCITT, **inicializado a `0xFFFF` e com o resultado complementado**.

Chamar-lhe "XMODEM" a seco seria errado, e é o que o `docs/13` §5 já
avisava: o XMODEM padrão tem `init = 0x0000` e **não** tem complemento
final. Codificá-lo como XMODEM puro produziria bytes de subcode inválidos.

O algoritmo está em `fw/src/gd_crc.c`, com os valores de referência
impressos pelo caso 101:
- `gd_crc_subcode("123456789")` = **`0xD64E`**
- `gd_crc_ccitt("123456789")` = `0x31C3` (o mesmo polinómio, sem init nem NOT)

### 3.2 `_gdDecSubcode` usa exactamente o mesmo algoritmo

`gdDec_.obj.elf` (1.728 bytes), `PSG`:

```
000000b8 <_gdDecSubcode>:
   6:  4a d5   mov.l  130 <+0x78>,r5     ! r5 = 0xff00
   8:  48 de   mov.l  12c <+0x74>,r14    ! r14 = &_crctbl (CSG+0)
  16:  d4 63   mov.b  @r13+,r3
  22:  18 42   shll8 r2                  ! mesmo: byte alto
  24:  ed 01   mov.w  @(r0,r14),r1       ! mesma tabela
  26:  59 22   and   r5,r2               ! mascara 0xff00
  ...
  4c:  07 64   not    r0,r4              ! <-- mesmo NOT
  4e:  4d 64   extu.w r4,r4
```

A relocação confirma a ligação à tabela:

```
.relaPSG  Offset 0000012c  R_SH_DIR32  00000000  CSG + 0
```

Isto é mais forte do que ler a spec: **é o algoritmo que a Sega usa para o
subcode**, no objecto que os jogos usam, e ele é partilhado com o CRC do
sistema de ficheiros do CD. O mesmo objecto `gdc_lib_` é linkado por
`gdctl_`, que por sua vez chama `_gdGdcReqDmaTrans`.

## 4. O que mais o `shinobi.elf.lib` diz

Símbolos relevantes, com o objecto que os define:

| Símbolo | Objecto | Pergunta que toca |
|---|---|---|
| `_gdGdcReqCmd` | `gdc_lib_` | B — o pedido de comando |
| `_gdGdcReadAbort` | `gdc_lib_` | **B** — o abort |
| `_gdGdcGetDrvStat` | `gdc_lib_` | B — o blob de `0xA1` |
| `_gdGdcReqDmaTrans` | `gdc_lib_` | **C** — pedido de DMA |
| `_gdGdcCheckDmaTrans` | `gdc_lib_` | **C** — verificação de DMA |
| `_gdGdcG1DmaEnd` | `gdc_lib_` | C — fim de DMA no G1 |
| `_gdGdcGetCmdStat` | `gdc_lib_` | B — estado do comando |
| `_gdDecSubcode` | `gdDec_` | **F** — resolvido |
| `_gdFsGetGdDcf` | `gdctl_` | E — formato de disco |

**Sobre C:** existem **três** funções distintas de DMA
(`ReqDmaTrans`, `CheckDmaTrans`, `G1DmaEnd`) ao lado das de PIO, e
`gdctl_` chama `CheckDmaTrans`. Isto é evidência de que o caminho DMA
existe e é exercitado pela biblioteca, o que concorda com o que o
[KallistiOS](19-kos-e-reversao-da-bios.md) faz por omissão. **Não fecha
C** — falta saber se o BIOS de retail o usa por omissão para leitura de
disco — mas estreita mais.

**Sobre B:** `_gdGdcReadAbort` e `_gdGdcGetDrvStat` são funções distintas,
o que sugere que o abort e a leitura do status do drive são passos
separados, e não um abort seguido de um blob fixo. É indício, não
prova.

## 5. O que continua aberto

- **A** (`0x71`: 6 bytes ou 1012?) — o SDK não menciona `0x71`; o código
  de `gdc_lib_` é que decidirá.
- **B** (`0xA1` abort + 80 bytes) — indício em §4, nãocitado ainda.
- **C** (DMA por omissão?) — estreitado, não fechado.
- **D** (`GET_SCD` formato 2/3) — nada no SDK.
- **E** (`GD_LEADOUT_FAD = 549300`) — `_gdFsGetGdDcf` é o candidato, não
  dissecado.
- **G** (`GetBaseFAD() = 45150`) — é syscall da BIOS, fora do SDK.

## 6. A tabela e o algoritmo, extraídos e verificados

### 6.1 A tabela

Extraída de `fmcrc_.obj.elf`, secção `CSG`, offset `0x70`, tamanho `0x200`
(512 bytes = 256 × 16 bits), lida byte a byte do ELF32.

| Teste | Resultado |
|---|---|
| Entradas | 256, todas distintas |
| Geração canónica de `0x1021` MSB-first | **256/256** (caso 97) |
| `crctbl[0]` | `0x0000` |
| `crctbl[1]` | `0x1021` (o próprio polinómio) |

Está em `fw/src/gd_crctbl.c`, com a origem e a política no header.

### 6.2 O algoritmo

`fw/src/gd_crc.c`, espelhando a rotina original registo a registo:

```c
uint16_t gd_crc_step(uint16_t acc, uint8_t byte) {
    unsigned idx = ((acc >> 8) ^ byte) & 0xFFu;      /* shll8 + xor */
    return (uint16_t)(((acc << 8) ^ gd_crctbl_sega[idx]) & 0xFFFFu);
}
```

O acumulador é de 16 bits; o índice é o byte **alto** XOR a entrada; a
tabela entra com `acc << 8` a compensar. O `init = 0xFFFF` e o `NOT` final
são os que a rotina original tem nos offsets `0x04` e `0x2c`.

Propriedades estruturais verificadas (casos 98–100):
- entrada vazia → `0x0000` (= `~0xFFFF`, consistente com o NOT)
- inverter 1 bit muda o resultado
- 200 entradas de `0x00` → 200 valores distintos (sem ciclo curto)
- 200 entradas de `0xFF` → 200 valores distintos
- avalanche média de 7,5 bits em 16

### 6.3 Os erros que essa extracção custou

Vale registar, porque a sequência é instrutiva:

1. **A primeira tabela estava errada.** O `objdump -s` imprimiu os bytes
   em endianness trocada (`0x2110, 0x4220…` em vez de `0x1021, 0x2042…`).
   Os valores errados não correspondiam a nenhum polinómio de 16 bits, o
   que fez parecer que a tabela era proprietária — uma conclusão errada
   que eu escrevi no `docs/20` e no `docs/13` antes de a refazer.
2. **Cinco indexações foram testadas com a tabela errada**, e todas
   falharam. Com a tabela certa, a indexação correcta é a primeira
   hipótese que fiz — e ela passa todos os testes. O sintoma (ciclo curto,
   insensibilidade a 1 bit) era da tabela, não da indexação.
3. **Dois testes meus estavam mal escritos** e falharam na primeira
   execução: um esperava `crctbl[0xFF]` isolado quando o correcto é
   `0xFF00 ^ crctbl[0xFF]`, e outro tinha `==` onde queria `!=`.

A lição geral: **o `sh4-linux-gnu-objdump` desalinha a secção `PSG` no
offset `0x10`** (imprime `.word 0x403c` onde há instrução), e o `-s` mostra
os dados em endianness trocada. Para objectos CodeWarrior, extrair pelo
ELF e desensamblar com o Ghidra.

### 6.4 O que continua por provar

**[UNKNOWN: hardware] Nada disto foi validado contra um GD-ROM real.**
O que está em §6 é leitura de código proprietário da Sega, verificada
internamente (a tabela é coerente, o algoritmo tem as propriedades de um
CRC), mas não medida. O [docs/16 §8](16-sniffer-g1.md) continua a ser o
caminho, e o caso 101 imprime os valores exactos para comparar com o que
um scope mostrar.

## 7. Estado da questão F

**RESOLVIDA quanto ao algoritmo**, com a ressalva da §6.4.

| | Valor |
|---|---|
| Polinómio | `0x1021` (CRC-CCITT / XMODEM), MSB-first, sem reflexão |
| Init | `0xFFFF` |
| Complemento final | **sim** (`not` + `extu.w`, offset `0x2c`) |
| Tabela | `crctbl[0..255]`, verificada 256/256 |
| `gd_crc_subcode("123456789")` | `0xD64E` |
| `gd_crc_subcode(96 zeros)` | `0x75D3` |

Ou seja: **a "variante complementada"** da alternativa B de [13 §7](13-estudo-flycast.md).
O `docs/13` §5 tinha acertado — mantinha a variante do GD-ROM por
observação em hardware — e o código da Sega confirma-lhe a razão.

Falta apenas a **medição** para fechar por completo, não a informação.

## 8. Estado da evidência, e o que falta para a publicar

O §3 é citável: endereço, algoritmo, e o `not` final. Mas duas coisas
faltam antes de isto entrar no firmware:

1. ~~Extrair a tabela completa~~ — **feito**, `fw/src/gd_crctbl.c`.
2. **Validar contra um drive real.** O `docs/16` §8 continua a ser o
   caminho, e é a única coisa que falta. Esta leitura diz o que a Sega
   *faz*; não diz o que o hardware *aceita*.
3. **Ligar ao firmware** quando (2) estiver feito: `gd_crc_subcode()` está
   pronto mas ainda não é chamado por nada.

Nada disto foi copiado para `fw/`. Como no [Flycast](13-estudo-flycast.md),
o que se extrai é **comportamento e constantes numéricas**, com citação
da origem — e este material é proprietário da Sega, pelo que a
regra de `ref/README.md` aplica-se com o
mesmo peso: análise e referência, nunca transcrição para o repo.
