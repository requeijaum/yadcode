# 05 — Análise do Dreamdrive (khill25/Dreamdrive)

Repositório: https://github.com/khill25/Dreamdrive · Log: https://hackaday.io/project/190347
Subárvore analisada: `Dreamcast/sw/rp2350/`

## 1. Metadados

| Campo | Valor |
|---|---|
| `default_branch` | `main` |
| Linguagem declarada | C (mistura C11/C++17) |
| SDK | pico-sdk 2.1.0, toolchain 13.3, `PICO_BOARD pimoroni_pga2350` |
| `pushed_at` | 2025-04-08 |
| Stars / forks | 26 / 1 |
| `license.spdx_id` | `NOASSERTION` |

⚠️ **Licença dividida.** Os fontes de `Dreamcast/sw/rp2350/` trazem
`SPDX-License-Identifier: BSD-2-Clause`, mas o `README.md` da raiz remete para a
*"Dreamdrive Open Hardware License v1.0"* (não-comercial). **O firmware e o hardware têm
licenças diferentes.** Ver [09-riscos-e-licencas.md](09-riscos-e-licencas.md).

⚠️ **`PICO_BOARD pimoroni_pga2350` é um RP2350A (60-pin, 30 GPIO)**, mas o `rp2350_pins.h`
do próprio projeto pede GPIO 0–39. Inconsistência real — o board file do pico-sdk para
`pimoroni_pga2350` chega a definir `PIMORONI_PGA2350_PSRAM_CS_PIN 47`, que não existe num
RP2350A. O autor moveu-se depois para o **QFN-80** (a PCB no repo é
`RP2350-QFN-80-1EP_10x10`). Ver [10-viabilidade-pinos-e-pcb.md](10-viabilidade-pinos-e-pcb.md).

## 2. Estrutura de `Dreamcast/sw/rp2350/`

```
CMakeLists.txt              pico-sdk 2.1.0, board pimoroni_pga2350
CMakeLists-sub.txt
OVERVIEW.md                 pinout — DESATUALIZADO (descreve a arquitetura de 2 MCUs RP2040)
Sega Pack Interface.md      transcrição da spec Sega (27 KB)  ⭐
rp2350.cpp                  firmware principal (63 KB, 1877 linhas)
rp2350_pins.h               pinout GPIO                       ⭐
ide_handling.pio            4 programas PIO                   ⭐
gdrom.h / gdrom.c           respostas enlatadas / gdrom.c = 0 BYTES
sega_packet_interface.c/.h  enums de registos e comandos       ⭐
lib/nulldc/ImgReader/       common.{h,cpp}, gdi.{h,cpp}, SCSIDEFS.H
utils/                      gdrom_utils.{h,cpp}, sd_utils.{h,cpp}, hw_config.c
```

### 2.1 Lacunas de build confirmadas

1. `CMakeLists.txt` faz `include(pico_sdk_import.cmake)` — **não existe** na árvore.
2. Faz `add_subdirectory(lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src)` — **não existe** em
   `sw/rp2350/`. A única cópia do driver está em `sw/rp2040/mcu1/lib/sdcard/no-OS-FatFS-SD-SPI-RPi-Pico/`
   (nome ligeiramente diferente: `SD-SPI`, não `SD-SDIO-SPI`).
3. `gdrom.c` tem **0 bytes**.
4. `gdrom_utils.h` não é header puro: contém `static uint8_t gdrom_rom_data[] = {...}` com
   ~4 KB de dados crus de `IP.BIN` embutidos.

→ **O build não é reproduzível a partir de `main`.** Isto não é um problema se vamos
reescrever, mas significa que não se pode simplesmente clonar e compilar.

## 3. Pinout GPIO — `rp2350_pins.h`

