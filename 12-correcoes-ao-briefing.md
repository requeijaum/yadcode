# 12 — Correções ao briefing original

> Este documento existe para **não voltarmos a propagar** informação errada. Cada linha tem
> a fonte da correcção. ⭐ = o briefing acertou.

## 1. Erros factuais

| # | Afirmação no briefing | Realidade | Fonte |
|---|---|---|---|
| 1 | "o barramento G1 … é um conector de 40 pinos" | **Molex 52602-0579, 2×25 = 50 pinos** (A1–A25, B1–B25) | `pcb/riser/riser.kicad_pcb` + `Molex_52602_0579.lib` do iceGDROM |
| 2 | Tabela de pinout com 40 pinos (pino 1 = /RESET, 2 = GND, 3 = DD7, …) | É o **layout IDE de 40 pinos reordenado**, não o G1. No G1 real, coluna A e coluna B têm sinais **diferentes no mesmo y** (`A7`=DD10 mas `B7`=DD11) | idem |
| 3 | "`GDROM_COM_EXECDIAG` (90h)" | ⭐ **Correto** — `0x90`. (Uma leitura intermédia gerou uma dúvida sobre isto; está certo.) | SPI Spec Tabela 3.3 |
| 4 | "`REQ_STAT` (10h), `GDROM_COM_SOFTRESET` (08h), `GDROM_COM_PACKET` (A0h)" | ⭐ **Corretos** | SPI Spec Tabela 3.3 |
| 5 | Comandos `SET_LBA` (C0), `READ` (C1), `WRITE` (C2), `READ_N` (85), `INIT` (C6), `STOP` (31), `PACKET2` (1A) | **Não existem na spec Sega.** São convenções de firmwares de terceiros. O GD-ROM é read-only | SPI Spec Tabela 3.3; Linux define só 4: `0x08`, `0x90`, `0xA0`, `0xA1` |
| 6 | "buffer de pacotes em 0xF0-0xFF ou similar" | **Não existe.** O packet de 12 bytes trafega pelo **registo Data**, 6 words de 16 bits | SPI Spec §7.1 passo 5 |
| 7 | "4 canais (DATA, SUB, SUB2/FEC)" no SPI | **Não existe.** É o seletor de 4 *flags* em Byte1[7:4] + a opção de 2352 bytes crus. O "4" real vem do GD de alta densidade (4 sub-blocos de 512 B em sectores de 2448 B) | SPI Spec §8.2; MAME `gdrom.cpp` |
| 8 | "osophans/filenames" no protocolo | **Não existe.** Nomes de ficheiro são do **formato do disco**, documento separado | SPI Spec |
| 9 | "MAME: driver em `src/devices/machine/gdrom.cpp`" | **`src/devices/bus/ata/gdrom.cpp`** | mamedev/mame |
| 10 | "RP2350B … tem o dobro dos GPIO do RP2040" | RP2040 e RP2350A têm **ambos 30**. O que dá margem é o **RP2350B = 48** (QFN-80) | RP2350 Datasheet §1.2.1.2 |
| 11 | "PicoGDR … é frequentemente mencionado como uma alternativa mais acessível (DIY)" | 🔴 **Não existe.** GitHub Search API: `picogdr` → **0 repositórios**; `dreamcast GD-ROM GDEMU` → 0. Única fonte em toda a internet: um post de **SEO de 04-05-2026** de uma loja britânica a vender o seu próprio SD card, sem link para firmware | `api.github.com/search/repositories?q=picogdr`; arcadesystems.co.uk |
| 12 | Link `https://arcadesystems.co.uk` como referência de projeto | É uma **loja** (e-commerce de SD cards pré-carregados), não um projecto | idem |
| 13 | "PIO Mode Timing: os parâmetros t1, t2, t9, t5, tA e trd" | `t1`, `t2`, `t9`, `t5`, `tA` existem (ATA-3 Tabela 22). **`t8` não existe no ATA-3** (é timing de IOCS16- em tabelas True IDE/CF). `trd` é `tRd`, existe | ATA-3 Tabela 22, p.127 |
| 14 | "os sinais de leitura e escrita são activados por pulsos que podem durar entre 60 e 300 ns" | 🔶 **Não verificável.** A spec Sega **não tem nenhuma tabela de timings**. O único número temporal oficial é `BSY` válido em **400 ns**. 60-300 ns é inferência de terceiros | SPI Spec (verificada integralmente) |
| 15 | "`33.8688 MHz` … para o chip de som" | ⭐ Correto, e mais preciso: é o **MCK, saída do GD-ROM (device→host) para a AICA**, pino **B23**. **O SH-4 não usa este clock** (corre a 33,3 MHz do PLL do sistema) | SPI Spec §3.2; HW Outline §4.2 |
| 16 | "O RP2350 tem capacidade de emular um dispositivo ATAPI completo" (ZuluIDE) | Parcial. No **RP2350**, o PIO do barramento IDE do ZuluIDE é **código fechado** (`libzuluide_rp2350b_core1_encrypted.a`, core1 Secure) | `ZuluIDE_platform_RP2350/` |

## 2. Afirmações que precisam de qualificação

