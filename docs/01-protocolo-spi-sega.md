# 01 — Protocolo GD-ROM SPI (Sega Packet Interface)

Fonte primária: **"GD-ROM Protocol SPI (Sega Packet Interface) Specifications Ver.1.30"**,
SEGA Enterprises, CS Development & Manufacturing Division, 12 January 1999 (rev 1.30 de 1998-07-15).

- PDF (archive.org): https://www.archive.org/download/SCSISpecificationDocumentsSCSIDocuments/Vendor%20SCSI%20documents/Sega/GD-ROM%20Protocol%20SPI%20(Sega%20Packet%20Interface)%20Specifications%20Ver.1.30.pdf
- Cópia Sega Retro: https://segaretro.org/images/7/72/Cdif131e.pdf
- Página do recurso original (`cdif131e.doc`): https://segaxtreme.net/resources/gd-rom-protocol-spi-sega-packet-interface-specifications.189/

Citações marcadas com ✅ são literais da spec. 🔶 = inferido de forma consistente entre
fontes, mas não explícito na spec.

---

## 1. Natureza do protocolo

✅ §1.1: *"The SPI standard matches the KATANA GD-ROM drive"* e
*"Electrical interface between host and GD block conforms to ATA-3 specifications"*.

O que muda em relação a ATAPI/CD-ROM padrão é **apenas a camada de protocolo por cima dos
mesmos registadores ATA**. O GD-ROM não é ATAPI: não tem INQUIRY, REQUEST SENSE, MODE SENSE,
READ TOC, nem capacities. Tem o seu próprio conjunto de 16 comandos.

✅ §1.1: *"High-speed data transfer is possible (max. 16.6 MB/s)."*

⚠️ **Este número contradiz o "Dreamcast Hardware Specification Outline"**, que documenta
*"The real transfer speed at the time of GD-ROM access is 10MB/s (2880ns/32B)"* (§3.8) e
ainda *"From the buffer approx. 11.1 MB/s (PIO Mode3)"* e *"Approx. 13.3 MB/s (Multi word DMA
Mode2)"* (§6.1). A spec SPI **não dá contexto nenhum** para o 16.6 MB/s: a frase aparece uma
única vez, no §1.1, e o documento nunca quantifica os modos de transferência. Os quatro
números nunca são reconciliados. Ver [03-timing-ide.md](03-timing-ide.md) para a
reconciliação.

---

## 2. Mapa de registradores (§3.4, Tabela 3.1)

O endereçamento **não** usa offsets em bytes. É feito por `/CS0-`, `/CS1-` e `DA2..DA0`.
"A" = asserted, "N" = negated, "x" = don't care.

| CS0- | CS1- | DA2 | DA1 | DA0 | READ (DIOR-) | WRITE (DIOW-) | Offset no HOLLY |
|:---:|:---:|:---:|:---:|:---:|---|---|---|
| N | N | x | x | x | hi-Z | não usado | — |
| N | A | 0 | x | x | hi-Z | não usado | — |
| N | A | 1 | 0 | x | hi-Z | não usado | — |
| **N** | **A** | **1** | **1** | **0** | **Alternate Status** | **Device Control** | `0x18` / `0x4E4` |
| **A** | N | 0 | 0 | 0 | **Data** | **Data** | `0x80` |
| **A** | N | 0 | 0 | 1 | **Error** | **Features** | `0x84` |
| **A** | N | 0 | 1 | 0 | **Interrupt Reason** (RO) | não usado | `0x88` |
| **A** | N | 0 | 1 | 1 | **Sector Number** | não usado | `0x88`/`0x8C` |
| **A** | N | 1 | 0 | 0 | **Byte Count L (0-7)** | **Byte Count L** | `0x90` |
| **A** | N | 1 | 0 | 1 | **Byte Count H (8-15)** | **Byte Count H** | `0x94` |
| **A** | N | 1 | 1 | 0 | **Drive Select** | **Drive Select** | `0x98` |
| **A** | N | 1 | 1 | 1 | **Status** | **Command** | `0x9C` |
| A | A | x | x | x | inválido | inválido | — |