```c
#define DATABUS_D0 0  // D0-D15 on GPIO 0-15
#define PIN_A0 16
#define PIN_A1 17
#define PIN_A2 18
#define PIN_CS0 19
#define PIN_CS1 20
#define PIN_RD 21
#define PIN_WR 22
#define PIN_IORDY 23
#define PIN_INTRQ 24
#define PIN_DMARQ 25
#define PIN_DMACK 26
#define PIN_CD_LRCK 27
#define PIN_CD_BCK 28
#define PIN_CD_SDAT 29
#define PIN_CDDA_CLK 30
#define PIN_CD_EMPH 31
#define PIN_DOPEN 32
#define PIN_GD_RST 33
#define PIN_SD_CLK 34
#define PIN_SD_CMD 35
#define PIN_SD_D0 36
#define PIN_SD_D1 37
#define PIN_SD_D2 38
#define PIN_SD_D3 39

#define CS_PINS_MASK        (0x18)
#define READ_WRITE_PIN_MASK (0x60)
#define READ_PIN_MASK       (0x40)
#define WRITE_PIN_MASK      (0x20)
#define REGISTER_PIN_MASK   (0x7F)
```

**O layout é GPIO 0..39 contíguo, sem pinos pulados.** Isto é deliberado: o PIO só vê
GPIO 0–31 por bloco, e todo o barramento IDE (0–26) cai dentro.

⚠️ **Inconsistência real que o autor não resolveu:** `PIN_CD_BCK` (28) e `PIN_CD_SDAT` (29)
caem em PIO0, mas `setup_squarewave_generator()` opera em **`pio2`**. E
`setup_squarewave_generator()` tem `pio_sm_set_clkdiv` **comentado** — ver §6.

### 3.1 Otimização de latência a reter

```c
// Set Input Bypass on the cs0, cs1, rd, wr pins for faster edge response
for (int i = PIN_CS0; i <= PIN_WR; i++) {
    pio0->input_sync_bypass |= (1 << i);
}
```

3 linhas, ganho grande. **Generalizar para todos os pinos de strobe.**

## 4. Os 4 programas PIO — `ide_handling.pio`

### 4.1 `ata_bus_handler`

```pio
.program ata_bus_handler
.side_set 1 opt
.wrap_target
    mov pindirs, null side 0 [7]   ;; pinos como entradas
check_cs_lines:
    mov x, null
    mov y, null
    mov osr, pins
    out null, 19        ;; descarta D0-15 + A0-A2
    out y, 1            ;; lê CS0
    out x, 1            ;; lê CS1
    jmp x!=y check_rd_rw
    jmp check_cs_lines
...
read_from_dreamcast:
    mov isr, x
    push                 ;; pede dados ao core0
    wait 1 gpio 22 side 1 [7]   ;; espera WR subir
    jmp check_cs_lines side 0 [7]

write_to_dreamcast:
    mov pindirs, ~null
    pull                 ;; pega dados do core0
    out pins, 32
    wait 1 gpio 21 side 1 [7]   ;; espera RD subir
.wrap
```

⚠️ **Limitação importante:** o `ata_bus_handler` **não usa `/RD` do host como clock de
shift** — usa apenas `/WR` como `jmp pin` e um strobe manual. Isto é frágil, e é
provavelmente a causa de parte dos hangs documentados no histórico de commits.

⚠️ Também **não declara `.pio_version 1`**, que o SDK v2 usa por omissão e que o ZuluIDE
declara explicitamente.

### 4.2 `dma_bus_handler`

```pio
.program dma_bus_handler
.side_set 1 opt
.wrap_target
setup:
    pull                ;; número de transferências
    mov x, osr
    pull                ;; primeira palavra
    set pins, 1         ;; asserta DMARQ
transfer_data:
    wait 0 gpio 21 side 0     ;; strobe em RD (PIO faz o clock do host!)
    out pins, 16 side 1
    pull
    wait 1 gpio 21
    jmp x-- transfer_data
finish:
    set pins, 0 side 0
    push
.wrap
```

🔶 **O PIO gera o clock do host.** Isto é o caminho de DMA, e está **inacabado** — ver
[06b-scope-pio-vs-dma.md](06b-scope-pio-vs-dma.md).

### 4.3 `square_wave_generator` — ver §6, está partido

## 5. Camada de registos / Pack Interface

