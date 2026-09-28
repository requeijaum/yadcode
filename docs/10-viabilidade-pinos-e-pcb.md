# 10 — Viabilidade de pinos, RP2350B e PCB

## 1. Resposta curta

**Um RP2350A (Pico 2) não chega. É obrigatório RP2350B (QFN-80) em PCB própria.**

## 2. Contagem de sinais

Contagem feita a partir do `rp2350_pins.h` do Dreamdrive, que é o único pinout real de um
ODE Dreamcast em RP2350 que existe publicamente:

| Bloco | Pinos | GPIO |
|---|---|---|
| Barramento G1: `DD0-15` (16), `DA0-2` (3), `/CS0`, `/CS1` (2), `/RD`, `/WR` (2), `IORDY`, `INTRQ`, `DMARQ`, `DMACK` (4), `GD_RST` (1) | **28** | 0–26 |
| CD-DA: `CD_LRCK`, `CD_BCK`, `CD_SDAT`, `CDDA_CLK`, `CD_EMPH` (5) + `DOPEN` | **6** | 27–32 |
| SD: `SD_CLK`, `SD_CMD`, `SD_D0-3` (5) | **5** | 34–38 |
| — | **39** | `PIN_SD_D3 = 39` |
| UART debug (RX/TX) | +2 | 40–41 |

**Total ≈ 40 pinos. (O RP2350B tem 48 GPIO: GP0–GP47, com GP40–GP47 sendo ADCs.)**

### 2.1 GPIO disponíveis por variante

| Produto | Package | GPIO | ADC |
|---|---|---|---|
| **RP2350A** | QFN-60, 7×7 mm | **30** | 4 (GP26–29) |
| **RP2350B** | QFN-80, 10×10 mm | **48** | 8 (GP40–47) |
| RP2354A | QFN-60 | 30 | 4 |
| RP2354B | QFN-80 | 48 | 8 |

Datasheet: *"Analogue input is available on GPIOs 26 through 29 in the QFN-60 package
(RP2350A) … and on GPIOs 40 through 47 in the QFN-80 package (RP2350B)."*
https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf

**40 > 30 → o RP2350A é insuficiente.** E a board "Pico 2" standard expõe apenas GP0–GP29
nos 40 pinos do header.

## 3. A inconsistência do Dreamdrive

`Dreamcast/sw/rp2350/CMakeLists.txt` diz:
```cmake
set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")
```

E o board file do pico-sdk para essa board diz:
```c
// --- RP2350 VARIANT ---
#define PICO_RP2350A 0
// …
#define PIMORONI_PGA2350_PSRAM_CS_PIN 47
```

⚠️ **GPIO 47 não existe num RP2350A.** O próprio board file do SDK é internamente
inconsistente. E `rp2350_pins.h` pede GPIO 0–39, que num RP2350A simplesmente não existem.

O autor **sabe disto** e é por isso que a PCB no repo é `RP2350-QFN-80-1EP_10x10`. Do log
no Hackaday:

> "…Raspberry Pi released a new line of silicon. **The RP2350!** Their 'B' variant is of
> particular interest. **Double the gpio of the base rp2040.** So no longer do I need to
> bolt two rp2040s together. **A single rp2350B will now do the job.** I designed a new
> board around the Pimoroni PGA2350, a stamp format board."

⚠️ Nota: *"Double the gpio of the base rp2040"* é impreciso — o RP2040 (QFN-56) e o
RP2350A (QFN-60) têm **ambos 30**. O que dá a margem é o **RP2350B = 48**.

## 4. A restrição do PIO: GPIO 0–31

⚠️ **Esta é a restrição que realmente condiciona o pinout, e o Dreamdrive desenhou em volta
dela.** Cada bloco PIO vê **32 pinos GPIO contíguos** a partir do seu IOBASE. Não é
possível fazer `out pins, 16` para pinos que cruzem o limite 31→32.

Daí a razão de o `rp2350_pins.h` ser **GPIO 0..39 contíguo sem pinos pulados**: o barramento
IDE inteiro (GPIO 0–26) cai dentro de PIO0. `PIN_CD_BCK` (28) e `PIN_CD_SDAT` (29) também
cabem em PIO0.

