# 04 — Clock de 33.8688 MHz (MCK) para a AICA

> **Isto não é uma feature opcional. É pré-requisito de bring-up.**
> Sem MCK no pino B23, a AICA não tem referência de clock e **nenhum** ODE funciona. Se
> tentares dar bring-up sem isto, vais perder dias a depurar "o console não arranca"
> quando a causa é o clock.

---

## 1. O que a Sega confirma

### 1.1 A spec SPI §3.2, "Signal Lines" — citação literal

> "**MCK (Main clock) 33.8688 MHz clock signal for AICA use.**"

Contexto (mesma secção, sinais de áudio):
- `BCLK (Bit clock)` — *"Clock signal for fetching Digital Audio."*
- `LRCK (Left right clock)` — *"Digital Audio left/right discriminant signal."*
- `SDTATA (Serial data)` — *"Digital audio data."*
- `EMPH (Emphasis)` — *"…"*

### 1.2 Hardware Specification Outline — três passagens

§1.3 System Block → "AICA":

> "Starting from the GD ROM it picks up audio signals or **33.8688MHz** from the audio clock."

§4.2 "Chip specification":

> "The sound block and the internal CPU has a **22.5792MHz** clock supplied from the GD-ROM
> drive and just like the memory i/f, it is **67.7376MHz** from the GD-ROM drive clock."
>
> "the **33.8688 MHz** clock supplied from the GD-ROM drive is **converted to the sampling
> frequency 44.1 KHz in the internal PLL**, operational sound block clock as well as the
> memory i/f clock."

§6 GD-ROM:

> "Opposed to the Audio 1C AICA, it supplies the digital audio playback signal and the
> **33.8688MHz clock that is the audio clock source**. Additionally there is a **33.8688MHz
> x'tal built into the drive**."

✅ **Direção confirmada: device → host.** O GD-ROM é a fonte; tem o cristal a bordo.

✅ Dev.Box System Architecture §1.2:

> "**Wave memory bus** — This is an SDRAM interface bus for audio that is supported by AICA.
> **The bus clock is 67.7MHz (2 x 33.8688MHz, which is supplied from the GD-ROM to AICA).
> The bus width is 16 bits.**"

E §4.2.2.4: *"Operating frequency 67.7376MHz"*.

## 2. O SH-4 não usa este clock

⚠️ Erro fácil de cometer. O clock do SH-4 vem do PLL do sistema, com x'tal de 13.5 MHz:

- HW Outline §1.3 "CPU": *"The frequency of the clock (where the operation is based) is
  **33.3MHz output from the external PLL**."*
- HW Outline §1.3 "PLL": *"This is for creating each clock in the system and has an attached
  **13.5MHz x'tal**. The frequency of the output clock … is 33.3MHz(SH4) and 54MHz(HOLLY)."*
- Dev.Box §1.2: *"The main CPU is a Hitachi SH4, which accepts a 33.3MHz clock signal from
  the system and, by means of an internal PLL, operates at 1.8V/200Mhz internally."*

→ **33.8688 MHz é exclusivo da AICA** (sound block 22.5792 MHz, wave memory 67.7376 MHz).
A única ligação ao GD-ROM é `MACK(33.8688M)`, que sai **da AICA para o drive**, não o
inverso para o SH-4.

Confirma-se no MAME `src/mame/sega/dccons.cpp`: `SH7091(config, m_maincpu, CPU_CLOCK)` com
`#define CPU_CLOCK (200000000)`, independente de `XTAL(33'868'800)`.

## 3. A matemática (com uma ressalva importante)

```
33.8688 MHz / 768 = 44 100 Hz          → 768·Fs de 44.1 kHz
2 × 33.8688 MHz   = 67.7376 MHz        → wave memory bus
67.7376 / 3       = 22.5792 MHz        → sound block
67.7376 / 3 / 8   =  2.8224 MHz        → ARM7 (o MAME usa isto)
```

⚠️ **A divisão por 768 NÃO está em nenhum documento da Sega.** Varrei os três: só aparece
*"768"* no Dev.Box como *"768B Test area"*. A aritmética 33868800/768 = 44100 é **correta e
consistente** com o que a Sega afirma, mas é **derivação nossa**, apoiada no par
"33.8688 MHz → 44.1 kHz via PLL interno". **Cita-la como derivação própria, nunca como
derivação da Sega.**