A última coluna é o **decode interno do HOLLY**, não parte do protocolo. Fonte: driver Linux
`drivers/cdrom/gdrom.c`, base `0x0A05F7000`.

**Consequência direta para o ODE:** só interessam os pinos `DA0..DA2`, `/CS0`, `/CS1`.
Os offsets são detalhe do host e nunca aparecem no fio.

### 2.1 Larguras de acesso

✅ §3.4: *"Except for the data register, all registers are read and written in byte units
(8 bits). The data register is always accessed in 16-bit words."*

→ **O ODE tem de apresentar 7 registradores de 8 bits + Data de 16 bits.** Isto tem
consequência estrutural no PIO: o `in pins` tem de ser mascarado a 8 bits para os
registadores de controlo, e o `out pins` tem de ser feito em 16 bits para o Data.

### 2.2 IOCS16- e DASP- são inúteis

✅ §3.2, citação literal completa:

> *"IOCS16- (Device 16-bit I/O) — For PIO transfer in mode 0, 1, or 2, the IOCS16- signal
> indicates that the 16-bit data port is already addressed and that the device can send and
> receive 16-bit data. This is an open collector output.*
> *■ In any PIO mode, the device supports only 16-bit data transfer. Therefore the IOCS16-
> signal is always asserted.*
> *■ During DMA mode transfer, the host uses the 16-bit DMA channel and the IOCS16- signal is
> not asserted. However, the signal cannot be used by the host."*

⚠️ **Atenção à estrutura condicional.** Em PIO o sinal está sempre asserted mas o host não o
usa; **em DMA não é asserted de todo**. A frase *"cannot be used by the host"* é a justificação
da designação PIO-only do sinal, não uma conclusão geral. Aplachar isto apaga precisamente a
distinção PIO/DMA onde vive o "sempre asserted".

Corroboração mecanicista, ainda no §3.4 "Data Register":

> *"The register is switchable between 8 and 16 bits. However, only 16-bit mode is supported
> because the host is not using the IOCS-16 signal."*

→ Não há pinos para `IOCS16-` e `DASP-` no G1 (ver
[02](02-barramento-g1-pinout.md)) e não devem ser implementados. O GD-ROM é sempre **master**
no barramento IDE.

### 2.3 Campo a campo

| Registo | Bits | Significado |
|---|---|---|
| **Status** | 7 BSY | ✅ *"always set to 1 when the drive accesses the command block"*. ✅ §5: *"Bit 7 (BSY) becomes valid 400 ns after a command is received"*. Acesso ao command block é permitido mesmo com BSY=1 (NOP, Soft Reset). |
| | 6 DRDY | pronto para responder a comando ATA |
| | 5 DF | drive fault |
| | 4 DSC | seek completo |
| | 3 DRQ | transfer ready |
| | 2 CORR | erro corrigível |
| | 0 CHECK | erro na última execução |
| **Alternate Status** | idem | ✅ *"same as the status register, but it does not clear DMA status information when it is accessed"* — razão pela qual o host lê AltStatus para fazer poll sem consumir o IRQ |
| **Error** | 7-4 | **Sense Key** (só refletido em modo SPI) |
| | 3 MCR | media change requested / ejected |
| | 2 ABRT | drive not ready, comando inválido |
| **Features** | 0 | **DMA** — 1 = transferir em modo DMA. Ver [06b](06b-scope-pio-vs-dma.md) |
| | 6-0 | Feature Number; **escrever 3** seleciona o modo de transferência definido no Sector Count |
| **Interrupt Reason (RO)** | 0 CoD | 0 = dados, 1 = comando |
| | 1 IO | 1 = device→host, 0 = host→device |
| **Sector Count (WO)** | 7-3 | `00000 0xx` PIO default · `00001 xxx` PIO flow-control · `00010 xxx` single-word DMA · `00100 xxx` multi-word DMA |
| | 2-0 | valor do modo |
| **Sector Number** | 7-4 | **Disc Format** (0=CD-DA, 1=CD-ROM, 2=XA/CD-Extra, 3=CD-I, **8=GD-ROM**) |
| | 3-0 | **Status** da unidade (BUSY/PAUSE/STANDBY/PLAY/SEEK/SCAN/OPEN/NODISC/RETRY/ERROR) |
| **Device Control** | 2 SRST | ✅ §3.3.1.3: *"not used in the current protocol"* |
| | 1 nIEN | 1 = INTRQ em hi-Z. É o registo que o host escreve para habilitar INTRQ (offset `0x4E4`) |