Abordagem elegante: **tabela de endereçamento direto por valor de controlo**, sem máquina
de estados.

```c
// Register Index = Bits = W, R, CS1, CS0, A2, A1, A0 (most->least)
uint16_t* registerIndex_map[128] = {0};

registerIndex_map[0x4E] = &SPI_registers[SPI_STATUS_REGISTER_INDEX];        // read
registerIndex_map[0x2E] = &SPI_registers[SPI_DEVICE_CONTROL_REGISTER_INDEX];// write
registerIndex_map[0x50] = &SPI_registers[SPI_DATA_REGISTER_INDEX];         // read
registerIndex_map[0x30] = &SPI_registers[SPI_DATA_REGISTER_INDEX];         // write
...
registerIndex_map[0x57] = &SPI_registers[SPI_STATUS_REGISTER_INDEX];        // read
registerIndex_map[0x37] = &SPI_registers[SPI_COMMAND_REGISTER_INDEX];      // write
```

Com os valores "coded" para filtragem no core0:
```c
#define CODED_DATA_REGISTER_READ  (0x50)
#define CODED_DATA_REGISTER_WRITE (0x30)
#define CODED_STATUS_REGISTER_READ (0x57)
#define CODED_COMMAND_REGISTER_WRITE (0x37)
```

⭐ **Copiar e estender.** É a ideia mais reutilizável de todo o projeto.

### 5.1 Arquitetura de dois núcleos

- **core1** = `ide_register_controller_main()` → `process_ata_register_access()` →
  `_process_ata_register_access()`: loop apertado (`tight_loop_contents()`) consumindo o RX
  FIFO do PIO.
- **core0** = `main_processing_loop()`: consome `multicore_fifo` (eventos do core1),
  despacha comandos Sega e executa transferências.
- `__not_in_flash_func(process_ata_register_access)` põe o loop crítico em RAM.

```c
// Set clock speed to 266MHz (3.76ns per cycle)
const int freq_khz = 266000;
bool clockWasSet = set_sys_clock_khz(freq_khz, false);
```

⚠️ **266 MHz é overclock.** Ver [04 §4.1](04-clock-aica.md): o máximo por spec é
228,57 MHz (VCO 1600 / postdiv1 7). Funciona na prática, mas não é o valor a escolher.

### 5.2 Máquina de estados do packet Sega — `process_packet()`

| Cmd | Nome | Estado |
|---|---|---|
| 0x00 | TEST_UNIT | ✅ parcial |
| 0x10 | REQ_STAT | ✅ |
| 0x11 | REQ_MODE | ⚠️ apenas 2 casos especiais |
| 0x12 | SET_MODE | ⚠️ não faz nada |
| 0x13 | REQ_ERROR | ❌ vazio |
| 0x14 | GET_TOC | ✅ |
| 0x15 | REQ_SES | ✅ |
| 0x16 | CD_OPEN | ❌ vazio |
| 0x20 | CD_PLAY | ❌ vazio |
| 0x21 | CD_SEEK | ❌ vazio |
| 0x22 | CD_SCAN | ❌ vazio |
| 0x30 | CD_READ | ✅ (PIO + DMA) |
| 0x31 | CD_READ2 | ❌ "Not implemented" |
| 0x40 | GET_SCD | ⚠️ parcial, respostas enlatadas |
| 0x70 | Code70 | ✅ cai em TEST_UNIT |
| 0x71 | Code71 | ✅ resposta enlatada `cmd71_reply` |

## 6. Clock de 33.8688 MHz — NÃO IMPLEMENTADO

O único lugar onde 33,8688 MHz aparece é um **comentário morto**, com aritmética
internamente inconsistente:

```c
// float bClkDivider = 133;//266000000.0 / (33868800 * 2); //7.85
```

E o programa PIO correspondente (`square_wave_generator`) opera **sem clkdiv**, logo na
frequência do sysclk. Verificação: com sysclk 266 MHz, `/4` daria 66,5 MHz — não é
33,8688 MHz. Além disso `266/33.8688 = 7,85` não é representável num clkdiv inteiro
(1/256ths: 7,8125 ou 7,875).

