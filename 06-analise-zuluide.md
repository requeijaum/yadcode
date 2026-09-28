# 06 — Análise do ZuluIDE (ZuluIDE/ZuluIDE-firmware)

Repositório: https://github.com/ZuluIDE/ZuluIDE-firmware

## 1. Metadados

| Campo | Valor |
|---|---|
| Descrição | *"ZuluIDE is an ATAPI/ATA CD-ROM emulator that can also emulate rigid PATA HDDs, as well as ATA Zip/Removable media"* |
| Stars / forks | 131 / 12 |
| Issues abertas | 24 |
| Último push | 2026-09-22 |
| Linguagem | C++ |
| Toolchain | PlatformIO + pico-sdk (RP2040 e RP2350B) |
| Licença | **GPL-3.0 + ZuluIDE Hardware Support Library Exception** |

⚠️ **GPL-3.0 é muito mais restritiva que a BSD-2 do Dreamdrive.** Se o objetivo for
código aberto permissive, não se pode copiar código do ZuluIDE. Ver
[09-riscos-e-licencas.md](09-riscos-e-licencas.md).

## 2. A descoberta estrutural mais importante

> **No RP2350, o PIO que implementa o barramento IDE NÃO está em código aberto.**

A rationale está documentada em `lib/ZuluIDE_platform_RP2350/rp2350_ide_phy.cpp`. O RP2350B
tem **dois contextos de segurança**: o Core0 corre em modo **Non-Secure** e **não pode
aceder aos periféricos IDE**; o Core1 corre em modo **Secure** e contém a implementação
proprietária, distribuída como binário:

```
lib/ZuluIDE_platform_RP2350/
├── rp2350_ide_phy.cpp                  camada física (21 KB)
├── zuluide_rp2350b_core1.h             API para core1 proprietário
├── libzuluide_rp2350b_core1_encrypted.a  33 KB — CÓDIGO FECHADO
├── rp2350_iocs16.pio                   PIO: sinal IOCS16 (aberto)
├── rp2350_sniffer.{h,cpp} + .pio       PIO: sniffer de diagnóstico (aberto)
├── ZuluIDE_platform.{h,cpp}            40 KB
├── ZuluIDE_platform_gpio.h             pinout
└── sdio_rp2350_config.h
```

O handshake core0→core1 é por bitfield com acquire/release:

```c
static void ide_phy_post_request(uint32_t request)
{
    __atomic_or_fetch(&g_idecomm.requests, request, __ATOMIC_ACQ_REL);
    IDE_PIO->irq_force = (1 << IDE_CORE1_WAKEUP_IRQ);
}
```

Com contrato de latência explícito:
```c
#define CORE1_RESPONSE_DELAY 100   // µs
```

> *"In general core1 processes requests within 1µs. The `CORE1_REQ_BUSY` indicates a state
> where the request bit has been cleared from `g_idecomm.requests` but the interrupt handler
> has not yet fully completed."*

**Consequência:** o ZuluIDE **não** é uma referência para o PIO do barramento IDE. É uma
referência para **padrões de engenharia** (ver §5).

## 3. Estrutura de `src/`

```
ZuluIDE.cpp / .h
ide_protocol.{h,cpp}      state machine ATA/ATAPI de nível superior
ide_phy.h                 API de camada física, independente de plataforma  ⭐⭐
ide_atapi.{h,cpp}         device ATAPI genérico
ide_cdrom.{h,cpp}         CD-ROM ATAPI (READ CD, TOC, …)
ide_rigid.{h,cpp}  ide_removable.{h,cpp}  ide_zipdrive.{h,cpp}
ide_constants.h  atapi_constants.h        ⭐ referência canónica
ide_imagefile.{h,cpp}  ide_utils.{h,cpp}
ZuluIDE_log.{h,cpp}  ZuluIDE_config.h  ZuluIDE_msc.*  ZuluIDE_usb_console.*
```

## 4. `rp2350_iocs16.pio` — o único PIO IDE aberto

Problema descrito no próprio ficheiro:

> *"Because it is on GPIO 32, it cannot be driven by the main IDE PIO which only has access
> to GPIO 0-31."*

O programa:

```pio
.program rp2350_iocs16
.pio_version 1
.out 0 right auto 8

wrong_addr:
    set pins, 1                 ; Deassert IOCS16

.wrap_target
check_addr:
    out x, 8                    ; Autopull, get address byte
    jmp x != y, wrong_addr      ; Compare against data register address

right_addr:
    set pins, 0                 ; Assert IOCS16
.wrap
```

Carregamento no core0:
```c
pio_sm_put(IOCS16_PIO, IOCS16_SM, 0x40); // Data register address
pio_sm_exec(IOCS16_PIO, IOCS16_SM, pio_encode_pull(false, false));
pio_sm_exec(IOCS16_PIO, IOCS16_SM, pio_encode_out(pio_y, 8));
```

