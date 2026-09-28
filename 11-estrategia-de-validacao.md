# 11 — Estratégia de validação (sem hardware GD)

> O objectivo desta fase é chegar a um firmware **correcto por construção**, validado contra
> o emulador de referência, antes de comprar uma única placa.

## 1. Flycast como modelo de ouro

O Flycast tem um GD-ROM HLE completo e maduro. Ele é a **referência de comportamento**:
qualquer dúvida sobre o que o host espera pode ser respondida lendo o código ou, melhor,
correndo o emulador com logging.

```bash
git clone https://github.com/flyinghead/flycast
cd flycast && git submodule update --init
```

Ficheiros a ter como referência: `core/hw/gdrom/gdromv3.cpp`, `gdromv3.h`,
`gdrom_if.h`, `gdrom_response.cpp`, `core/imgread/common.h`.

**Vantagem adicional:** o Flycast tem GDI/CUE readers completos, incluindo
alta densidade, que se podem usar como referência de parser.

## 2. Simulador de host no PC

Escrever um **host simulator** em Python/C que imite o HOLLY:

```
- gera acessos de registo (CS0/CS1/DA, RD/WR) com timings PIO-3 realistas
- consome o que o "device" responde
- executa uma máquina de estados idêntica à §7.1 da spec
- compara com o Flycast byte a byte
```

### 2.1 Golden traces

O instrumento central: **gravar e comparar traces**.

1. Correr um dump de GD-ROM no Flycast com log de cada transacção G1
   (registo, valor, direcção, timestamp).
2. Guardar como golden trace (JSON ou texto).
3. Correr o **mesmo dump** contra o nosso device, a correr no PC ou no RP2350 via UART.
4. Diff. Cada diferença é um bug.

⚠️ Isto é o que substitui ter hardware. Um device correcto é um device que produz o mesmo
trace que o Flycast para os mesmos 16 comandos.

### 2.2 Casos de teste obrigatórios

| # | Teste | Verificação |
|---|---|---|
| 1 | Power-on reset, ler cada um dos 7 registos | Task file = `Status=00, Error=01, SecCnt=01, SecNum=01, CylLo=14, CylHi=EB, DrvHd=00` |
| 2 | Command 0x00 (NOP) | BSY vai a 1 em < 400 ns, depois a 0 |
| 3 | Command 0x08 (soft reset) com BSY=1 | Aceite; task file reinicializada, **bit DRV mantido** |
| 4 | Command 0x90 (EXECDIAG) | Error = 0x01 (Normal) |
| 5 | Command 0xA1 (IDENTIFY) | Strings correctas, sempre PIO |
| 6 | Command 0xEF (SET FEATURES) | Modo de transferência aplicado |
| 7 | Command inválido (ex. 0x55) | `Error=0x04`, `Status=0x51`, INTRQ |
| 8 | Packet 0x00 TEST_UNIT | GOOD, sem CHECK |
| 9 | `0x10` REQ_STAT | 10 bytes, campo a campo, FAD correcto |
| 10 | `0x11` REQ_MODE | 32 bytes, `0xB4` no byte 5, ASCII real em [10-31] |
| 11 | `0x13` REQ_ERROR | 10 bytes, `0xF0` no byte 0 |
| 12 | `0x14` GET_TOC | 408 bytes, `Select` devolve 2 TOCs para GD |
| 13 | `0x15` REQ_SES | 6 bytes |
| 14 | `0x30` CD_READ | Sector FAD conhecido == sector da imagem |
| 15 | `0x31` CD_READ2 | length 16-bit em [6-7] |
| 16 | `0x40` GET_SCD fmt 0 | 100 bytes, CRC-16 CCITT válido |
| 17 | `0x40` GET_SCD fmt 1 | 14 bytes, Q-subcode |
| 18 | `0x70` → `0x71` | GOOD, depois blob; PAUSE se bootável |
| 19 | `0x12` SET_MODE | Único comando host→device |
| 20 | Troca de disco | TEST_UNIT reflecte `<BUSY>` |

## 3. PIO sniffer (o instrumento para o hardware)

Adaptar o `rp2350_sniffer.pio` do ZuluIDE:

```
lib/ZuluIDE_platform_RP2350/rp2350_sniffer.{h,cpp} + rp2350_sniffer.pio
```