MAME confirma as frequências:
```c
ARM7(config, m_soundcpu, ((XTAL(33'868'800)*2)/3)/8);
AICA(config, m_aica,     (XTAL(33'868'800)*2)/3);
```
⚠️ O comentário do MAME *"AICA bus clock is 2/3rds * 33.8688"* é **enganador** — o código
faz `*2` e o div 3 dá 22.5792 MHz (o clock do bloco de áudio), não 67.7. O código está
certo, o comentário não.

---

## 4. Gerar 33.8688 MHz num RP2350

### 4.1 O erro a evitar: "sysclk 270,95 MHz + clkdiv 8"

⚠️ **Não funciona.** O sysclk máximo do RP2350 **não é 400 MHz**. Os limites reais:

| Parâmetro | Valor |
|---|---|
| `PICO_PLL_VCO_MIN_FREQ_HZ` | **750 MHz** |
| `PICO_PLL_VCO_MAX_FREQ_HZ` | **1600 MHz** |
| postdiv1 (2.º divisor) | mínimo **7**, máximo 133 (5.4 fixo) |
| postdiv2 | 1–7 |
| div_int do PIO | 16 bits + **frac8** (`div_frac8`, passo de **1/256**) |
| sysclk por defeito / *spec* | **150 MHz** |

→ **sysclk máximo alcançável = 1600 / 7 ≈ 228,57 MHz.** Logo:
- `33.8688 × 8 = 270,95 MHz` → **fora de alcance** (e o "400 MHz" da literature é
  overclocking, não spec).
- `33.8688 × 12 = 406,43 MHz` → também fora, e já é overclock.
- **Não há "passo de 1 MHz" no PLL** — `fbdiv` é 16.16 fixo, `postdiv1` é 5.4 fixo. A
  granularidade não é o problema.
- **O USB não restringe**: os 48 MHz vêm do `PLL_USB`, independente do sysclk. Não há mínimo
  de sysclk imposto pelo USB.

Fonte: `hardware_pll/include/hardware/pll.h` e `hardware_pio/include/hardware/pio.h` do
pico-sdk; https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf

### 4.2 As duas soluções exatas

Com o XOSC de 12 MHz, `33.8688/12 = 2,8224 = 1764/625`. Para a PLL produzir isso
exatamente é preciso que `625` divida `refdiv·postdiv1·postdiv2` — ou seja, `refdiv`
múltiplo de 25, porque `1764 = 2²·3²·7²` não tem fatores 5.

**(a) sysclk = 33.8688 MHz directamente**
```
refdiv = 25,  fbdiv = 1764,  postdiv1 = 25,  postdiv2 = 1
VCO = 12 MHz / 25 × 1764 = 846.72 MHz      (dentro de 750–1600 ✅)
sysclk = 33.8688 MHz
```
Sem fractional, sem clkdiv, sem PIO. ✅

**(b) sysclk de trabalho + clkdiv fracionário do PIO — RECOMENDADA**
```
refdiv = 25,  fbdiv = 3087,  postdiv1 = 7,  postdiv2 = 1
VCO = 12/25 × 3087 = 1481.76 MHz           (dentro de 750–1600 ✅)
sysclk = 211.68 MHz
PIO clkdiv = 6.25  →  div_int = 6, div_frac8 = 64
211.68 / 6.25 = 33.8688 MHz  ✅ exacto
```

**Porquê (b) e não (a)?** 33.8688 MHz de sysclk é demasiado lento para o PIO do barramento
IDE — a 8 ns/ciclo só dá ~4 ciclos dentro dos 180 ns de `t0`, e a folha de cálculo de
[03 §6](03-timing-ide.md) quer ~22. Com 211.68 MHz há 42 ciclos por word. **O clock do AICA
deve sair de um PIO dedicado, não do sysclk.**

### 4.3 O programa PIO

Um square wave de 50% no PIO, sem `side_set` se preferires GPIO dedicado:

```pio
.pio_version 1
;
; Gera 33.8688 MHz a partir de sysclk=211.68 MHz com clkdiv=6.25
; (div_int=6, div_frac8=64 → 6.25)
;
.program mck_33m8688
.wrap_target
    set pins, 1
    set pins, 0
.wrap
```