### 2.4 Reset

- **Power-on / hard reset** ✅ §3.3.1.1: task file inicializada com
  `Status=00h, Error=01h, Sector Count=01h, Sector Number=01h, Cyl Low=14h, Cyl High=EBh, Drive/Head=00h`.
- **Soft Reset (0x08)** ✅ §3.3.1.2: pode ser emitido mesmo com BSY=1; o único status
  reportado é BUSY; a task file reinicializa-se como no power-on, **exceto o bit DRV que
  permanece inalterado**.

---

## 3. Comandos ATA / task-file (Tabela 3.3)

| Comando | Code |
|---|---|
| NOP | `00h` |
| Soft Reset | `08h` |
| Execute Device Diagnostic | `90h` |
| Packet Command | `A0h` |
| Identify Device | `A1h` |
| Set Features | `EFh` |

Erros de Execute Diagnostic (Tabela 3.4): `01h` Normal · `03h` Data buffer · `04h` ODC ·
`05h` CPU · `06h` DSC · `07h` outro.

**Comando desconhecido** ✅ §5: o drive responde `abort` no Error register, ERROR no Status,
clear de busy, e INTRQ. É o que o iceGDROM faz: `IDE_ERROR = 0x04; IDE_STATUS = 0x51;`
(`rv32/source/ide.c`, `cmd_irq()`).

**Identify Device** (A1h): dados = bytes 0-3 IDs, `0x10`-`0x1F` fabricante (16 ASCII),
`0x20`-`0x2F` modelo, `0x30`-`0x3F` firmware. É sempre PIO.

**Set Features** (EFh): só modo de transferência. Feature = 3, modo nos 5 bits altos do
Sector Count.

> **Nota de nomenclatura.** Não existem `GDROM_COM_SET_LBA (0xC0)`, `READ (0xC1)`,
> `WRITE (0xC2)`, `READ_N (0x85)`, `INIT (0xC6)`, `STOP (0x31)`, `PACKET2 (0x1A)` na spec
> Sega. São convenções de firmwares de terceiros / mods G1-ATA. O GD-ROM é read-only.
> Ver [12-correcoes-ao-briefing.md](12-correcoes-ao-briefing.md).

---

## 4. Comandos SPI (Tabela 6.1)

| Comando | Função | Opcode | Parâmetros relevantes |
|---|---|---|---|
| **TEST_UNIT** | Verify access readiness | `00h` | não reporta CHECK; retorna GOOD |
| **REQ_STAT** | Get CD status | `10h` | Byte2 = starting address (sempre par), Byte4 = alloc length |
| **REQ_MODE** | Get various settings | `11h` | Byte2 = start addr, Byte4 = length (32) |
| **SET_MODE** | Make various settings | `12h` | Byte2 = start addr, Byte4 = length. **Único comando com dados host→device** |
| **REQ_ERROR** | Get error details | `13h` | Byte4 = alloc length (10) |
| **GET_TOC** | Get all TOC data | `14h` | Byte1 bit0 **Select** (0 single-density, 1 double-density); Bytes3-4 = alloc len 16-bit |
| **REQ_SES** | Get session data | `15h` | Byte2 = sessão (0-99), Byte4 = alloc length |
| **CD_OPEN** | Open tray | `16h` | — |
| **CD_PLAY** | Play CD | `20h` | Byte1[2:0] param type: 001=FAD, 010=MSF, 111=start playback; Byte2-4 start, Byte6[3:0] repeat, Byte8-10 end |
| **CD_SEEK** | Seek | `21h` | Byte1[2:0]: 001=FAD, 010=MSF, 011=stop→home, 100=pause |
| **CD_SCAN** | Scan | `22h` | Byte2 bit0 direção, Byte3 velocidade (0-2→2x, 3-5→5x, 6-9→9x, ≥10→16x) |
| **CD_READ** | Read CD | `30h` | ver §6 |
| **CD_READ2** | CD read (pre-read position) | `31h` | start em Byte2-4, **transfer length em Byte6-7 (16-bit)**, next address em Byte8-10 |
| **GET_SCD** | Get subcode | `40h` | Byte1[3:0] formato: 0=raw 96B, 1=Q 12B, 2=UPC, 3=ISRC; Bytes3-4 = alloc len |