Função: samplear os sinais G1 a 1 Nyquist e transcrever para memória, depois despejar por
SD/UART. Com um PIO `rx` sem shift é possível capturing contínuo, mas para 180 ns/word é
preciso `autopush` + `push` a cada few samples e pós-processar.

**Usos:**
- (a) responder à **questão PIO vs DMA** ([06b §4.1](06b-scope-pio-vs-dma.md)) — a mais
  importante;
- (b) validar o timing do nosso próprio PIO contra o drive real;
- (c) encontrar sequências que travam o boot.

⚠️ Com 33 sinais a 1 sample/ciclo não cabe no FIFO. Estratégia prática: fazer sniffer de
**um subconjunto** por passagem (ex.: só `Features`/`INTRQ` para a questão DMA), ou
usar um analisador lógico externo (Saleae) num subconjunto de sinais.

## 4. Ambiente de build e testes

```bash
# pico-sdk 2.1.0+
git clone -b master https://github.com/raspberrypi/pico-sdk
git clone -b master https://github.com/raspberrypi/pico-examples   # opcional

export PICO_SDK_PATH=$PWD/pico-sdk
cmake -B build -S . && cmake --build build
```

⚠️ **Tratar o build como parte do produto.** O Dreamdrive tem dependências em falta e não
compila a partir de `main` ([05 §2.1](05-analise-dreamdrive.md)). Isto é uma lição
concreta: **o repositório tem de compilar a partir de um clone limpo, sempre.**

Meta:
- `cmake --build` sem warnings com `-Wall -Wextra` (o Dreamdrive compila com `-O0` e `-Wall`
  desligado)
- Testes unitários para a camada L3 (SPI) a correr no host, sem RP2350
- `ctest` a correr em CI

## 5. Emulação do host no RP2350 (para o core1)

Como não há hardware, um **PIO de loopback** que gera os sinais de um host é a forma mais
barata de testar o nosso PIO:

```
PIO_A: gera CS0/CS1/DA/RD/WR com timings PIO-3 (delays em instrucções)
PIO_B: o nosso PIO real, a responder
→ loopback interno, sem pinos
```

Isto valida **t0, t5, t3, t6, t9** contra a spec [03 §3](03-timing-ide.md) sem qualquer
hardware. É a mesma técnica dos testes do iceGDROM
(`test/source/benchmark.c`, `dmatest.c`).

⚠️ **Limite:** só valida timings PIO-3 gerados por nós. **Não** valida se o host real é
mais rápido ou mais lento. Para isso só o sniffer (§3).

## 6. PIO sniffer golden: ciclo de vida do firmware

```
1. Simulador de host (PC)         → valida L2+L3 contra Flycast
2. Loopback PIO no RP2350         → valida L1 timings
3. Dump de imagens + testes CI    → valida L5
4. Board bring-up (MCK primeiro)  → valida L0
5. Sniffer num Dreamcast + GD real → responde PIO vs DMA, valida L1 contra o host
6. Bring-up completo             → valida L4 (CD-DA)
```

⚠️ **Os passos 1-3 não precisam de nenhuma placa.** Podem começar já.

## 7. Fixtures

Diretório de fixtures necessárias:

| Ficheiro | Origem | Nota |
|---|---|---|
| `IP.BIN` dump | Dreamdrive `gdrom_utils.h` (~4 KB) | ⚠️ ver questão legal ([09](09-riscos-e-licencas.md)) |
| `IP.BIN` dump limpo | BIOS real, dumpado | **Preferir este.** Google em "Dreamcast IP.BIN" dá material dc-program |
| Respostas canned | `gdrom.h` do Dreamdrive, `GDROM_Cmd71_Reply[]` do MAME | MIT/GPL — ver licença |
| GDI de teste | Qualquer dump, track 01 | 2352 raw |
| Imagem de alta densidade |precisa de high-density CD (p.ex. `Bangai-O`, `Skies of Arcadia`) | Testa o `Select` bit do GET_TOC |
| Golden traces | Flycast + logging | Gerar nós |

⚠️ **Preferir sempre fixtures derivadas de hardware real** (BIOS dumpado) às canned do
Dreamdrive. As canned não são verificadas, e o autor admite
*"I have no idea why....."* sobre registos específicos.

---

Ver também: [06b-scope-pio-vs-dma](06b-scope-pio-vs-dma.md) ·
[08-arquitetura-proposta](08-arquitetura-proposta.md) ·
[09-riscos-e-licencas](09-riscos-e-licencas.md)