| # | Afirmação | Qualificação |
|---|---|---|
| 17 | "o clock de 33.8688 MHz … pode ser feito usando um dos PIOs ou dos temporizadores" | ✅ Possível, mas **não com sysclk 270,95 MHz** (o máximo é **228,57 MHz** = VCO 1600 / postdiv1 7). Solução exacta: sysclk **211,68 MHz** (VCO 1481,76) + PIO `clkdiv 6,25`, ou sysclk 33,8688 MHz directo. Ver [04 §4](04-clock-aica.md) |
| 18 | "Dreamdrive … o consumidor já tem o console a arrancar … e a correr jogos" | ⚠️ Mais modesto: `README.md` diz *"Prototype/v3 — Repo files **wont boot yet**"*. O último commit é 2025-03-17 (*"Soul Calibur booting!!!!"*), e o **build não é reproduzível** (deps em falta, `gdrom.c` com 0 bytes) |
| 19 | "ele reescreveu o handler de registos ATA de um busy loop para um programa PIO" | ⭐ Correto. E a optimização que vale a pena reter: `pio0->input_sync_bypass` nos pinos de strobe |
| 20 | "O Dreamdrive … já tem o clock de 33.8688 MHz" | 🔴 **Não.** É um comentário morto (`//266000000.0 / (33868800 * 2); //7.85`) com PIO **sem clkdiv** → 66,5 MHz. O autor não conseguiu |
| 21 | "conector Molex 52602-0579 … difícil de encontrar" | ⭐ Correto. E `52602-4081` (citado noutros contextos) **não existe** — o número em uso é 52602-0579 |
| 22 | "PicoGDR … prova que a abordagem com o Pico é sólida" | O único ODE Dreamcast com firmware aberto e verificável é o **Dreamdrive**. Ver #11 |
| 23 | "Tabela de 40 pinos" / pinout "parcial" | Ver #1, #2 |

## 3. O que o briefing omiteu por completo

| # | Omissão | Porquê importa |
|---|---|---|
| 24 | **PIO vs DMA é uma questão em aberto** | A Sega documenta que o host configura **MWDMA-2** (Dev.Box §2.6.3). O briefing assume que o ODE fala com o host sem mencionar isto. Ver [06b](06b-scope-pio-vs-dma.md) |
| 25 | **RP2350B + PCB própria é obrigatório** | 40 pinos > 30 do RP2350A. O briefing diz que um chip substitui dois RP2040, mas não diz que um Pico 2 **não serve**. Ver [10](10-viabilidade-pinos-e-pcb.md) |
| 26 | **Level shifting 3.3 V ↔ 5 V para VA0** | O RP2350 é 3.3 V; consoles VA0 podem ter 5 V no G1. Sem isto, metade do parque não funciona |
| 27 | **`tB` = 1250 ns (IORDY max)** | É a restrição mais apertada do modo PIO e obriga a pré-atenção real, não cosmética. Ver [03 §6.1](03-timing-ide.md) |
| 28 | **O subcode tem de ser gerado continuamente** | ✅ Spec: *"the subcode information is updated approximately every 13.3 ms"*. Calcular sob demanda dá Q-subcode errado |
| 29 | **A spec Sega não tem timings de bus** | Todo o design de PIO tem de ser justificado com o ATA-3, não com a Sega. É uma lacuna de sourcing, não um detalhe |
| 30 | **Riscos de licença** | Dreamdrive firmware BSD-2 mas hardware não-comercial; ZuluIDE GPL-3.0+HSL; **Flycast GPL-2.0**; iceGDROM GPL-3.0; MAME GPL-2.0. Ver [09](09-riscos-e-licencas.md) |
| 31 | **Estratégia de validação sem hardware** | Sem isso, o projecto não tem critério de aceitação |
| 32 | **IP.BIN é conteúdo proprietário** | O Dreamdrive embute ~4 KB no header. Questão legal para um projecto aberto |
| 33 | **`0x70` e `0x71`** | Comandos **não documentados** e obrigatórios para o boot. O briefing menciona "comandos não documentados" sem dizer quais |
| 34 | **A pinagem de alimentação do G1 tem uma única fonte** | A1/B1=3V3, A3/B3=5V, A25/B25=**12 V** — tudo de fonte única, nada confirmado por esquema. Verificar com multímetro |

## 4. O que o briefing acertou

Vale a pena registar, para não voltar a duvidar:

- ⭐ A stack RP2350 + PIO é a abordagem certa.
- ⭐ O 33.8688 MHz é mesmo obrigatório e tem de vir do ODE.
- ⭐ `REQ_STAT` 0x10, `SOFTRESET` 0x08, `EXECDIAG` 0x90, `PACKET` 0xA0 — todos correctos.
- ⭐ A necessidade de ler a spec SPI Ver.1.30 e de cruzar com NullDC + iceGDROM.
- ⭐ O PIO é necessário para o timing — e o `input_sync_bypass` é mesmo a optimização certa.
- ⭐ A dificuldade de compatibilidade de jogos é real.
- ⭐ Que a camada física ATAPI do ZuluIDE é boa referência.

---

Ver também: [00-README](00-README.md) ·
[06b-scope-pio-vs-dma](06b-scope-pio-vs-dma.md) ·
[09-riscos-e-licencas](09-riscos-e-licencas.md)
