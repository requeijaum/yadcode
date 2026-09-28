# 03 — Timing do barramento IDE/G1

Todos os números desta página foram **verificados contra a fonte primária** (ATA-3,
X3T13/2008D Rev 7b = ANSI X3.298-1997, Tabela 22 p.127 e Tabela 23 p.129).
Fonte: https://www.cs.jhu.edu/~huang/cs318/fall20/project/specs/ata-3-std.pdf

---

## 1. O que a Sega diz (e não diz)

✅ A spec SPI Ver.1.30 **não contém nenhuma tabela de setup/hold**. Sem `t1`, `t2`, `t3`,
`t8`, `t9`, `tA`, `trd`. Sem secção "Electrical Characteristics" quantificada. A §3.2
descreve sinais apenas por semântica (*"At the falling edge of this signal, the data …
become valid. At the rising edge, the host latches on to the data."*), nunca por tempo.

Os **únicos** parâmetros temporais explícitos em todo o documento são:

| Valor | Local | Contexto |
|---|---|---|
| **400 ns** | §3.4, §5, §7.1/7.2/7.3 | ✅ *"Bit 7 (BSY) becomes valid 400 ns after a command is received"* / *"the device sets the BSY bit within 400 ns"* |
| **13.3 ms** | §8.2, CD_SCD | ✅ *"the subcode information is updated approximately every 13.3 ms"* — taxa de refresh de dados, não timing de bus |
| **33.8688 MHz** | §3.2, MCK | ver [04-clock-aica.md](04-clock-aica.md) |
| 1/75 s | §2.1, §6.1.1 | frame = 1/75 s, FAD=150 — estrutural, não temporização de bus |

⚠️ **Isto é um problema de sourcing, não um detalhe.** Não existe um timing oficial Sega
para o G1. Qualquer asserted sobre "os pulsos duram 60-300 ns" em material de terceiros é
inferência. A aproximação correta é usar o **PIO mode 3 como envelope legal**, e verificar
que o host real nunca excede. Ver §3.

---

## 2. O número de ouro: 10 MB/s = 2880 ns / 32 B

Sega, *Dreamcast Hardware Specification Outline*, §3.8 "External interface specification" →
"G1 bus", p.24. Citação literal:

> "The G1 bus is connected to the GD-ROM drive and the ROM/FLASH (space 2MB and bus width
> 8bit). Access to the GD-ROM uses an interface similar to EIDE (similar to ATA) but part of
> the bus signal line has multiplexed signal lines that are used for ROM/FLASH access.
> Additionally part of the address line to ROM/FLASH has multiplexed MODE signals which are
> incorporated in the system information. **The bus operation is not synchronised and at the
> time of GD-ROM access the data bus width is 16 bit** although at the time of ROM/FLASH
> access, it is 8 bit. **The real transfer speed at the time of GD-ROM access is 10MB/s
> (2880ns/32B).**"

🔶 Derivação para o PIO:

```
2880 ns / 32 bytes  =  2880 ns / 16 words  =  180 ns por palavra de 16 bits
                     =  5.556 M transferências/s
                     =  10.0 - 11.1 MB/s de payload
```

**Isto coloca o GD-ROM exactamente na classe PIO-3** (t0 = 180 ns), que é a classe de
tempo de ciclo do ATA-3. Não é PIO-2 (240 ns) nem PIO-4 (120 ns).

### 2.1 Reconciliação dos quatro números de velocidade da Sega

A Sega documenta **quatro** gamas de velocidade e **nunca as reconcilia em texto**:

| Valor | Fonte | O que significa |
|---|---|---|
| 600 KB/s (4x) – 1800 KB/s (12x) | HW Outline §6.1 | velocidade do **disco** (4x–12x) |
| **10 MB/s** (2880 ns/32 B) | HW Outline §3.8 | *"real transfer speed"* do **access path PIO** |
| **11.1 MB/s** | HW Outline §6.1 | *"From the buffer approx. 11.1 MB/s (PIO Mode3)"* |
| **13.3 MB/s** | HW Outline §6.1 | *"Approx. 13.3 MB/s (Multi word DMA Mode2)"* |
| **16.6 MB/s** | SPI Spec §1.1 | *"max."*, sem contexto nenhum |

⚠️ O **16.6 MB/s da spec SPI é uma contradição não resolvida**. A frase aparece uma única
vez, no §1.1, e o documento nunca quantifica os modos de transferência — apenas os
enumera no Sector Count Register. **Não inventes uma explicação para ele.** Cita-o como
"número máximo declarado pela spec, sem definição de contexto".