O offset 0 é fixo de propósito, para não colidir com o SDIO:
> *"The PIO is shared with SDIO and the offset is fixed to avoid problems when SDIO
> dynamically switches PIO programs."*

⚠️ Honestidade do autor sobre a conformidade:
> *"IOCS16 signaling for PIO data transfer implementation is not completely to spec on the
> V2. Not all systems care so this allows the user to disabled it…"*

⭐ **O `.pio_version 1` e a técnica de cruzar o limite GPIO 31→32 são diretamente
reutilizáveis.** (Para o Dreamcast o problema não se põe: não há pinos para `IOCS16-`.)

## 5. `ide_phy.h` — a parte mais reutilizável

O contrato de transferência em blocos é limpo e bem documentado:

> 1. Device chama `ide_phy_start_write()` para definir o tamanho do bloco.
> 2. … `ide_phy_write_block()`; a PHY põe status `DEVRDY | DATAREQ` e asserta interrupt.
> 3. O host lê o registo Data para transferir.
> 4. No fim do bloco, a PHY põe status `BSY`.
> 5. O device pode esperar `IDE_EVENT_DATA_TRANSFER_DONE` ou fazer poll de
>    `ide_phy_is_write_finished()`.

Correção conforme T13/1410D rev 3a:
```c
// ATA version of start_read() differs in that it does not assert IRQ
// for the first data block. See figure 28 in T13/1410D revision 3a.
void ide_phy_start_ata_read(uint32_t blocklen, int udma_mode = -1);
```

Double-buffering com o próximo bloco pedido antes do consumo do atual:
```c
if (continue_transfer) { data_out_give_next_block(false); }
```

Watchdog de 5 s por transferência:
```c
if (!status && (uint32_t)(millis() - g_ide_phy.transfer_block_start_time) > 5000)
{
    logmsg("ide_phy_can_read_block() detected transfer timeout");
    g_ide_phy.watchdog_error = true;
}
```

### 5.1 Padrão de blocos via FIFO inter-core

```c
#define IDECOMM_MAX_BLOCKSIZE 8192
#define IDECOMM_MAX_BLOCK_PAYLOAD 4096
#define IDECOMM_BUFFERCOUNT 8
#define IDECOMM_DATA_PATTERN 0x80060000
#define IDECOMM_DATAFORMAT_PIO(x) ((x) | IDECOMM_DATA_PATTERN)

sio_hw->fifo_wr = (uint32_t)block;         // ponteiro trafega pela FIFO SIO de 32 bits
ide_phy_post_request(CORE1_REQ_START_DATAIN);
```

O payload é comprimido de 16→32 bits (2 palavras de 16 bits por palavra de 32, com marker
`0x8006` nos bits altos) para reduzir tráfego de FIFO, com unroll ×4.

## 6. Parseamento de comandos — `ide_atapi.cpp`

Nível 1, comandos ATA (`handle_command`):
```c
case IDE_CMD_IDENTIFY_DEVICE: [[fallthrough]];
case IDE_CMD_READ_SECTORS:   [[fallthrough]];
case IDE_CMD_READ_SECTORS_EXT:
    return set_device_signature(regs, IDE_ERROR_ABORT, false);
case IDE_CMD_IDENTIFY_PACKET_DEVICE:     return cmd_identify_packet_device(regs);
case IDE_CMD_PACKET:                     return cmd_packet(regs);
...
```

Nível 2, recepção do PACKET de 12 bytes:
```c
uint8_t cmdbuf[12] = {0};
ide_phy_read_block(cmdbuf, sizeof(cmdbuf));
dbgmsg("-- ATAPI command: ", get_atapi_command_name(cmdbuf[0]), " ", bytearray(cmdbuf, 12));
return handle_atapi_command_wrapper(cmdbuf);
```

Nível 3, dispatch com Unit Attention / Not Ready. Bypass de unit attention:
```c
case ATAPI_CMD_TEST_UNIT_READY:                   return atapi_test_unit_ready(cmd);
case ATAPI_CMD_INQUIRY:                           return atapi_inquiry(cmd);
case ATAPI_CMD_REQUEST_SENSE:                     return atapi_request_sense(cmd);
case ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION:     return atapi_get_event_status_notification(cmd);
case ATAPI_CMD_START_STOP_UNIT:                   return atapi_start_stop_unit(cmd);
```

Nível 5, CD-ROM — parse do `READ CD` (equivalente SCSI MMC-4, referência útil para o
`CD_READ` Sega):
```c
bool IDECDROMDevice::atapi_read_cd(const uint8_t *cmd)
{
    uint8_t  sector_type   = (cmd[1] >> 2) & 7;
    uint32_t lba           = parse_be32(&cmd[2]);
    uint32_t blocks        = parse_be24(&cmd[6]);
    uint8_t  main_channel  = cmd[9];
    uint8_t  sub_channel   = cmd[10];
    return doReadCD(lba, blocks, sector_type, main_channel, sub_channel, false);
}
```

