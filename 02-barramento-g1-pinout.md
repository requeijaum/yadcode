# 02 — Barramento G1: conector e pinout

## 1. Correção crítica: o G1 tem 50 pinos, não 40

O conector é um **Molex 52602-0579, 2×25 = 50 posições** (A1-A25, B1-B25), com 2 furos de
polarição. Confirmado pelo footprint no `riser.kicad_pcb` do iceGDROM:

```
(module Molex_52602_0579:Molex_52602_0579 …
  pads A1..A25 e B1..B25 (smd rect) + 2 np_thru_hole de Polarização)
```

Fonte: https://github.com/zeldin/iceGDROM/blob/master/pcb/riser/riser.kicad_pcb

Os sinais são o **superconjunto de um conector IDE de 40 pinos** (33 sinais + 8 de
alimentação + 8 GND + 1 NC), **mais 5 pinos de CD-DA**. Por isso é fácil confundir — a
tabela de 40 pinos que circula é uma reconstrução a partir do IDE, não o G1 real.

> ❌ O número de peça **Molex 52602-4081** não foi encontrado em nenhuma fonte. O número em
> uso é **52602-0579**. A série 52602 é "board-to-board / ribbon"; variantes comuns incluem
> 2×20 = 52602-0571.

## 2. Tabela dos sinais do G1 — VERIFICADA contra o esquema

Método de verificação: extraí as 50 posições de `Molex_52602_0579.lib` (A1-A25 a
`x=-400`, B1-B25 a `x=+400`, `y` de +1200 a -1200 em passos de 100) e cruzei com os `Text
Label` do `riser.sch`, aplicando a transformação `F 2 … H 2950 3300 60 0001` (espelho em Y).
Resultado: coluna A em `x=2550`, coluna B em `x=3350`, pino *n* em `y = 2100 + 100·(n-1)`.
**Os 33 sinais batem todos com a tabela abaixo.**

Os nomes net no esquema do riser: `G_RST` (RESET), `RDn` (DIOR-), `WRn` (DIOW-), `CS0n`,
`CS1n`, `A0`-`A2` (DA), `D0`-`D15` (DD), `IORDY`, `INTRQ`, `DMARQ`, `DMACKn`, e para CD-DA:
`LRCK`, `SCK` (BCLK), `SDAT` (SDTATA), `EMPH`, `CD_CLK` (MCK).

| Pino G1 | Sinal (§3.2) | Nome no riser | Net no esquema | Verificado |
|---|---|---|---|---|
| **A2** | `RESET` | G_RST | y=2200 | riser.sch |
| A4 / B4 | `DD7` / `DD6` | D7 / D6 | y=2400 | riser.sch |
| A5 / B5 | `DD8` / `DD9` | D8 / D9 | y=2500 | riser.sch |
| A6 / B6 | `DD5` / `DD4` | D5 / D4 | y=2600 | riser.sch |
| A7 / B7 | `DD10` / `DD11` | D10 / D11 | y=2700 | riser.sch |
| A9 / B9 | `DD3` / `DD2` | D3 / D2 | y=2900 | riser.sch |
| A10 / B10 | `DD12` / `DD13` | D12 / D13 | y=3000 | riser.sch |
| A11 / B11 | `DD1` / `DD0` | D1 / D0 | y=3100 | riser.sch |
| A12 / B12 | `DD14` / `DD15` | D14 / D15 | y=3200 | riser.sch |
| **A14** | `DMARQ-` | DMARQ | y=3400 | riser.sch |
| **A15** | `DIOR-` = **/RD** | **RDn** | y=3500 | riser.sch |
| A16 | `DMACK-` | DMACKn | y=3600 | riser.sch |
| **A17** | `EMPH` | EMPH | y=3700 | riser.sch |
| **A19** | `DA0` | A0 | y=3900 | riser.sch |
| **A20** | `CS0-` | CS0n | y=4000 | riser.sch |
| **A22** | `BCLK` | SCK | y=4200 | riser.sch |
| **A23** | `SDTATA` | SDAT | y=4300 | riser.sch |
| **B14** | `DIOW-` = **/WR** | **WRn** | y=3400 | riser.sch |
| **B15** | `IORDY` | IORDY | y=3500 | riser.sch |
| **B16** | `INTRQ` | INTRQ | y=3600 | riser.sch |
| **B17** | `DA1` | A1 | y=3700 | riser.sch |
| **B19** | `DA2` | A2 | y=3900 | riser.sch |
| **B20** | `CS1-` | CS1n | y=4000 | riser.sch |
| **B22** | `LRCK` | LRCK | y=4200 | riser.sch |
| **B23** | **`MCK` 33.8688 MHz** | **CD_CLK** | y=4300 | riser.sch |

⚠️ **Atenção ao `y`:** a coluna A e a coluna B têm sinais **no mesmo y mas diferentes**. Por
exemplo, `A7 = DD10` e `B7 = DD11`. Uma PCB que assuma "linha 7 do IDE" vai estar errada.

### 2.1 Alimentação e massa — fonte ÚNICA, não verificada no esquema

O `riser.sch` **deixa os pinos de alimentação sem ligar** (o riser só extrai sinais). A
atribuição abaixo vem **exclusivamente** do neperos, e é 🔶 não verificada:

| Pino | Sinal | Fonte |
|---|---|---|
| A1 / B1 | **+3V3 (VLOGIC)** | neperos |
| A3 / B3 | **+5V** | neperos |
| A25 / B25 | **+12V (motor do disco)** | neperos |
| A24 / B24 | **P.GND (power ground)** | neperos |
| A8, B8, A13, B13, A18, B18, A21, B21 | GND | neperos |

⚠️ **A25/B25 = 12 V para o motor.** Curto-circuitar aqui destrói a placa. **Confirmar
fisicamente com multímetro antes de ligar qualquer coisa**, porque esta atribuição tem uma
única fonte e nenhum esquema de Dreamcast a confirma.

### 2.2 Pinos sem ligação

O neperos lista explicitamente como **NC** no G1:

- `B2` — posição de chave
- **`CSEL`** — ausente no esquema do riser, sem uso conhecido
- **`/IOCS16`** — NC (consistente com §2.2 de [01](01-protocolo-spi-sega.md))
- **`/PDIAG`** — NC
- `/DASP` — presente na mother board, ligado ao **LED de atividade** (`/ACTIVE LED`), não é
  um sinal de drive a usar

⚠️ `/IOCS16`, `/PDIAG` e `/DASP` não aparecem no G1 mas o briefing original os apresentava
implicitamente como parte do pinout. São pins IDE genéricos que o Dreamcast não expõe.

### 2.3 Nomes de sinais na spec (§3.2) — verbatim

`CS0-` (seleciona command block; *"known as CS1FX- in the industry"*) · `CS1-` (seleciona
control block; *"known as CS3FX-"*) · `DA2/DA1/DA0` · `DASP-` · `DD0-DD15` · `DIOR-` ·
`DIOW-` · `DMACK-` · `DMARQ-` · `INTRQ` · `IOCS16-` · `IORDY` · `RESET` · `MCK` · `BCLK` ·
`LRCK` · `SDTATA` · `EMPH`.

### 2.4 Nomes equivalentes no lado do HOLLY 🔶

`G1D[7:0]` · `G1RAD[15:8]` · `G1RAL[10:8]` · `G1MRA[18:11]` · `G1RAH[20:19]` ·
`G1CSf[1:0]N` · `G1RDN` · `G1WRN` · `G1IORDY` · `G1DREQ` · `G1DACKN` · `G1INTRQ`.

Fontes: `Dreamcast_Hardware_Specification_Outline.pdf`
(https://segaretro.org/images/8/8b/Dreamcast_Hardware_Specification_Outline.pdf) e o
dicionário em `https://git.idk.st/bilbo/dreamcast/`.

### 2.5 Nível elétrico

🔶 A interface é **3.3 V LVTTL**. Consoles **VA0** podem usar 5 V no G1 — é por isso que o
GDEMU e o MODE têm circuitos de level-shifting.

- https://consolemods.org/wiki/Dreamcast:GDEMU
- https://shop.terraonion.com/shop/product/terraonion-mode-dreamcast-saturn-ode/view

**Isto afeta diretamente o RP2350**, que é 3.3 V. Ver
[10-viabilidade-pinos-e-pcb.md](10-viabilidade-pinos-e-pcb.md).

---

## 3. Registos de timing do G1 no HOLLY 🔶

O HOLLY tem 4 registos de timing de acesso à G1 no bloco de controlo G1 (`0x005F74xx`, 256 B),
documentados por engenharia inversa:

| Reg | Offset | Significado |
|---|---|---|
| `G1RRC` | `+0x80` | System ROM read |
| `G1RWC` | `+0x84` | System ROM write |
| `G1FRC` | `+0x88` | Flash ROM read |
| `G1FWC` | `+0x8C` | Flash ROM write |
| **`G1CRC`** | **`+0x90`** | **GD PIO read access** |
| **`G1CWC`** | **`+0x94`** | **GD PIO write access** |
| `G1GDRC` | `+0xA0` | GD **DMA** read |
| `G1GDWC` | `+0xA4` | GD **DMA** write |
| `G1SYSM` | `+0xB0` | System mode |
| `G1CRDYC` | `+0xB4` | G1IORDY signal control |
| `GDAPRO` | `+0xB8` | GD-DMA address range |

Fonte: `https://git.idk.st/bilbo/dreamcast/src/commit/8ce3f8bdc9114910c4b90aa02224cd84626e5670/systembus.hpp`

**Implicação:** o ODE não precisa de ser rápido, precisa de estar **dentro do pior caso** que
o host pratica. Ver [03-timing-ide.md](03-timing-ide.md).

---

## 4. Referências de pinout

| Fonte | URL |
|---|---|
| iceGDROM riser (KiCad, esquema + footprint) | https://github.com/zeldin/iceGDROM/tree/master/pcb/riser |
| neperos G1-ATA (IDE) adapter | https://www.neperos.com/article/p1xiwx4fc77323f2 |
| DC-SWAT G1-ATA | https://www.dc-swat.ru |
| Sega Retro — documentação oficial DC | https://segaretro.org/Dreamcast_official_documentation |
| Dreamcast Hardware Specification Outline | https://segaretro.org/images/8/8b/Dreamcast_Hardware_Specification_Outline.pdf |
| libKOS `g1ata.h` | https://kos-docs.dreamcast.wiki/g1ata_8h_source.html |

Ver também: [01-protocolo-spi-sega](01-protocolo-spi-sega.md) ·
[10-viabilidade-pinos-e-pcb](10-viabilidade-pinos-e-pcb.md)