O autor deixou `pio_sm_set_clkdiv` **comentado**.

→ **O caminho de CD-DA está inerte.** O commit de 2025-03-17 diz literalmente
*"Next up is getting CDDA working"*.

Solução correcta em [04 §4.2](04-clock-aica.md).

## 7. Respostas enlatadas — `gdrom.h` / `gdrom_utils.h`

```c
uint16_t reply_a1[] = { 0x2020,0x0020,... };   // "ES   DC-ROM  .634"
uint16_t reply_11[] = { ...,0x4553,0x2020, 0x2020,0x6552,0x2076,0x2e36,0x3334,0x3939,0x3430,0x3830 };
                                                     // "SE  Ru v.639408"
static const uint8_t cmd71_reply[] = { 0xba, 0x06, 0x0d, 0xca, 0x6a, 0x1f };
```

`gdrom_version[] = "Rev 5.07"`, TOC completo de 408 bytes em `gdrom_utils.h`, mais ~4 KB
de `IP.BIN` cru.

⭐ **Alto valor.** Poupa semanas de engenharia reversa. ⚠️ Ver §9 sobre o IP.BIN e a
questão legal.

## 8. Leitura de imagem e CD-DA

`utils/gdrom_utils.cpp` decodifica os parâmetros do CD_READ fielmente à spec:

```c
gdrom_read_data_select_value = (packet[1] & 0xFF00) >> 8;
gdrom_read_expected_data_type = (packet[1] & 0xE) >> 1;
gdrom_read_data_parameter_type = (packet[1] & 0x1;

gdrom_read_sector_size   = _gdrom_get_sector_type(...);   // 2048 ou 2340
gdrom_read_start_sector  = _gdrom_read_get_FAD(&packet[2], isMSF);
gdrom_read_remaining_sectors = (packet[8]<<16)|(packet[9]<<8)|packet[10];
```

Buffer duplo com pré-leitura (`MAX_BUFFERED_SECTORS = 16` × 2352 B = 37 632 B, dois
buffers ≈ 75 KB):

```c
void gdrom_fill_read_buffer(uint8_t* buffer) {
    uint32_t count = (gdrom_read_remaining_sectors > MAX_BUFFERED_SECTORS)
                     ? MAX_BUFFERED_SECTORS : gdrom_read_remaining_sectors;
    current_disc->ReadSectors(gdrom_read_start_sector + gdrom_read_read_sectors,
                              count, buffer, gdrom_read_sector_size);
    gdrom_read_read_sectors += count;
    gdrom_read_remaining_sectors -= count;
}
```

⭐ **A lógica de preenchimento/consumo está pronta e testada em campo.** É o padrão
"t2i ≤ 70 ns" do ATA-3 posto em prática — ver [03 §6.1](03-timing-ide.md) sobre `tB`.

⚠️ **Patch hardcoded do IP.BIN, que o autor não compreende:**

```c
if (current_disc->type == GdRom && sector == 45150 && sectorCount == 7) {
    PatchRegion_0(buffer, sectorSize);
    PatchRegion_6(buffer + 2048 * 6, sectorSize);
}
```

⚠️ **`Disc::FillGDSession()` é hardcoded para sessão única** — GD-ROM multi-sessão não
suportado, apesar de `REQ_SES` responder.

## 9. Cartão SD

**SPI0, não SDIO**: `.sck_gpio = 34, .mosi_gpio = 35, .miso_gpio = 36`, `ss_gpio = 39`.

```c
.baud_rate = 125 * 1000 * 1000 / 2   // 62,5 MHz
```

⚠️ Com o comentário do autor: *"Not even sure if it will go that fast"*. Alternativas
comentadas: 66 / 15,625 / 20,8 / 31,25 MHz. O bloco SDIO está comentado em `hw_config.c`.

`sd_utils.cpp` contém: *"TODO: Create a read buffer… so we can dma? How do we read this
from main to send to DC?"*

