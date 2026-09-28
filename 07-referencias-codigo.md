# 07 — Referências de código

Mapa dos ficheiros que contêm a lógica da SPI / G1, para saber onde ir buscar cada coisa.

---

## 1. iceGDROM — a melhor referência para um ODE em MCU

https://github.com/mrlxx/iceGDROM (o riser é de `zeldin/iceGDROM`)

| Ficheiro | Conteúdo |
|---|---|
| **`rv32/source/ide.c`** (14,7 KB) | ⭐⭐ **O ficheiro central.** `union packet` com as estruturas de 12 bytes exactas; `process_packet()` (switch de `0x00`/`0x11`/`0x12`/`0x13`/`0x14`/`0x15`/`0x20`/`0x21`/`0x30`/`0x40`/`0x70`/`0x71`); `cmd_irq()` (0xA0 → `IDE_ALT_STATUS=0x58`, `IDE_IOTARGET=0x05` = 6 palavras); `data_irq()`; `packet_data_last()` / `packet_data_dma()` (programam `IDE_SECCNT`/`IDE_CYLHI`/`IDE_CYLLO` para codificar IO/CoD e o nº de bytes); `do_req_mode()`, `do_set_mode()`, `do_req_error()`, `service_get_toc()`, `service_req_ses()`, `service_cd_read()`, `service_cd_read_cont()`, `service_cd_playseek()`, `service_cd_scd()` (gera subcode P–W com **CRC-16 CCITT**, polinómio `0x1021`); `do_cmd71()`; `set_secnr()` |
| **`rv32/source/hardware.h`** | Mapa MMIO do core IDE: `IDE_STATUS`, `IDE_ERROR`, `IDE_IOCONTROL`, `IDE_IOPOSITION`, `IDE_ALT_STATUS`, `IDE_IOTARGET`, `IDE_DEVCON`, `IDE_FEATURES`, `IDE_SECCNT`, `IDE_SECNR`, `IDE_CYLLO`, `IDE_CYLHI`, `IDE_DRVHEAD`, `IDE_COMMAND`, e o **buffer de dados de 512 B**. Modelo de expor task-file + buffer por uma única janela |
| `rv32/source/ide.h` | API: `service_ide()`, `set_disk_type()`, `set_secnr()` |
| `rv32/source/cdda.c` / `.h` | CD-DA: `cdda_start(blk, eblk, reptime)`, `cdda_get_status()`, `cdda_subcode_q[12]`, `cdda_active` (alimenta o bit 3 do Sector Number) |
| `rv32/source/imgfile.c` / `.h` | `imgfile_seek(FAD, flags)`, `imgfile_read_next_sector()`, `imgfile_data_offs`/`imgfile_data_len`, `imgfile_sector_complete()`, `toc[]`, `num_tocs`/`num_sessions` (áreas single/double-density do GET_TOC byte1) |
| **`fpga/source/ide/ide_interface.v`** (14 KB) | ⭐⭐ A máquina de estados do lado G1 (decode CS0/CS1, strobes, IORDY, buffer, IRQ) |
| `fpga/source/ide/ide_reset_generator.v` | Geração de reset |
| **`fpga/source/top.v`** | ⭐ `REFCLK_FREQ = 11289600`, **`CDCLK_FREQ = 33868800`**, PLL `clkgen` |
| `fpga/source/top.pcf` | Pinout do FPGA ↔ sinais G1 (nome de rede = nome de sinal G1) |
| **`pcb/riser/riser.sch` + `riser.kicad_pcb` + `Molex_52602_0579.lib`** | ⭐⭐ **Pinout completo do G1** — foi daqui que veio a tabela de [02](02-barramento-g1-pinout.md) |
| `test/source/benchmark.c`, `dmatest.c`, `cdops.c`, `cddatest.c` | ⭐ Testes de throughput, DMA e CD-DA. Referência para validar o RP2350 |

⭐ **PIO e DMA intercambiáveis** via `IDE_IOCONTROL`: `0x01` PIO in, `0x02` PIO out, `0x03`
PIO bidir, `0x04` DMA out, `0x80` preload. E `IDE_FEATURES & 1` decide PIO vs DMA.

