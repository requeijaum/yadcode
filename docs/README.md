# GD-ROM ODE para Dreamcast com RP2350 — estudo + firmware

Estudo de engenharia para implementar um Optical Drive Emulator do Dreamcast com um
Raspberry Pi RP2350B, mais o firmware correspondente — ainda **sem validação em
silício**: não há placa.

Objectivo desta fase: fixar, com fontes primárias, tudo o que é necessário para escrever
correctamente o firmware — e saber o que ainda é desconhecido.

---

## 1. Estado

| | |
|---|---|
| **Fase** | Firmware escrito e testado em simulação; por validar em hardware. |
| **Hardware** | Nenhum. Nenhuma placa comprada. |
| **Firmware** | Compila para RP2350B (`fw/build-rp/dreamcast_gdrom.uf2`). Nunca correu em silício. |
| **Documentos** | 17 |
| **Código** | `fw/` — 434 checks, firmware RP2350B, leitor de GDI e CUE, ferramentas `cue2gdi` e `gdsniff` |
| **Data** | 2026-09-28 |

## 2. Índice

| # | Documento | Conteúdo |
|---|---|---|
| 01 | [Protocolo SPI Sega](01-protocolo-spi-sega.md) | Norma. 8 registos, comandos ATA, 16 comandos SPI, packets de 12 B, formatos de resposta, Sense Keys, comandos `0x70`/`0x71` |
| 02 | [Barramento G1](02-barramento-g1-pinout.md) | Pinout de 50 pinos **verificado contra o esquema**, alimentação, NC, registos de timing do HOLLY |
| 03 | [Timing IDE](03-timing-ide.md) | Tabelas ATA-3 PIO e MWDMA **verificadas na fonte primária**, reconciliação dos 4 números de velocidade da Sega, implicações para o PIO |
| 04 | [Clock 33.8688 MHz](04-clock-aica.md) | MCK/B23, aritmética do PLL do RP2350, as duas soluções exactas, CD-DA |
| 05 | [Análise do Dreamdrive](05-analise-dreamdrive.md) | O único ODE Dreamcast com firmware aberto. Pinout, PIO, task file, maturidade |
| 06 | [Análise do ZuluIDE](06-analise-zuluide.md) | Referência de engenharia ATA. O PIO IDE do RP2350 é **código fechado** |
| 06b | [PIO vs DMA](06b-scope-pio-vs-dma.md) | ⚠️ **Questão aberta.** A Sega diz que o host usa MWDMA-2 |
| 07 | [Referências de código](07-referencias-codigo.md) | iceGDROM, Flycast, MAME, Linux, libKOS, e por onde começar a ler |
| 08 | [Arquitetura proposta](08-arquitetura-proposta.md) | Camadas, divisão de núcleos, pré-atenção, ordem de bring-up, Definition of Done |
| 09 | [Riscos e licenças](09-riscos-e-licencas.md) | 14 riscos, matriz de licenças, questões legais, decisões pendentes |
| 10 | [Viabilidade de pinos e PCB](10-viabilidade-pinos-e-pcb.md) | Porquê RP2350B e não Pico 2, a restrição do PIO 0–31, level shifting, conector |
| 11 | [Estratégia de validação](11-estrategia-de-validacao.md) | Flycast como modelo de ouro, simulador de host, golden traces, PIO sniffer, loopback |
| 16 | [Sniffer do barramento G1](16-sniffer-g1.md) | PIO de captura por mudança de estado, buffer circular, análise das perguntas A, B, C, E e F, analisador de PC |
| 12 | [Correções ao briefing](12-correcoes-ao-briefing.md) | 34 correcções, para não voltarmos a propagar |
| 13 | [Estudo do Flycast](13-estudo-flycast.md) | 15 diferenças encontradas, 7 questões ainda abertas (5 só com hardware) |
| 15 | [Formatos de imagem GD](15-formatos-imagem-gd.md) | GDI e CUE, a armadilha do LBA 45000 vs FAD 45150, CUE→GDI |
| **fw/** | [Núcleo do emulador](../fw/README.md) | **Código.** 434 checks em seis suites, firmware RP2350B, Apache-2.0 |
| **fw/pio/** | [Programa PIO do G1](../fw/pio/g1_timing.pio) | Timing ATA-3 provado por interpretador |

> `ref/` (ex.: Flycast clonado, GPL-2.0, só referência) **não vai para o
> repo**: consulta local. Ver [07](07-referencias-codigo.md).

## 3. Os cinco achados que mais mudam o projecto

1. **O clock de 33.8688 MHz é pré-requisito absoluto, não uma feature.** Sem MCK no B23 a
   AICA não inicializa e o sintoma não aponta para a causa. ([04](04-clock-aica.md))
2. **PIO vs DMA é uma questão em aberto, não uma decisão.** A Sega documenta que o host
   configura MWDMA-2. Se o ODE for só PIO, pode não funcionar. ([06b](06b-scope-pio-vs-dma.md))
3. **A spec Sega não tem nenhum timing de bus.** Todo o design do PIO tem de ser justificado
   com o ATA-3, não com a Sega. ([03](03-timing-ide.md))
4. **Um Pico 2 não chega.** São ~40 pinos contra 30 do RP2350A. Obrigatório RP2350B
   QFN-80 em PCB própria, com level shifting. ([10](10-viabilidade-pinos-e-pcb.md))
5. **O `dma_bus_handler` do Dreamdrive está errado** — o PIO gera o clock no pino `/RD` do
   host, o que é errado em DMA. Reimplementar de raiz se DMA for necessário. ([06b §3.1](06b-scope-pio-vs-dma.md))

## 4. As três decisões que precisa de tomar

Ver [09 §5](09-riscos-e-licencas.md).

| # | Decisão | Opções |
|---|---|---|
| **D1** | Licença | BSD-2 (permite reusar o Dreamdrive) vs GPL (permite reusar MAME/Linux) |
| **D2** | Risco de hardware | Placa própria com level shifting (meses) vs. adaptador com fios (dias) |
| **D3** | Ambição | Repo de referência (não funcional) vs. ODE funcional (×3 tempo) |

## 5. O que se pode fazer já, sem hardware

De [11 §6](11-estrategia-de-validacao.md), os passos 1–3 não precisam de placa nenhuma:

1. **Simulador de host** (PC) + a máquina de estados da §7.1 da spec, a correr contra a
   nossa camada SPI em código nativo. Validar contra golden traces do Flycast.
2. **Loopback PIO no RP2350**: um PIO gera CS/RD/WR com timings PIO-3, outro é o nosso PIO
   real. Valida `t0`, `t3`, `t5`, `t6`, `t9` **sem pinos**.
3. **Testes de L5** (GDI/CUE, TOC, sectores) contra imagens de referência.

## 6. Ordem de bring-up quando houver placa

De [08 §8](08-arquitetura-proposta.md). **Cada passo é verificável isoladamente:**

```
1. Scope no B23                    → 33.8688 MHz. Sem isto, nada mais funciona.
2. Analisador lógico nos 33 sinais → ciclo PIO-3 correcto
3. Registos + INTRQ + IORDY        → o host lê Status / Drive Select
4. 0xA0 + packet de 12 B           → o host passa a pedir o packet
5. REQ_MODE / REQ_STAT / GET_TOC   → contra o Flycast
6. CD_READ em PIO                  → sector conhecido == imagem
7. Boot até ao logo Sega            → ⚠️ esperar aqui
8. 0x70/0x71, TEST_UNIT, REQ_ERROR
9. CD-DA + subcode
10. DMA (se necessário)
```

## 7. Glossário rápido

| Termo | Significado |
|---|---|
| **G1** | Conector de 50 pinos entre a placa-mãe e o GD-ROM. Superset do IDE de 40 pinos |
| **SPI** | *Sega Packet Interface*. Protocolo proprietário sobre registos ATA-3. **Não é SPI serial** |
| **Pack Interface** | O mesmo: packet de 12 B, Byte 0 = comando |
| **MCK** | 33.8688 MHz, saída do GD-ROM para a AICA, pino B23 |
| **CD-DA** | Áudio de CD. 5 pinos: MCK, BCLK, LRCK, SDTATA, EMPH |
| **FAD** | Frame Address Number. Sector absoluto, 0 = centro do lead-in |
| **IORDY** | Host pronto. Estender o ciclo de transferência |
| **PIO** | Programmable I/O do RP. Máquinas de estado de hardware com 1 ciclo de 1 instrução |
| **B23** | Pino do G1 com MCK |
| **A25/B25** | 🔴 **+12 V** (motor do disco). Fonte única, não confirmada |

## 8. Regra desta fase

> **Nada entra aqui sem uma URL de fonte primária.**

Cada afirmação normativa neste directório tem a citação literal e o link. Quando não foi
possível verificar, está marcado como 🔶 (inferido) ou ⚠️ (não confirmado) — e há três
dessas: a pinagem de alimentação do G1, a existência de timings de 60-300 ns, e se o host
real usa DMA.

---

## Fontes primárias

| Documento | URL |
|---|---|
| **GD-ROM Protocol SPI (Sega Packet Interface) Specs Ver.1.30** | [archive.org](https://www.archive.org/download/SCSISpecificationDocumentsSCSIDocuments/Vendor%20SCSI%20documents/Sega/GD-ROM%20Protocol%20SPI%20(Sega%20Packet%20Interface)%20Specifications%20Ver.1.30.pdf) · [segaretro.org](https://segaretro.org/images/7/72/Cdif131e.pdf) |
| **Dreamcast Hardware Specification Outline** | [segaretro.org](https://segaretro.org/images/8/8b/Dreamcast_Hardware_Specification_Outline.pdf) |
| **Dreamcast Dev. Box System Architecture** | [segaretro.org](https://segaretro.org/images/7/78/DreamcastDevBoxSystemArchitecture.pdf) |
| **Disc Format Standard Specifications Ver.1.0** | [antime.kapsi.fi](https://antime.kapsi.fi/sega/files/ST-040-R4-051795.pdf) |
| **ATA-3 (X3T13/2008D Rev 7b = ANSI X3.298-1997)** | [cs.jhu.edu](https://www.cs.jhu.edu/~huang/cs318/fall20/project/specs/ata-3-std.pdf) |
| **RP2350 Datasheet** | [pip.raspberrypi.com](https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf) |
| **Hardware design with RP2350** | [pip.raspberrypi.com](https://pip.raspberrypi.com/documents/RP-008280-DS-hardware-design-with-rp2350.pdf) |

## Código

| Projecto | URL | Licença |
|---|---|---|
| **Dreamdrive** (khill25) | [github](https://github.com/khill25/Dreamdrive) · [Hackaday log](https://hackaday.io/project/190347) | BSD-2 (fw) / não-comercial (hw) |
| **iceGDROM** (mrlxx / zeldin) | [github](https://github.com/mrlxx/iceGDROM) | ⚠️ verificar |
| **Flycast** | [github](https://github.com/flyinghead/flycast) | 🔴 GPL-2.0 |
| **ZuluIDE** | [github](https://github.com/ZuluIDE/ZuluIDE-firmware) | GPL-3.0 + HSL |
| **MAME** | [github](https://github.com/mamedev/mame) | GPL-2.0+ |
| **Linux kernel** | `drivers/cdrom/gdrom.c` | GPL-2.0 |
| **libKOS** | [kos-docs](https://kos-docs.dreamcast.wiki/g1ata_8h_source.html) | BSD-2 |