⚠️ **62,5 MHz em SPI é frágil.** O RP2350 tem **SDIO nativo** — o ZuluIDE usa-o
(`sdio_rp2350_config.h`). Migrar.

## 10. Maturidade — o que o histórico de commits revela

Mensagens literais dos commits em `Dreamcast/sw/rp2350/`:

1. `1d95b63` (2025-03-17) — **"Soul Calibur booting!!!!"**
2. `c5e33ae` (2025-03-15) — "ITS WORKS — Just need to clean up all the debug info. Next up is getting CDDA working."
3. `76f9552` — "Changed size of gdrom read buffer to be 64 sectors. This is causing a hang (maybe a crash…) on the first 'big' transfer of 717 sectors."
4. `caae120` — "Struggling to get games to load. Soul Calibur loads data but spams the TOC call."
5. `9f5f9a7` — "Trying icegdrom subcode command implementation but that doesn't seem to get me past the 'loading crazy taxi (15k)' screen."
6. `6b77e03` — "Currently after the 5th dma finishes, the ata bus handler loop doesn't ever restart."
7. `e9fdfb0` — "IT LIVES! We get to the sega logo screen!!!"
8. `ac44b46` — "Stable? DC will send command 71 then command 0… I must be missing something with the way the data is formatted."

⚠️ **Fragmentos de debug mantidos em código de produção:**

```c
if(writtenRegisterIndex < 5000) {
    writtenRegisters[writtenRegisterIndex++] = 0xDEADBEEF;
}
```

E um comentário revelador sobre o registo Data (o mais crítico):
```c
// This doesn't appear to be working as expected and I have no idea why.....
```

**Veredicto:** protótipo funcional de baixo nível, ainda não confiável. O autor é honesto
sobre as pendências. Última mensagem do `README.md`:
`| Dreamdisc | Dreamcast | Dreamdive ODE For Dreamcast | Prototype/v3 | Repo files wont boot yet |`

## 11. Tabela de reutilização

| # | Item | Caminho | Avaliação |
|---|---|---|---|
| A1 | Programas PIO IDE | `Dreamcast/sw/rp2350/ide_handling.pio` | ⭐⭐ Altíssimo (com as ressalvas de §4) |
| A2 | Tabela `registerIndex_map[128]` | `rp2350.cpp` | ⭐⭐ Altíssimo |
| A3 | `input_sync_bypass` | `rp2350.cpp` | ⭐⭐ Altíssimo, 3 linhas |
| A4 | Enums de registos / bits / sense keys | `sega_packet_interface.h` | ⭐⭐ Altíssimo |
| A5 | Os 16 opcodes do packet | `sega_packet_interface.h` | ⭐⭐ Altíssimo |
| A6 | Transcrição da spec Sega (27 KB) | `Sega Pack Interface.md` | ⭐⭐ Altíssimo |
| A7 | Máquina de estados CD_READ + duplo buffer | `rp2350.cpp`, `gdrom_utils.cpp` | ⭐⭐ Muito alto, testado em campo |
| A8 | Parser GDI + leitura por track | `lib/nulldc/ImgReader/` | ⭐ Muito alto, mas reescrever |
| A9 | Respostas enlatadas | `gdrom.h`, `gdrom_utils.h` | ⭐ Alto ⚠️ ver §9/IP.BIN |
| A10 | Pinout RP2350B | `rp2350_pins.h` | ⭐ Alto, se a PCB coincidir |
| A11 | Decodificação CD_READ | `gdrom_utils.cpp` | ⭐ Alto, fiel à spec |
| A12 | Config SD | `utils/hw_config.c` | ⚠️ Médio. Migrar para SDIO |
| A13 | Driver FatFs | `sw/rp2040/mcu1/lib/sdcard/…` | ⚠️ Médio |

---

Ver também: [06-analise-zuluide](06-analise-zuluide.md) ·
[06b-scope-pio-vs-dma](06b-scope-pio-vs-dma.md) · [07-referencias-codigo](07-referencias-codigo.md)