Com `sm_config_set_clkdiv_int_frac8(&c, 6, 64)`. O duty cycle de 50% é exato com dois
`set` seguidos.

⚠️ **`.pio_version 1`** — o Dreamdrive **não** declara isto, e o ZuluIDE declara. Gera código
melhor no RP2350 (sem overhead de delay-chain). É uma melhoria gratuita.

### 4.4 Alternativa: cristal ativo de 33.8688 MHz

Se não quiseres mexer no PLL, um **cristal ativo de 33.8688 MHz** ligado a B23 com
output-enable resolve. É o workaround que a comunidade usa quando não há ODE (comprado em
stock ou soldado de um oscilador de QuartzLab). Custa pouco e é uma boa rede de segurança
para depurar.

## 5. Quem fornece MCK na prática

| Device | Fornece 33.8688 MHz em B23? | Confiança |
|---|---|---|
| **iceGDROM** | ✅ **SIM, explicitamente** | `fpga/source/top.v`: `localparam REFCLK_FREQ = 11289600; localparam CDCLK_FREQ = 33868800;`, PLL `clkgen` com `.clkout(CDCLK)`, saída `output CDCLK` em `top.pcf`, e no `riser.sch` a net `CD_CLK` vai de **B23** até ao FPGA. ✅ **CONFIRMADO** |
| **GDEMU** | 🔶 **Provável** (PCB com x'tal de 11.2896 MHz; a troca de 12.000 → 11.2896 é documentada) | A documentação **não diz** se exporta para B23. **Medir.** https://consolemods.org/wiki/Dreamcast:GDEMU |
| Clones GDEMU / **PicoGDR** | ❓ | Ver [12-correcoes-ao-briefing.md](12-correcoes-ao-briefing.md) — o PicoGDR **não existe** como projeto |
| **MODE** (Terraonion) | ❓ não documentado | https://consolemods.org/wiki/Saturn:MODE |
| Mods G1-ATA (HDD na G1) | ❌ **NÃO** — e é um bug conhecido | *"You will have to attach AICA clock manually to pin B23"* |

**Conclusão de projeto:** mesmo que um ODE comercial "funcione", **não presumas que gera o
clock.** Instrumentar o B23 com um scope na placa alvo antes de decidir. E gerar
33.8688 MHz é trivial no RP2350 — **deve ser feito**.

---

## 6. Relação com o CD-DA

⚠️ **MCK é só uma das cinco linhas de CD-DA.** O packet `CD_PLAY` / `CD_SEEK` /
`CD_SCAN` (§1 de [01](01-protocolo-spi-sega.md), risco **Alto**) precisa de:

| Sinal | Pino G1 | Nota |
|---|---|---|
| `MCK` 33.8688 MHz | **B23** | este documento |
| `BCLK` (bit clock) | A22 | 2.1168 MHz para 44.1 kHz × 48 ou 4.2336 MHz × 96 frames |
| `SDTATA` (serial data) | A23 | I2S-like, com pre-emphasis |
| `LRCK` (L/R clock) | B22 | discriminante canal |
| `EMPH` (emphasis) | A17 | 0 = sem ênfase, 1 = com |

O iceGDROM faz isto em `rv32/source/cdda.c` (`cdda_start`, `cdda_get_status`,
`cdda_subcode_q[12]`, `cdda_active`) e o subcode Q é gerado com **CRC-16 CCITT**
(polinómio `0x1021`) em `ide.c`.

**Isto é um projeto paralelo dentro do projeto.** Áudio CD-DA + MCK + subcode contínuo
(13.3 ms, ver [01 §7](01-protocolo-spi-sega.md)) é provavelmente a maior fatia de trabalho
depois do próprio PIO do barramento. Ver [09-riscos-e-licencas.md](09-riscos-e-licencas.md).

---

Ver também: [01-protocolo-spi-sega](01-protocolo-spi-sega.md) ·
[02-barramento-g1-pinout](02-barramento-g1-pinout.md) ·
[10-viabilidade-pinos-e-pcb](10-viabilidade-pinos-e-pcb.md)