🔶 Leitura defensável: 10 MB/s = access path PIO; 11.1 = PIO-3 a partir do buffer de 128 KB;
13.3 = o que o host faz em MWDMA-2; 16.6 = pico teórico de marketing, não atingido.

---

## 3. Tabela 22 do ATA-3 — "PIO data transfer to/from device"

Cabeçalho literal: `Mode 0ns | Mode 1ns | Mode 2ns | Mode 3ns | Mode 4ns`

| Parâmetro | Mode 0 | Mode 1 | Mode 2 | **Mode 3** | Mode 4 |
|---|---|---|---|---|---|
| t0 — Cycle time (min) | 600 | 383 | 240 | **180** | 120 |
| t1 — Address valid to DIOR-/DIOW- setup (min) | 70 | 50 | 30 | **30** | 25 |
| **t2 — DIOR-/DIOW- 16-bit (min)** | 165 | 125 | 100 | **80** | 70 |
| t2i — DIOR-/DIOW- recovery (min) | – | – | – | **70** | 25 |
| t3 — DIOW- data setup (min) | 60 | 45 | 30 | **30** | 20 |
| t4 — DIOW- data hold (min) | 30 | 20 | 15 | **10** | 10 |
| t5 — DIOR- data setup (min) | 50 | 35 | 20 | **20** | 20 |
| t6 — DIOR- data hold (min) | 5 | 5 | 5 | **5** | 5 |
| t6Z — DIOR- data tristate (**max**) | 30 | 30 | 30 | 30 | 30 |
| t9 — DIOR-/DIOW- to address valid hold (min) | 20 | 15 | 10 | **10** | 10 |
| tRd — Read Data Valid to IORDY active (min) | 0 | 0 | 0 | 0 | 0 |
| tA — IORDY setup time | 35 | 35 | 35 | **35** | 35 |
| tB — IORDY pulse width (**max**) | 1250 | 1250 | 1250 | **1250** | 1250 |

### 3.1 Notas da spec que mudam o design

**Nota 1 — ciclo composto.** t0 é o tempo total mínimo, t2 o tempo ativo mínimo, t2i o
recovery. *"All three requirements must be satisfied simultaneously"* e t0 > t2 + t2i
(modo 3: 80+70 = 150 < 180). **O host tem de alongar t2 e/ou t2i; o device tem de suportar
qualquer implementação legal do host.** Isto significa que um PIO que só implementa t2
fixo **viola a spec**.

**Nota 2 — t6Z.** Tempo desde a borda de negação de DIOR- até o bus deixar de ser dirigido.
Isto é um **máximo** (30 ns) e é o que determina quando se pode virar o bus para output.

**Nota 3 — IORDY é obrigatório em PIO 3 e 4.** *"If the device is not driving IORDY negated
at tA, t5 shall be met and tRd is not applicable. If it is, tRd shall be met and t5 is not
applicable."* → **t5 e tRD são mutuamente exclusivos**, consoante se use IORDY ou não.
Para PIO 3+, o t0 mínimo vem da **word 68 do IDENTIFY DEVICE**, não da tabela.

### 3.2 Armadilhas de leitura

- ⚠️ **`t2i` está em branco ("–") para os modos 0, 1, 2** no ATA-3. Não é zero. Se
  implementares modo 2 com IORDY, a spec não te dá t2i — só t0=240, t1=30, t2=100.
- ⚠️ **Não confundas t2 de 16-bit com t2 de registo de 8-bit.** A Tabela 21 (registos,
  p.125) dá t2(8-bit) = **290/290/290/80/70**; a Tabela 22 (dados) dá **165/125/100/80/70**.
  Divergem brutalmente nos modos 0/1/2. Para o registo Data do G1 usa-se sempre a de
  16-bit.
- ❌ **`t2r` não existe** no ATA-3 nem no ATA/ATAPI-6. Não inventes um "t2r".
- ❌ **`t7` e `t8` não existem** nas tabelas do ATA. São timings de `IOCS16-` e só aparecem
  em tabelas True IDE / PCMCIA-CF. **O briefing original citava `t8` como se fosse um
  parâmetro aplicável — não é.**

---

## 4. Tabela 23 do ATA-3 — "Multiword DMA data transfer"

