# 08 — Arquitetura proposta

> Documento de **desenho**, não de implementação. O objectivo é fixar as decisões de
> arquitectura que ficam fechadas antes de escrever a primeira linha de firmware.

---

## 1. Divisão em camadas

```
┌─────────────────────────────────────────────────────────────────┐
│  L6  Menu / seleção de disco          (dreammenu.c do Dreamdrive)│
├─────────────────────────────────────────────────────────────────┤
│  L5  imgread  — GDI / CUE, TOC, multi-sessão, 2048/2352/2448   │
├─────────────────────────────────────────────────────────────────┤
│  L4  CD-DA + subcode  (BCLK, LRCK, SDTATA, EMPH, Q 13.3 ms)    │
├─────────────────────────────────────────────────────────────────┤
│  L3  SPI Sega  — 16 comandos, packets de 12 B, respostas        │
├─────────────────────────────────────────────────────────────────┤
│  L2  Task file  — 7 registos 8-bit + Data 16-bit, INTRQ,       │
│                   máquina de estados §7.1 da spec              │
├─────────────────────────────────────────────────────────────────┤
│  L1  PHY G1  — PIO: CS0/CS1/DA decode, strobes, IORDY, (DMA)   │
├─────────────────────────────────────────────────────────────────┤
│  L0  Board  — RP2350B QFN-80, level shifting, Molex 52602-0579  │
└─────────────────────────────────────────────────────────────────┘
        ⊥ sideways: L7  clock 33.8688 MHz (PIO dedicado, B23)
```

**Regra de dependência:** só as camadas superiores conhecem as inferiores pela interface, nunca
pela implementação. L3 não sabe se L5 é SD, eMMC ou RAM.

## 2. Divisão de núcleos

| | core0 | core1 |
|---|---|---|
| Papel | L3-L6: protocolo, SPI, imgread, menu | L1: PIO + task file |
| Latência | tolerante | **crítica** |
| Localização do código | flash | **`__not_in_flash_func` no caminho crítico** |
| Comunica com | core1 | core0 (eventos) |

Isto é a arquitetura do Dreamdrive e está certa: o loop que tem de reagir a um strobe de
~80 ns não pode estar em flash nem partilhar cache de dados com o resto.

⚠️ **Cuidado com o RPC.** O core1 tem de responder ao host em **t0 = 180 ns**. Isso
significa que o core1 **nunca** pode bloquear à espera do core0 num caminho de dados. O
padrão correcto é o inverso do que o Dreamdrive faz: **o core1 tem sempre os dados**,
pré-carregados num ring buffer, e **avisa** o core0 (fire-and-forget), em vez de fazer
`push` e esperar. Ver §3.2.

## 3. O problema do IORDY e a pré-atenção

### 3.1 Por que o duplo-buffer não basta

O ATA-3 impõe `tB` = **1250 ns máximo** para o pulso de IORDY negado ([03 §6.1](03-timing-ide.md)).
Isto significa: se o host pedir dados e o IORDY continuar negado mais de 1250 ns, o
dispositivo está **em violação de spec** e o comportamento é indefinido.

O Dreamdrive gere isto com pré-leitura de 16 setores. **É a resposta certa, e a razão
pelo qual a pré-atenção é obrigatória e não uma optimização.**

### 3.2 Padrão de ring buffer pré-carregado

```
core0:  enfileira N setores no ring, longe do risco
       notifica core1 (FIFO, sem bloquear)

core1:  no strobe, tem os dados JÁ no ring
       responde no PIO sem qualquer round-trip à CPU
```

Com `t0` = 180 ns e ~22 ciclos de PIO a 211 MHz, o core1 tem tempo de fazer um `push` e um
`pull` de 32 bits. Mas **não** tem tempo para um `multicore_fifo_drain` com arbitragem.
Logo: **o core1 lê de RAM partilhada, não de um canal com hand-shake.**

⚠️ Isto é uma divergência deliberada face ao Dreamdrive, e é a razão pela qual a DMA
handler dele não tem paralelo em PIO: o `pull` do core1 no `write_to_dreamcast` **pode
bloquear** se o core0 não tiver dado dados.

### 3.3 Dimensionamento

Com `t0` = 180 ns e pré-atenção de 2× o pior caso (`tB` = 1250 ns), um anel de 4 setores
(4 × 2352 B ≈ 9,4 KB) dá margem de ~4,6 µs. Com o disco no cartão SD a 30-60 MB/s, ler
9,4 KB leva ~200 µs. **O gargalo nunca é o buffer, é o SDIO** — ver §5.

## 4. PIO: partilha de estado entre núcleo e máquina

Problema: o core1 mantém o registo Index. Quando o host escreve no registo Data, o valor
dessas palavras tem de ser transferido para o PIO.

Padrão escolhido (o do Dreamdrive, que funciona):
```
host escreve Data → PIO faz push ao core1 → core1 responde
```

**Alternativa a registar:** PIO em `side_set` para o `out` dos dados, e o core1 só
actualiza o ring buffer quando o PIO sinaliza "linha completa". **Isto elimina a espera
completamente** ao custo de um `out null, 16` extra por palavra. É a solução que
gostaria de testar primeiro, porque transforma a latência num detalhe.

## 5. Armazenamento: SDIO, não SPI

⚠️ O Dreamdrive usa **SPI a 62,5 MHz** com o comentário *"Not even sure if it will go that
fast"*. A 10 MB/s de payload, com blocos de 32 B, isso é impossible de sustainar.

O RP2350 tem **SDIO nativo** (o ZuluIDE usa-o via `sdio_rp2350_config.h`, e o
`rp2040_sdio.pio` funciona no RP2350). Com SDIO a 4 bits, o bandwidth é de sobra para os
10-13 MB/s do barramento G1.