Nomes canónicos usados pelos emuladores (Flycast, `core/hw/gdrom/gdrom_if.h`):

```c
SPI_TEST_UNIT 0x00  SPI_REQ_STAT 0x10  SPI_REQ_MODE 0x11  SPI_SET_MODE 0x12
SPI_REQ_ERROR 0x13  SPI_GET_TOC  0x14  SPI_REQ_SES  0x15  SPI_CD_OPEN   0x16
SPI_CD_PLAY   0x20  SPI_CD_SEEK  0x21  SPI_CD_SCAN  0x22  SPI_CD_READ   0x30
SPI_CD_READ2  0x31  SPI_GET_SCD  0x40
```

### 4.1 Comandos NÃO documentados, obrigatórios

- **`0x70`** — "prepare disk" / security check; `cmd[2] = 0x1f` ou `0x9f`; sem dados;
  sempre seguido por `0x71`. O driver Linux chama-lhe `gdrom_preparedisk_cmd()`.
- **`0x71`** — devolve um blob "obfuscated" (~200 bytes). MAME guarda-o em
  `GDROM_Cmd71_Reply[]`. Após `0x71` o estado da unidade muda: **PAUSE** se o disco é
  bootável, **STANDBY** caso contrário. O iceGDROM responde 6 bytes
  `BA 06 0D CA 6A 1F` (`rv32/source/ide.c`, `do_cmd71()`).

🔶 A resposta exata é sensível ao firmware/BIOS, mas **responder GOOD ao `0x70` e algo
plausível ao `0x71` é obrigatório** para o boot avançar. Este é o motivo pelo qual
`gdrom.h` do Dreamdrive contém respostas enlatadas.

---

## 5. Formato do pacote (§8.1)

✅ O packet tem **exatamente 12 bytes**, byte 0 = command code, bytes 1-11 = parâmetros.
**Não existe cabeçalho** de tipo/offset/size como em SCSI/OSD.

```
Byte 0      : Command code
Byte 1..11  : Command parameter
```

Estruturas exatas, extraídas do `union packet` do iceGDROM (`rv32/source/ide.c`), que espelha
1:1 as tabelas da spec:

```c
req_stat/req_mode/set_mode: { cmd, pad1, start_addr, pad2, alloc_len }
req_error                  : { cmd, pad[3], alloc_len }
get_toc                    : { cmd, select, pad, alloc_len_hi, alloc_len_lo }
req_ses                    : { cmd, pad1, session_nr, pad2, alloc_len }
cd_read                    : { cmd, flags, start_addr[3], pad[3], transfer_length[3] }
cd_read2                   : { cmd, flags, start_addr[3], pad, transfer_length[2], next_addr[3] }
get_scd                    : { cmd, format, pad, alloc_len_hi, alloc_len_lo }
cd_play                    : { cmd, ptype, start_point[3], pad1, reptime, pad2, end_point[3] }
cd_scan                    : { cmd, pad, dir, speed }
```

Tamanho da Allocation Length: **1 byte** para REQ_STAT/REQ_MODE/SET_MODE/REQ_ERROR/REQ_SES;
**2 bytes** (MSB em Byte3) para GET_TOC e GET_SCD.

> ❌ Não existe "osophans" nem "filenames" no protocolo SPI. Nomes de ficheiro só existem
> no formato do **disco** GD-ROM (`Disc Format Standard Specifications`), que é um documento
> separado. Ver [12](12-correcoes-ao-briefing.md).