## 7. Timing — responde a "PIO mode 4?"

**Resposta: não. O máximo declarado é PIO 3.**

```c
static ide_phy_capabilities_t g_ide_phy_capabilities = {
    .max_blocksize = IDECOMM_MAX_BLOCKSIZE,
    .supports_iordy = true,
    .max_pio_mode = 3,
    .min_pio_cycletime_no_iordy   = 240,   // ns
    .min_pio_cycletime_with_iordy = 180,   // ns
    .max_udma_mode = 2,
};
```

⚠️ `min_pio_cycletime_with_iordy = 180 ns` **coincide exactamente com o t0 do PIO-3 do
ATA-3** que medimos em [03 §3](03-timing-ide.md), e com os 2880 ns/32 B do Sega. Isto é uma
validação independente de que **PIO-3 é a classe correcta** para IDE de CD a 16 bits.

Capacidades publicadas no `IDENTIFY PACKET DEVICE`:
```c
if (m_phy_caps.supports_iordy) idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] |= (1 << 11);
idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1]   = (m_phy_caps.max_pio_mode << 8);
idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN]   = m_phy_caps.min_pio_cycletime_no_iordy;
idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] = m_phy_caps.min_pio_cycletime_with_iordy;
```

Mudança de modo PIO tem efeito real no RP2350 (ao contrário do RP2040/FPGA):
```c
if (pio_mode != g_idecomm.pio_mode) {
    g_idecomm.pio_mode = pio_mode;
    ide_phy_post_request(CORE1_REQ_CHANGE_PIO_MODE);
}
```

## 8. Tabela de reutilização

| # | Item | Caminho | Avaliação |
|---|---|---|---|
| B1 | PIO para IOCS16 | `rp2350_iocs16.pio` | ⭐⭐ `.pio_version 1`, cross-boundary GPIO 31→32 |
| B2 | **API `ide_phy.h`** | `src/ide_phy.h` | ⭐⭐ Melhor abstração IDE open-source vista |
| B3 | Mecânica core0↔core1 | `rp2350_ide_phy.cpp` | ⭐ Muito alto (mas GPL) |
| B4 | Packer 16→32 bits para FIFO | `zuluide_rp2350b_core1.h` | ⭐ Muito alto |
| B5 | `ide_atapi.cpp` (genérico) | `src/ide_atapi.cpp` | ⭐ Alto — patterns de dispatch |
| B6 | Constantes ATAPI/IDE | `atapi_constants.h`, `ide_constants.h` | ⭐ Alto, referência canónica |
| B7 | Parser CUE | `lib/SharedCUEParser/` | ⭐ Alto (GD-ROM = CUE) |
| B8 | `doReadCD` | `src/ide_cdrom.cpp` | ⭐ Alto — pregap, multi-arquivo |
| B9 | PIO SDIO | `ZuluIDE_platform_RP2040/rp2040_sdio.pio` | ⭐ Médio/alto, funciona no RP2350 |
| B10 | `ide_protocol.cpp` | `src/ide_protocol.cpp` | ⚠️ Médio — lógica DIAG/DASP é PC-IDE |
| B11 | **PIO do IDE bus** | `libzuluide_rp2350b_core1_encrypted.a` | ❌ **NÃO reutilizável.** Código fechado |
| B12 | Bitstream FPGA | `fpga_bitstream.h` (440 KB) | ❌ **NÃO reutilizável.** Restrito a hardware RHC |

## 9. O que adotar de qualquer forma (independente de licença)

1. **Watchdog de 5 s por transferência.** O histórico do Dreamdrive tem múltiplos hangs
   (*"after the 5th dma finishes, the ata bus handler loop doesn't ever restart"*) que um
   watchdog teria tornado diagnosticáveis.
2. **`max_blocksize` negociável** (≤ 4096 B). O Dreamdrive usa 37 KB fixos; para GD-ROM,
   streaming por blocos de ~2 KB libertaria RAM para PSRAM.
3. **Packer de 16→32 bits** para a FIFO inter-core.
4. **`pio0->input_sync_bypass`** — já no Dreamdrive, mas generalizar a todos os strobes.
5. **`.pio_version 1`** em todos os `.pio` — o Dreamdrive não declara.
6. **Contrato de latência explícito e documentado** entre as camadas, com constante única.
7. **SDIO nativo** em vez de SPI a 62,5 MHz.

---

Ver também: [05-analise-dreamdrive](05-analise-dreamdrive.md) ·
[08-arquitetura-proposta](08-arquitetura-proposta.md) ·
[09-riscos-e-licencas](09-riscos-e-licencas.md)