| Parâmetro | Mode 0 | Mode 1 | **Mode 2** |
|---|---|---|---|
| t0 — Cycle time (min) | 480 | 150 | 120 |
| tC — DMACK to DMARQ delay | *(rótulo presente, sem valores numéricos)* | | |
| **tD — DIOR-/DIOW- (min)** | 215 | 80 | **70** |
| tE — DIOR- data access (max) | 150 | 60 | ⚠️ 50 (ver nota) |
| tF — DIOR- data hold (min) | 5 | 5 | 5 |
| tG — DIOR-/DIOW- data setup (min) | 100 | 30 | 20 |
| tH — DIOW- data hold (min) | 20 | 15 | 10 |
| tI — DMACK to DIOR-/DIOW- setup (min) | 0 | 0 | 0 |
| tJ — DIOR-/DIOW- to DMACK hold (min) | 20 | 5 | 5 |
| tKr — DIOR- negated pulse width (min) | 50 | 50 | 25 |
| tKw — DIOW- negated pulse width (min) | 215 | 50 | 25 |
| tLr — DIOR- to DMARQ delay (max) | 120 | 40 | 35 |
| tLw — DIOW- to DMARQ delay (max) | 40 | 40 | 35 |
| tZ — DMACK- to tristate (max) | 20 | 25 | 25 |

⚠️ **`tE` em modo 2**: a camada de texto do PDF rendeu só 2 dos 3 valores em duas extrações
independentes. A Tabela 68 do ATA/ATAPI-6 Rev 3b (descendência directa) dá **50 ns**. Não
confirmado visualmente no ATA-3 — abrir a p.129 antes de citar.

⚠️ `tC` no ATA-3 é o rótulo *"DMACK to DMARQ delay"* sem valores. **No ATA/ATAPI-6 `tC`
significa outra coisa** (*"IORDY assertion to release (max) = 5 ns"*). Não citar `tC` sem
dizer de que revisão vem.

❌ A Tabela 23 do ATA-3 **não tem** tM/tN (CS valid/hold). Aparecem no ATA/ATAPI-6.

---

## 5. Cross-check com o Linux

`struct ata_timing` (include/linux/libata.h), ordem real dos campos:

```c
struct ata_timing {
	unsigned short mode;        /* ATA mode                */
	unsigned short setup;       /* t1                      */
	unsigned short act8b;       /* t2 for 8-bit I/O        */
	unsigned short rec8b;       /* t2i for 8-bit I/O       */
	unsigned short cyc8b;       /* t0 for 8-bit I/O        */
	unsigned short active;      /* t2 or tD                */
	unsigned short recover;     /* t2i or tK               */
	unsigned short dmack_hold;  /* tj                      */
	unsigned short cycle;       /* t0                      */
	unsigned short udma;        /* t2CYCTYP/2              */
};
```

`ata_timing[]` (drivers/ata/libata-pata-timings.c):

```c
	{ XFER_PIO_0,     70, 290, 240, 600, 165, 150, 0,  600,   0 },
	{ XFER_PIO_1,     50, 290,  93, 383, 125, 100, 0,  383,   0 },
	{ XFER_PIO_2,     30, 290,  40, 330, 100,  90, 0,  240,   0 },
	{ XFER_PIO_3,     30,  80,  70, 180,  80,  70, 0,  180,   0 },
	{ XFER_PIO_4,     25,  70,  25, 120,  70,  25, 0,  120,   0 },
```

⚠️ **O Linux mapeia explicitamente os nomes da spec** (`t2`→`active`, `t0`→`cycle`,
`tD`→`active`). `active` bate exactamente com t2(16-bit) em **todos** os 5 modos, e `cycle`
bate exactamente com t0 em todos. Zero divergência.

A única discrepância é `cyc8b` de PIO_2 (330 ns vs 240 ns da Tabela 21 do ATA-3), e a causa
**não** é uma convenção diferente: a tabela foi retirada do **ATA/ATAPI-6 rev 0a** (diz o
comentário no topo do ficheiro), onde a Tabela 66 dá t0 modo 2 = 330. Afeta apenas acessos
de registo de 8-bit; ciclos de dados de 16-bit não são afetados.

⚠️ **O Linux não tem campos para t3, t4, t5, t6, t6Z, t9, tA nem tB.** Se o PIO for um
programador de strobes próprio, **os timings que importam e que o Linux não te dá são
exactamente esses** — e t5/tRd são mutuamente exclusivos.

---

## 6. O que isto implica para um PIO no RP2350