---

## 6. CD_READ (30h) em detalhe

```
Byte 0    = 0x30
Byte 1    = [7:4] Data Select | [3:1] Expected Data Type | [0] Parameter Type
Byte 2-4  = Start Point (FAD 24-bit BE, ou MSF min/sec/frame)
Byte 5-7  = 0
Byte 8-10 = Transfer Length (nº de setores, 24-bit BE)
Byte 11   = 0
```

### 6.1 Data Select (Byte1[7:4])

| Bit7 | Bit6 | Bit5 | Bit4 | Significado |
|---|---|---|---|---|
| Header | Sub header | **Data** | Other | "Other"=1 → todos os 2352 bytes; se Data Type = CD-DA a seleção é inválida e 2352 bytes são sempre transferidos |

**Byte1[3:1] Expected Data Type**: `000`=Any, `001`=CD-DA (2352), `010`=Mode 1 (2048),
`011`=Mode 2 (2336), `100`=Mode 2 Form 1 (2048), `101`=Mode 2 Form 2 (2324),
`110`=Mode 2 non-XA (2336).

**Byte1[0]**: 0 = FAD, 1 = MSF.

Uso típico (driver Linux, modo PIO): `cmd[0]=0x30, cmd[1]=0x20` → 2048 bytes/setor.

🔶 O MAME aceita **apenas** `data_select == 2` ("Data") e faz `emu_fatalerror` nos demais
(`src/devices/bus/ata/gdrom.cpp`). Na prática os jogos só pedem `0x20`; implementar os outros
é dívida futura, não bloqueio.

> ❌ O protocolo SPI **não** tem "4 canais DATA/SUB/SUB2/FEC". O que a spec oferece é o
> seletor de 4 flags acima + a opção de 2352 bytes crus. O fator "4" que aparece em código
> vem do **GD-ROM de alta densidade**, onde setores de 2448 bytes são tratados como 4
> sub-blocos de 512 bytes — não são canais de CD.

Conversão MSF (implementada em `gdrom_utils.cpp` do Dreamdrive, e é fiel):

```c
uint32_t _gdrom_read_get_FAD(uint8_t* data, uint8_t msf) {
    if (msf) return (uint32_t)((data[0]*60*75) + (data[1]*75) + data[2]);
    return (uint32_t)((data[0] << 16) | (data[1] << 8) | data[2]);
}
```

---

## 7. Formatos de resposta

| Comando | Resposta | Tam. |
|---|---|---|
| **REQ_STAT** | `[0]`=STATUS(nibble), `[1]`=DiscFormat<<4 \| RepeatCount, `[2]`=Control/ADR, `[3]`=TNO, `[4]`=Index, `[5..7]`=FAD 24-bit BE, `[8]`=MaxReadErrorRetryTimes, `[9]`=0 | 10 |
| **REQ_MODE** | `[0-1]`=0, `[2]`=CD-ROM speed (0=MAX), `[3]`=0, `[4-5]`=Standby time (default `0x00B4`=180s), `[6]`=flags (bit5 ReadContinuous, bit4 ECC, bit3 ReadRetry, bit0 Form2ReadRetry), `[9]`=ReadRetryTimes (default `08h`), `[10-17]`=Drive name ASCII, `[18-25]`=System version ASCII, `[26-31]`=System date ASCII | 32 |
| **REQ_ERROR** | `[0]`=0xF0, `[2][3:0]`=Sense Key, `[4-7]`=Command specific info, `[8]`=ASC, `[9]`=ASCQ | 10 |
| **GET_TOC** | 102 entradas: tracks 1..99 (4 bytes cada: Control/ADR + FAD), `[396..399]` start track, `[400..403]` end track, `[404..407]` lead-out | **408** |
| **REQ_SES** | `[0]`=STATUS, `[1]`=0, `[2]`=nº de sessões (se 00 → total), `[3-5]`=Lead-out FAD | 6 |
| **GET_SCD** fmt 0 | `[0]`=rsvd, `[1]`=Audio status, `[2-3]`=len=0x0064, `[4..99]`=subcode P..W | 100 |
| **GET_SCD** fmt 1 | `[0]`=rsvd, `[1]`=Audio status, `[2-3]`=len=0x000E, `[4]`=Control/ADR, `[5..13]`=Q | 14 |