⚠️ **Inconsistência do autor:** `setup_squarewave_generator()` opera em **`pio2`** apesar de
os pinos 28/29 estarem em PIO0. Funciona, mas é incoerente.

**Opções para a nossa PCB:**
- **Opção A** — aceitar o layout contíguo 0–39 e usar PIO0 para IDE+CD-DA, PIO1 para SDIO
  (IOBASE 32) e PIO2 para o clock.
- **Opção B** — agrupar por bloco PIO: IDE inteiro em 0–26, CD-DA noutro bloco,
  SDIO noutro. Mais flexível, mas perde a vantagem do layout contíguo.
- **Opção C** — como o ZuluIDE: `pio1` com `IOBASE 16`, usando o seu
  `rp2350_iocs16.pio` para o pino que cai do outro lado.

## 5. Nível elétrico — o problema prático

🔶 A interface é **3.3 V LVTTL**. Consoles **VA0** podem usar **5 V** no G1 — é por isso
que o GDEMU e o MODE têm circuitos de level-shifting.

O RP2350 é 3.3 V. Opções:
- **Buffer bidireccional** (74LVC245 / SN74LVC8T245) entre RP2350 e G1.
- Confirmação: `/DD`, `/DA`, `/CS`, `/RD`, `/WR` são todos **entradas** do GD-ROM
  (device) — unidireccionais, precisam só de buffer. `DD0-15` é **bidireccional** (o host
  lê o registo Data).
- `INTRQ` é saída do drive. `IORDY` é saída do drive. `DMARQ` é saída do drive,
  `DMACK` é entrada.

⚠️ **Isto é a maior fonte de custo e de risco da PCB.** Sem level shifting, um ODE só
funciona em VA1 e é inútil para quem tem VA0 — que é metade do parque instalado.

## 6. Componentes e custos

| Item | Nota | Risco |
|---|---|---|
| **RP2350B** | QFN-80, 0,4 mm pitch.difícil de soldar à mão; assembly é obrigatório | 🔴 |
| **Molex 52602-0579** | 2×25, raro. O autor do Dreamdrive tem tido dificuldade em obtê-lo | 🔴 |
| Level shifters | 2× 74LVC245 ou similar | 🟡 |
| Cartão microSD | qualquer um; SDIO nativo | 🟢 |
| Crystal 33.8688 MHz | *opcional*, boa rede de segurança para depurar ([04 §4.4](04-clock-aica.md)) | 🟢 |
| Alimentação 3.3 V | do próprio console (VLOGIC) ou regulator local | 🟡 |

⚠️ **"PCBs local" não é uma opção realista** — o G1 é um conector SMT de 0,5 mm num
cartão que não é teu. **Precisas de uma placa riser** que encaixe no slot, ou de
soldar fios directamente à motherboard (é o que a maioria dos projectos faz).

## 7. O conector

- **Molex 52602-0579**, 2×25 = 50 posições, com 2 furos de polarização.
- **B2** é a posição de chave — não conectada. Serve para posicionamento.
- ⚠️ **A25/B25 = 12 V (motor do disco)**. Source única (neperos), não confirmada. Verificar
  com multímetro antes de ligar.
- ⚠️ Alternativas: `52602-0571` (2×20 = 40 pinos) é uma variante comum da mesma série —
  mas **não serve**: o G1 precisa de 50.

## 8. Recomendações

1. **RP2350B, sem dúvida.** Não há alternativa.
2. **PCB riser** com level shifting, desenhada desde o início para QFN-80.
3. **Verificar o pinout de alimentação com multímetro** antes de desenhar a placa.
4. **Usar um stamp-format board** (estilo Pimoroni PGA2350 mas em variante B) se não
   quiser soldar QFN-80 — mas então confirmar que expõe ≥ 40 GPIO em 0–39 contíguos,
   porque o layout contíguo é o que dá o PIO simples.
5. **Tornar o level shifting configurável** (jumper) para VA0/VA1.

---

Ver também: [02-barramento-g1-pinout](02-barramento-g1-pinout.md) ·
[04-clock-aica](04-clock-aica.md) · [08-arquitetura-proposta](08-arquitetura-proposta.md) ·
[09-riscos-e-licencas](09-riscos-e-licencas.md)