| Requisito | Valor (PIO-3) | Análise a 125 MHz (8 ns/ciclo) |
|---|---|---|
| Setup dos dados do ODE após a borda de **subida** de DIOR- | `t5` ≥ 20 ns | 2–3 ciclos de atraso (16–24 ns) ✅ confortável |
| Hold dos dados após a borda de **subida** de DIOR- | `t6` ≥ 5 ns | manter o `out` asserted ≥1 ciclo pós-subida ✅ |
| Release do bus (tri-state) | `t6Z` ≤ 30 ns | ≤3 ciclos antes de virar o bus para output ✅ |
| Setup dos dados do host em DIOW- | `t3` ≥ 30 ns | amostrar o `in` na **subida** de DIOW-; 30 ns ≈ 4 ciclos ✅ |
| Hold em DIOW- | `t4` ≥ 10 ns | relaxável no ciclo seguinte ✅ |
| Tempo de ciclo | `t0` = 180 ns | ≈ **22 ciclos** a 125 MHz, ou 44 a 250 MHz ✅ folgado |
| Extensão por IORDY | `tA` = 35 ns, `tB` max 1250 ns | amostrar IORDY 5+ ciclos após o strobe; `tB` é *pulse width*, não timeout |
| `t1` address→strobe | 30 ns | o host cumpre; o ODE não tem de fazer nada |
| `t9` strobe→address hold | 10 ns | idem |

**Margem real:** 22 ciclos por word, dos quais ~4 a 5 são setup/hold obrigatórios. Há
folga para absorver latência de FIFO, jitter e o custo de um `push` para a CPU.

### 6.1 `tB` = 1250 ns é uma restrição, não uma folga

`tB` é o **IORDY pulse width máximo**. 🔶 Se o host estiver a segurar IORDY negado durante
1250 ns a espera de dados, um ODE lento fica **em violação de spec**. Isto justifica
buffering generoso: o ODE tem de ter dados prontos **antes** de IORDY ser amostrado, não
depois. Ver a estratégia de pré-buffer em [08-arquitetura-proposta.md](08-arquitetura-proposta.md).

### 6.2 Arquitetura de PIO recomendada (2 state machines)

- **SM "strobe"** — detecta bordas de descida/subida em `DIOR-` e `DIOW-`; gera os atrasos de
  `t5`/`t3`; comanda o outro SM.
- **SM "data"** — `out` de 16 pinos (DD0–DD15) para escritas do host; `in` de 16 pinos
  amostrado nas bordas de subida.
- Cross-coupling por `pin` IRQ ou `exec` pull.

⚠️ **Limitação do RP2350: o PIO só vê GPIO 0–31** num bloco. Ver
[10-viabilidade-pinos-e-pcb.md](10-viabilidade-pinos-e-pcb.md) — obriga a mapear o barramento
todo dentro de GPIO 0–31 ou a usar `pio1`/`pio2` com IOBASE deslocado.

🔶 **Estratégia de IORDY**: o host é lento **de propósito** (os registos `G1CRC`/`G1CWC`
do HOLLY programam o wait). O ODE não precisa de ser rápido, precisa de estar **dentro do
pior caso**. IORDY é a **única** fonte de multi-ciclo; implementar espera por IORDY no PIO
(`wait 0 pin` + atraso) é pseudo-DMA.

---

## 7. Referência de implementação

O **iceGDROM** implementa PIO e DMA de forma intercambiável via `IDE_IOCONTROL`:
`0x01` PIO in, `0x02` PIO out, `0x03` PIO bidir, `0x04` DMA out, `0x80` preload. E usa
`IDE_FEATURES & 1` para decidir PIO vs DMA.

- `rv32/source/ide.c` — `cmd_irq()`, `data_irq()`, `packet_data_last()`, `packet_data_dma()`
- `fpga/source/ide/ide_interface.v` — a máquina de estados do lado G1
- `test/source/benchmark.c`, `dmatest.c`, `cddatest.c` — testes de throughput, Excellent
  referência para validar o RP2350

O **Dreamdrive** reimplementa isto em `Dreamcast/sw/rp2350/ide_handling.pio`. Ver
[05-analise-dreamdrive.md](05-analise-dreamdrive.md).

---

Ver também: [01-protocolo-spi-sega](01-protocolo-spi-sega.md) ·
[04-clock-aica](04-clock-aica.md) · [06b-scope-pio-vs-dma](06b-scope-pio-vs-dma.md) ·
[08-arquitetura-proposta](08-arquitetura-proposta.md)