**Disc Format** (REQ_STAT byte1[7:4]): `0`=CD-DA, `1`=CD-ROM, `2`=CD-ROM XA/CD-Extra,
`3`=CD-I, **`8`=GD-ROM**.

**Audio status** (GET_SCD byte1): `00h` não suportado, `11h` tocando, `12h` pausado,
`13h` terminou normal, `14h` terminou anormal, `15h` sem informação (default).

✅ §8.2, CD_SCD: *"Because the subcode information is updated approximately every 13.3 ms,
the data may be wiped out unless the subcode is read out as fast as possible when subcode is
used."*

→ O subcode tem de ser gerado **continuamente** e não calculado sob demanda no instante do
pedido, senão o Q-subcode sai desatualizado. Este é um requisito de implementação, não um
detalhe.

**REQ_MODE é de risco alto.** Os 8 primeiros bytes são lidos por praticamente todo o jogo
(o driver Linux lê exatamente esse prefixo). Devolver ASCII real em [10-17], [18-25], [26-31].

**GET_TOC é de risco alto.** Byte1 `Select` tem de devolver **2 TOCs** para GD
(single-density + double-density) — é assim que o host descobre que é um disco de alta
densidade. 🔶 O `Disc::FillGDSession()` do Dreamdrive é hardcoded para sessão única e é uma
lacuna conhecida.

---

## 8. Códigos de status

A spec Sega **não** usa os nomes `STAT_*` / `NERR_*` — são apelidos do libGD (Marcus
Comstedt) e de emuladores. Mapeamento em `src/devices/bus/ata/gdrom.h` do MAME:

| Estado da unidade | Macro | Valor |
|---|---|---|
| BUSY | `GDROM_BUSY_STATE` | 0x0 |
| PAUSE | `GDROM_PAUSE_STATE` | 0x1 |
| STANDBY | `GDROM_STANDBY_STATE` | 0x2 |
| PLAY | `GDROM_PLAY_STATE` | 0x3 |
| SEEK | `GDROM_SEEK_STATE` | 0x4 |
| SCAN | `GDROM_SCAN_STATE` | 0x5 |
| OPEN | `GDROM_OPEN_STATE` | 0x6 |
| NODISC | `GDROM_NODISC_STATE` | 0x7 |
| RETRY | `GDROM_RETRY_STATE` | 0x8 |
| ERROR | `GDROM_ERROR_STATE` | 0x9 |
| FATAL | `GDROM_FATAL_STATE` | 0xA |

**Sense Key** (Tabela 3.2):

| | | | |
|---|---|---|---|
| `0h` NO SENSE | `1h` RECOVERED ERROR | `2h` NOT READY | `3h` MEDIUM ERROR |
| `4h` HARDWARE ERROR | `5h` ILLEGAL REQUEST | `6h` UNIT ATTENTION | `7h` DATA PROTECT |
| `8h`-`Ah` reservado | `Bh` ABORTED COMMAND | `Ch`-`Fh` reservado | |

**ASC/ASCQ** (Apêndice I, p.60-64) — tabela completa, incluindo `B4 00 7` lente/mídia
contaminada, `63 00 5` playback em lead-out, `92 00 5` header mode diferente, `B9 00 B` erro
de leitura com paragem do áudio, `BF 00 B` drive data buffer overflow.

---

## 9. Fluxos de comando (§7)

| Fluxo | Comandos | Natureza |
|---|---|---|
| §7.1 PIO data → host | REQ_STAT, REQ_MODE, REQ_ERROR, GET_TOC, REQ_SES, CD_READ, CD_READ2, GET_SCD | unknown-length, host lê Byte Count para saber o tamanho |
| §7.2 PIO data ← host | **SET_MODE** | known-length, host escreve |
| §7.3 DMA | CD_READ, CD_READ2 | unknown-length via DMARQ/DMACK — **vê [06b](06b-scope-pio-vs-dma.md)** |
| §7.4 Non-data | TEST_UNIT, CD_OPEN, CD_PLAY, CD_SEEK, CD_SCAN | sem dados |