**Decisão: SDIO.** Isto liberta os 4 pinos `PIN_SD_D0-3` e deixa o SPI0 livre.

⚠️ **Ponto de estrangulamento a medir:** a fatia de tempo é o **latency**, não o bandwidth.
Um `f_read` de 2048 B num cartãoFAT tem latência de ~1-2 ms. Com `tB` = 1250 ns de IORDY,
**um único sector por vez não chega** — é obrigatório ler **vários setores por comando** e
aceitar blocking-ness no core1 (i.e., o core1 serve do ring, e o ring tem de ser
profundamente pré-cheio: ver §3.1).

## 6. Camada L2 — task file

```c
typedef struct {
    uint8_t data;          /* 16-bit word, mascarado a 8 quando não é o Data reg */
    uint8_t error, features, seccnt, secnr, cyl_lo, cyl_hi, drive_head;
    uint8_t altstatus, devcontrol, int_reason;
} gd_taskfile_t;
```

- `data` é o **único** registo de 16 bits; os restantes mascaram `& 0xFF`.
- `int_reason` tem `CoD` no bit 0 e `IO` no bit 1, e é **read-only**.
- `devcontrol.nIEN` (bit 1) controla o tri-state de `INTRQ`.
- **Drive Select = `0x00`, sempre.** Nunca `0x90`.

Decodificação: adoptar a tabela de indexação directa do Dreamdrive
(`registerIndex_map[128]`, indexada por `W<<5 | CS1<<4 | CS0<<3 | A2<<2 | A1<<1 | A0`). É
simples e o hot path fica sem branches.

## 7. Camada L3 — SPI Sega

- `switch` puro sobre `packet[0]`, sem alocação, sem blocking na fase de setup.
- Cada handler preenche uma `gd_response_t` (offset + length) e chama
  `l2_start_data_phase()`.
- **`REQ_MODE`, `REQ_STAT`, `GET_TOC` geram risco ALTO** — são lidos por praticamente todos
  os jogos ([01 §7](01-protocolo-spi-sega.md)). São os primeiros a testar.
- `SET_MODE` é o **único** comando com dados host→device.
- `0x70`/`0x71` respondidos com GOOD / blob plausível e transicionar PAUSE/STANDBY.

## 8. Ordem de bring-up sugerida

Cada passo é verificável isoladamente, e **depende só do anterior**:

| # | Passo | Como se verifica |
|---|---|---|
| 1 | Gerar 33.8688 MHz em B23 | **Scope.** Sem isto nada mais funciona |
| 2 | L0/L1: PIO descodifica CS0/CS1/DA, responde a reads de registo | **Analisador lógico** nos pinos. `0x50` a byte-clears |
| 3 | L2: registos, INTRQ, IORDY | Host consegue ler Status/Drive Select |
| 4 | L2/L3: `0xA0` + packet de 12 B | O host passa a pedir o packet (`CoD=1, DRQ=1`) |
| 5 | L3: `REQ_MODE`, `REQ_STAT`, `GET_TOC` | Python de teste: enviar packet, comparar com o Flycast |
| 6 | L3/L5: `CD_READ` em PIO | Ler um sector conhecido e comparar com a imagem |
| 7 | Boot até ao logo Sega | Testar. ⚠️ esperar aqui |
| 8 | `0x70`/`0x71`, TEST_UNIT, REQ_ERROR | Advanced Titles / Resident Evil |
| 9 | L4: CD-DA + subcode | Teste de áudio em jogo |
| 10 | DMA (se [06b](06b-scope-pio-vs-dma.md) exigir) | Reimplementar de raiz |

⚠️ **O passo 1 é pré-requisito absoluto.** A AICA sem clock não inicializa, e o sintoma
(vídeo morto ou arranque instável) não aponta para a causa.

## 9. Definition of Done por fase

| Fase | Critério |
|---|---|
| 0 — Estudo | Este directório. Documentos normativos com fontes primárias |
| 1 —PHY | Analisador lógico mostra ciclo PIO-3 correcto (180 ns/word) nos 33 sinais |
| 2 — Task file | O BIOS lê Drive Select e Status; `0x08` soft reset limpa a task file |
| 3 — SPI | Python de teste envia os 16 comandos e valida contra o Flycast |
| 4 — Leitura | Um `CD_READ` devolve os bytes correctos de um FAD conhecido |
| 5 — Boot | Chega ao logo Sega, depois ao menu BIOS, depois a um jogo |
| 6 — CD-DA | CD-ROM de áudio toca; subcode coerente |
| 7 — Completo | 3+ jogos de loja diferentes, incluindo 1 com CD-DA |

## 10. O que NÃO fazer

1. **Não implementar DMA antes de PIO.** O caminho de erro mais provável.
2. **Não usar SPI a 62,5 MHz para o cartão SD.** Dáverte a banda.
3. **Não enviar `0x90` no Drive Select.** Bit reservado, o GD-ROM não gosta.
4. **Não implementar `IOCS16-` / `DASP-` / `PDIAG`.** Não existem no G1.
5. **Não assumir multi-sessão.** `FillGDSession()` do Dreamdrive é hardcoded a sessão
   única e nunca foi testado. Começar por uma.
6. **Não bloquear o core1 à espera do core0.** Ver §2.
7. **Não usar o `dma_bus_handler` do Dreamdrive.** Está errado ([06b §3.1](06b-scope-pio-vs-dma.md)).

---

Ver também: [10-viabilidade-pinos-e-pcb](10-viabilidade-pinos-e-pcb.md) ·
[11-estrategia-de-validacao](11-estrategia-de-validacao.md) ·
[09-riscos-e-licencas](09-riscos-e-licencas.md)