## 2. NullDC / Flycast — comportamento de referência

| Ficheiro | Conteúdo |
|---|---|
| **`nulldc/nullDC/dc/gdrom/gdromv3.cpp`** | ⭐⭐ `gd_process_spi_cmd()` com o switch completo de `SPI_*`; `gd_spi_pio_end()` / `gd_spi_pio_read_end()`; `gds_readsector_pio` / `gds_readsector_dma`; tratamento de `0x70`/`0x71`; `REQ_STAT` campo a campo; `REQ_ERROR` (`resp[0]=0xF0`); `CD_PLAY` com `param_type` 1/2/7; `CD_SEEK` |
| `nulldc/nullDC/dc/gdrom/gdromv3.h` | `enum gd_states` (`gds_waitcmd`, `gds_procata`, `gds_waitpacket`, `gds_procpacket`, `gds_pio_send_data`, `gds_pio_get_data`, `gds_pio_end`, `gds_procpacketdone`, `gds_readsector_pio`, `gds_readsector_dma`, `gds_process_set_mode`); `struct SpiCommandInfo { u8 CommandCode; u8 CommandData[12]; u16 CommandData_16[6]; }`; bitfields `GD_StatusT`, `GD_ErrRegT`, `GD_FeaturesT` |
| `nulldc/nullDC/dc/gdrom/gdrom_response.cpp` | Geração de respostas de alto nível |
| `nulldc/nullDC/dc/gdrom/gdrom_if.h` | Enumeração `SPI_*` e interface de alto nível |
| `nulldc/nullDC/dc/gdrom/gdromv3.cpp` → **flycast** `core/reios/gdrom_hle.cpp` | Camada HLE equivalente, mais fácil de clonar |
| flycast `core/imgread/common.h` | `enum SectorFormat { SECFMT_2352, SECFMT_2048_MODE1, SECFMT_2048_MODE2_FORM1, SECFMT_2336_MODE2, SECFMT_2448_MODE2 }`, `struct TOC`, `GetBaseFAD()` (GD ⇒ 45150) |

⚠️ O **NullDC está abandonado**; o Flycast é o fork vivo. Preferir o Flycast.
O `lib/nulldc/ImgReader/` que o Dreamdrive usa veio daqui.

## 3. MAME

| Ficheiro | Conteúdo |
|---|---|
| **`src/devices/bus/ata/gdrom.cpp`** | ⭐⭐ `gdrom_device::device_start()`, `device_reset()` (restaura `GDROM_Cmd11_Reply[32]` = `"    Rev 6.42990316"`, com `0xB4` no byte 5 = 180 s standby), `do_command()` (switch de opcodes), `ReadData()` / `WriteData()`, `handle_drive_ready()`. Contém `GDROM_Cmd71_Reply[]` (~200 bytes, o blob de segurança) |
| `src/devices/bus/ata/gdrom.h` | Estados `GDROM_*_STATE`, `CDROM_STS_GOOD` / `CDROM_STS_CHECK_CONDITION`, constantes do task file |
| `src/mame/sega/dccons.cpp` | `dc_cons_state::gdrom_config()`, `ATA_INTERFACE`, `dc_map()` com `map(0x005f7000, 0x005f70ff)`, e as expressões de clock da AICA |
| `src/devices/bus/ata/ata_interface.cpp`, `atapi_device.cpp` | O modelo de device genérico que o gdrom.cpp estende |

⚠️ **Correção de caminho:** o briefing original dizia
`src/devices/machine/gdrom.cpp`. O correcto é **`src/devices/bus/ata/gdrom.cpp`**.

Rejeições explícitas do MAME (bons testes de aceitação para o nosso ODE):
- MSF em `CD_READ`
- `read_type != 0 && != 2`
- `data_select != 2` → `emu_fatalerror("GDROM: Unhandled data_select %d")`

## 4. Linux — referência de produção para o handshake

`drivers/cdrom/gdrom.c` — ⭐⭐ o melhor exemplo de **sequência canónica** de comando:

```c
/* gdrom_spicommand() */
writeb(0x08, GDROM_ALTSTATUS_REG);        /* habilita INTRQ */
writeb(buflen & 0xff,  GDROM_BCL_REG);
writeb(buflen >> 8,    GDROM_BCH_REG);
writeb(0, GDROM_INTSEC_REG); writeb(0, GDROM_SECNUM_REG); writeb(0, GDROM_ERROR_REG);
wait_bsy_zero();
writeb(0xA0, GDROM_STATUSCOMMAND_REG);
wait_for((readb(GDROM_ALTSTATUS_REG) & 0x88) == 0x08);
outsw(GDROM_DATA_REG, cmd, 6);            /* os 6 words do packet */
```

Outras funções essenciais:

| Função | O que faz |
|---|---|
| `gdrom_getsense()` | REQ_ERROR `0x13`, buflen 10, verifica `sense[1] & 40` (drive not ready) |
| `gdrom_readtoc_cmd()` | `0x14`, buflen = `sizeof(struct gdromtoc)` |
| **`gdrom_readdisk_dma()`** | ⭐ o caminho de leitura por DMA: programa `DMA_STARTADDR/LENGTH/DIRECTION/ENABLE`, `ERROR=1`, `SECNUM=BCL=BCH=DSEL=INTSEC=0`, `COMMAND=0xA0`, `outsw(DATA, cmd, 6)`, espera `DMA_STATUS`, escreve `DMA_STATUS=1` para iniciar |
| `gdrom_init_dma_mode()` | `ERROR=0x13`, `INTSEC=0x22`, `COMMAND=0xEF`; depois `DMA_ACCESS_CTRL=0x8843407F` (faixa `0x0C000000`–`0x0FFFFFFF`) e `DMA_WAIT=9` |
| `gdrom_preparedisk_cmd()` | o `0x70` (`cmd[0]=0x70, cmd[2]=0x1f`) |

## 5. libKOS — detalhe do registo Drive Select

`kernel/arch/dreamcast/include/dc/g1ata.h`:

```c
#define G1_ATA_BUS_PROTECTION 0x005F74E4
#define G1_ATA_MASTER 0x00
#define G1_ATA_SLAVE 0xB0
#define G1_ATA_LBA_MODE 0x40
```

Documenta: *"the GD-ROM really does not like the reserved bits being set in the device
select register"* → **enviar `0x00` no Drive Select, nunca `0x90`.**

https://kos-docs.dreamcast.wiki/g1ata_8h_source.html

## 6. Especificação do formato do disco

Sega, *Disc Format Standard Specifications Ver.1.0*
https://antime.kapsi.fi/sega/files/ST-040-R4-051795.pdf

Formato do disco GD-ROM (channels, CN/SM, Mode 2) — **documento separado da spec SPI**.
Relevante para o `GET_TOC` de alta densidade e para a leitura de imagens raw 2352/2448.

## 7. Contexto arquitetural

- Copetti, *"Dreamcast Architecture: A Practical Analysis"* —
  https://www.copetti.org/writings/consoles/dreamcast/
- *Dreamcast Dev. Box System Architecture* —
  https://segaretro.org/images/7/78/DreamcastDevBoxSystemArchitecture.pdf
  (⚠️ é daqui que vem a pista do MWDMA-2, ver [06b](06b-scope-pio-vs-dma.md))

## 8. Ordem de leitura recomendada

1. **spec SPI Ver.1.30** — a norma. Ler uma vez, com calma.
2. `sega_packet_interface.h` do Dreamdrive — a mesma spec em enums C. Para consultar.
3. `rv32/source/ide.c` do iceGDROM — como a spec vira código.
4. `gdromv3.cpp` do Flycast — como o emulador completo orquestra tudo.
5. `ide_interface.v` do iceGDROM — a máquina de estados do lado do device.
6. `drivers/cdrom/gdrom.c` do Linux — como um host real conduz o device.

---

Ver também: [01-protocolo-spi-sega](01-protocolo-spi-sega.md) ·
[05-analise-dreamdrive](05-analise-dreamdrive.md) · [06-analise-zuluide](06-analise-zuluide.md)