Pontos fixos do fluxo (§7.1), idênticos ao que o iceGDROM implementa em `cmd_irq()` /
`data_irq()`:

1. Host espera `BSY=0 && DRQ=0`.
2. Host escreve Features / Sector Count / Byte Count (Drive Select).
3. Host escreve **`0xA0`** no Command register → drive seta BSY **em até 400 ns**.
4. Drive seta `CoD=1, IO=0, BSY=0, DRQ=1` → ✅ *"Command packet can be received"*.
5. Host escreve **6 palavras de 16 bits** no Data register.
6. Drive clear DRQ, seta BSY, lê Features + Byte Count.
7. Drive executa; para dados: seta Byte Count com o nº de bytes,
   `IO=1, CoD=0, DRQ=1, BSY=0`, **INTRQ=1**.
8. Host lê Status (**isso derruba INTRQ**), lê Byte Count, transfere dados.
9. Fim: `IO=CoD=DRDY=1, BSY=DRQ=0`, INTRQ=1. Host lê Status e, se CHECK, lê Error.

Valores de status concretos usados pelo iceGDROM (úteis como referência bit-exata):
`0x50` = DRDY (ok) · `0x58` = DRQ+DRDY · `0x51` = CHECK+DRDY · `0xD0` = BSY.

Referência de produção para este handshake: `gdrom_spicommand()` em
`drivers/cdrom/gdrom.c` do Linux.

---

## 10. Checklist de conformidade

| # | Requisito | Risco |
|---|---|---|
| 1 | Apresentar 7 registos de 8 bits + Data de 16 bits, com decode CS0/CS1/DA2..0 | Baixo |
| 2 | `0x50` a byte-clears; `0x58` para DRQ; `0x51` + `Error=0x04` para comando desconhecido | Baixo |
| 3 | Máquina de estados de §9 passo 1 (packet 12B, Byte Count, INTRQ) | Médio |
| 4 | REQ_MODE com os 32 bytes corretos | **Alto** |
| 5 | REQ_STAT campo-a-campo, FAD correto, TNO/Index derivados da TOC | **Alto** |
| 6 | GET_TOC 408 bytes, com `Select` a devolver 2 TOCs para GD | **Alto** |
| 7 | REQ_SES 6 bytes; REQ_ERROR 10 bytes com `0xF0` no byte 0 | Médio |
| 8 | TEST_UNIT a refletir `<BUSY>` durante troca de disco | Médio |
| 9 | CD_READ 2048 B/setor, transfer length 24-bit em Byte8-10, respeitar Data Select | **Alto** |
| 10 | CD_PLAY/CD_SEEK/CD_SCAN com CD-DA real em B22/A22/A23 | **Alto** (ver [04](04-clock-aica.md)) |
| 11 | GET_SCD formato 1 (Q, 14B) **e** formato 0 (P-W, 100B, CRC-16 CCITT) | Médio |
| 12 | Responder `0x70`/`0x71` e transicionar PAUSE/STANDBY corretamente | Médio |
| 13 | IOCS16- / DASP- irrelevantes; Drive Select = 0x00 | Baixo |
| 14 | IORDY para estender o ciclo quando o host pedir | Médio |
| 15 | Gerar 33.8688 MHz em B23 (MCK) | **Crítico — pré-requisito de bring-up** |

**Drive Select = 0x00, nunca 0x90.** O libKOS (`kernel/arch/dreamcast/include/dc/g1ata.h`)
documenta: *"the GD-ROM really does not like the reserved bits being set in the device select
register"*.

---

Ver também: [02-barramento-g1-pinout](02-barramento-g1-pinout.md) ·
[03-timing-ide](03-timing-ide.md) · [04-clock-aica](04-clock-aica.md) ·
[07-referencias-codigo](07-referencias-codigo.md)
