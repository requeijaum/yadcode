# 13 — Estudo do Flycast: o que faltava

Data: 2026-09-28. Flycast clonado em `ref/flycast`, HEAD `e36e9df` (2026-09-23).
Ficheiros: `core/hw/gdrom/{gdromv3.cpp,gdromv3.h,gdrom_response.cpp,gdrom_if.h}`,
`core/imgread/{common.h,common.cpp,ImgReader.cpp,gdi.cpp,cue.cpp,chd.cpp}`.

> 🔴 **O Flycast é GPL-2.0**, não BSD-2 como eu tinha escrito nos documentos 09, 00 e 12.
> Verificado no `LICENSE` da raiz. **Nada foi copiado.** Extraí-se comportamento e valores
> numéricos, e reimplementou-se a partir da spec.

---

## 1. As 15 diferenças que o estudo revelou

Todas implementadas e cobertas por testes.

| # | Assunto | A spec | O GD-ROM real | Estado |
|---|---|---|---|---|
| 1 | `REQ_MODE` Byte 6 | máscara de 4 bits | **`0x19`**, não `0x1e` | ✅ |
| 2 | `REQ_MODE` [10..31] | nomes livres | **`"SE      "`, `"Rev 6.43"`, `"990408"`** | ✅ |
| 3 | `REQ_STAT` Byte 4 (Index) | não especificado | **sempre `1`** | ✅ |
| 4 | `REQ_STAT` Byte 2 | "Control/ADR" | preenchido da track corrente | ✅ |
| 5 | `REQ_STAT` Bytes 5-7 | FAD | **FAD corrente**, não o lead-out | ✅ |
| 6 | `GET_TOC` ADR | não especificado | **forçado a 1** (só canal Q tem subcode) | ✅ |
| 7 | `GET_TOC` [396..403] | "start/end track" | o campo FAD leva o **número da track** em BE | ✅ |
| 8 | `GET_TOC` densidade única | não especificado | só tracks **1 e 2**; a 3+ é `0xFF` | ✅ |
| 9 | Sessões | "0-99" | um GD-ROM tem **sempre 2** (track 1 e track 3) | ✅ |
| 10 | Soft Reset | task file do power-on | Byte Count = **`0x14` / `0xEB`** | ✅ |
| 11 | `REQ_ERROR` | limpa no comando seguinte | limpa **imediatamente** ao responder | ✅ |
| 12 | `TEST_UNIT` | "não reporta CHECK" | põe CHECK se a unidade estiver **BUSY** | ✅ |
| 13 | `SET_MODE` | known-length | só os offsets **0..9** são graváveis | ✅ |
| 14 | `CD_READ` tamanho | 5 tipos na spec | o GD-ROM só produz **2048, 2340 ou 2352** | ✅ |
| 15 | Alternate Status | "não limpa DMA status" | **só Status limpa o INTRQ** | ✅ |

### 1.1 O bug mais grave que isto revelou

`sync_status()` reconstruía o registo Status **inteiro** a partir da fase. Um erro
sinalizado antes de terminar a transferência era **apagado pela própria transição que o
deveria confirmar** — o host nunca via o CHECK, só fazia polling. Tinha escapado aos 20
casos porque o caminho do comando ATA inválido punha o CHECK *depois* de `sync_status()`.

É a classe de bug que só a comparação com a referência denuncia: compila, parece correcto,
e não faz nada de útil.

---

## 2. `0x70` e `0x71` — comandos não documentados

### `0x70` — sem resposta
Não muda o estado. O comentário na referência: *"mount/map drive ? some kind of
reset/unlock ?? seems like a non data command"*. Ninguém sabe o que faz. Seguro ignorar.

### `0x71` — divergência entre fontes

| Fonte | Resposta | Licença | Corre em hardware? |
|---|---|---|---|
| iceGDROM `do_cmd71()` | `BA 06 0D CA 6A 1F` (6 B) | GPL-3.0 | ✅ sim |
| Dreamdrive `cmd71_reply[]` | `BA 06 0D CA 6A 1F` (6 B) | BSD-2 | ✅ sim |
| MAME `GDROM_Cmd71_Reply[]` | ~200 B | GPL-2.0 | ❌ emulador |
| Flycast `reply_71` | **1012 B** | GPL-2.0 | ❌ emulador |

As duas fontes que correm em hardware real usam 6 bytes. **Mantidos 6.**

⚠️ **O que importa não é o conteúdo, é a transição de estado:**

> *"Command 71 seems to trigger some sort of authentication check(?). … If the drive is fed
> with a 'bootable' disc it ends up in 'PAUSE' state. On all other cases it ends up in
> 'STANDBY'."*

- Disco `GdRom` ou `CdRom_XA` → **PAUSE**; caso contrário → **STANDBY**
- A transição acontece **antes** de o host consumir os dados

**É este o mecanismo de detecção de "GD-ROM bootável" por software de protecção, e é o que
faz o boot avançar.** Implementado.

### `0xA1` — não reproduzido
O comportamento real inclui `Error = 0x4` (ABRT) **e** um blob de 80 bytes — ou seja
responde com erro *e* com dados — e ignora o offset pedido. A spec descreve um IDENTIFY
normal. Sem fonte permissiva nem dump de hardware, inventar os bytes seria pior do que ficar
pelo IDENTIFY sensato. Registado como dívida (R6 em [09](09-riscos-e-licencas.md)).

---

## 3. PIO vs DMA — resposta mais clara, e diferente do que eu achava

O selector é o **Features bit 0**, e o **Sector Count é ignorado** (há um `*FIXME*` no
código da referência).

⚠️ **E o DMA do Dreamcast não é um handshake IDE.** É o canal GD do **DMAC do SH4**. O host
programa `SB_GDSTAR`/`SB_GDLEN`/`SB_GDDIR`/`SB_GDEN`/`SB_GDST` e o device **gera o
clock**. Não há `DMARQ`/`DMACK` handshaked.

Isto muda a implementação: não há máquina de estados de DMA com handshake para simular, mas
o device tem de servir dados a uma taxa que não controla. **Continua a ser uma questão
aberta** — ver [06b](06b-scope-pio-vs-dma.md).

## 4. Timings que a referência impõe

| Transacção | Taxa |
|---|---|
| > 10240 bytes | **1,8 MB/s** (taxa do disco) |
| ≤ 10240 bytes | **25 MB/s** (bus G1 a 50 MHz × 16 bits, reduzido por `wsb2k2`) |
| `FastGDRomLoad` | 512 ticks — atalho **não-emulativo**, a ignorar |

⚠️ Isto **corrige parcialmente** os 10 MB/s do *Hardware Specification Outline*: os 25 MB/s
do bus são plausíveis, mas o disco não dá mais que ~1,8 MB/s no caminho longo. O `t0`
efectivo numa transferência real é o de 1,8 MB/s, não o do bus.

---

## 5. Subcode: BCD no formato 0, binário no formato 1

**Formato 0** (100 B): Q em **BCD**, CRC-16 sobre os 10 primeiros bytes, P de 96 B por
**expansão** (cada byte do Q → 8 bytes, `0x40` por bit a 1).

**Formato 1** (14 B): número da track em **binário**, Byte 4 binário, **sem CRC**.

**Formato 2** (UPC, 24 B) e **3** (ISRC, 16 B) — implementados.

O Q é sintetizado da TOC, não lido do disco; o P real só existe em fontes de 2448 B/CHD.

### 5.1 O CRC tem uma divergência que não consigo resolver sozinho

| Variante | poly | init | reflect | xorout | `crc("123456789")` |
|---|---|---|---|---|---|
| **CRC-16/XMODEM** (padrão) | 0x1021 | 0x0000 | não | `0x0000` | **0x31C3** |
| **Variante do GD-ROM** | 0x1021 | 0x0000 | não | `0xFFFF` | **0xCE3C** |

A referência implementa `return ~crc`, isto é, com complemento final. O XMODEM padrão
**não** tem. A diferença é um complemento bit a bit, e como o subcode é copiado verbatim
pelos jogos, importa.

**Mantida a variante do GD-ROM**, porque é o comportamento observado em hardware. Registada
como questão **F**.

**Actualização 2026-09-28** ([20](20-shinobi-crc-subcode.md)): o `shinobi.elf.lib` do
Katana SDK **confirma a variante do GD-ROM** e dá-lhe a razão. O polinómio é `0x1021`
(CRC-CCITT), o `init` é `0xFFFF` e há **complemento final** (`fmcalccrc`, offset `0x2c`:
`not r0,r0` seguido de `extu.w`). Ou seja: é o XMODEM com `init` e `NOT` — exactamente a
distinção que o texto acima descreve. A tabela foi extraída e verificada 256/256, e o
algoritmo está em `fw/src/gd_crc.c` (casos 97–101).

Falta apenas a **medição** para fechar por completo: o `docs/16` §8.

---

## 6. `imgread` — sector size e as constantes do GD-ROM

| Fonte | Sector size |
|---|---|
| **GDI** | `SSIZE` tem de ser 2352 ou 2048, senão **rejeita** |
| **CUE** | `AUDIO`/`CDG`/`MODE1/2352` → 2352 · `MODE1/2048` → 2048 · `MODE2/2336` → 2336 · `CDI/2336` → 2336 |
| **CHD** | `AUDIO`/`MODE1_RAW`/`MODE2_RAW` → 2352 · `MODE1` → 2048 · `MODE2` → 2336 |

Regras de validação do GDI que vale a pena reimplementar:
- `trackCount` entre 3 e 99; `CTRL` só 0 (áudio) ou 4 (dados)
- track 1 = dados, track 2 = áudio, track 3 = dados
- **track 3 tem `FADS == 45000`** — é isto que define a fronteira densidade única/dupla
- `StartFAD = FADS + 150`

Constantes que o `fw/` já tem:
- **`GD_LEADOUT_FAD = 549300`**
- **`GetBaseFAD() = 45150`** para um GD-ROM — o FAD a partir do qual a BIOS procura a tabela
  de boot. ⚠️ **Ainda não implementado no `fw/`.**

---

## 7. Questões em aberto

| # | Questão | Como resolver |
|---|---|---|
| **A** | `0x71`: 6 bytes ou 1012? | **Estreitada** por [25](25-opengdemu-comportamento.md) §3: 6 B é suficiente e é o que um GDEMU stock aceita. O que uma drive real põe além dos 6 continua aberto |
| **B** | ~~`0xA1`: abort + 80 bytes?~~ **RESPONDIDA** | **Não aborta: devolve 512 bytes.** O abort está no `0xEC`, e é imposto pela ATA. [25](25-opengdemu-comportamento.md) §4 |
| **C** | O host real usa DMA? | **Evidência forte** de [25](25-opengdemu-comportamento.md) §5: o canal de DMA da BIOS aborta a ~10 KB, o que só existe se o host usar DMA. Falta a medição directa. [06b](06b-scope-pio-vs-dma.md) |
| D | `GET_SCD` formato 2/3 é usado por algum jogo? | Testar jogos com CD-DA |
| E | `GD_LEADOUT_FAD = 549300` é universal? | GDI real de alta densidade |
| **F** | ~~CRC do subcode~~ **RESOLVIDA** | **A variante complementada**: `0x1021`, init `0xFFFF`, NOT final. [20](20-shinobi-crc-subcode.md) §7. Falta medir |
| G | `GetBaseFAD() = 45150` — usado pela BIOS como? | Dump do BIOS + rastreio |
| **H** | **O device select `0x90`/`0xB0` deve fazer o drive deixar de responder?** | **Documentado, não implementado.** Ver [19 §5](19-kos-e-reversao-da-bios.md). O OpenGDEMU **não implementa** device select ([25](25-opengdemu-comportamento.md) §7) |

**B fecha sem hardware.** Das que restam, só A, C, D e E se resolvem com medição;
G e H continuam sem caminho que não seja tracear a BIOS. Ver
[11 §4.1](11-estrategia-de-validacao.md).

> **Questão nova, vinda de [25](25-opengdemu-comportamento.md) §5, e a mais grave
> que apareceu:** o canal de DMA da BIOS aborta a ~10 KB, e o primeiro read é de
> 7 sectores (14 KB, o IP.BIN). **Um emulador que sirva `CD_READ` com latência no
> caminho não arranca.** É requisito de firmware, não optimização.

A **H** apareceu depois: o KallistiOS documenta que o GD-ROM não aceita
os bits reservados no device select, e o firmware actual é internamente
inconsistente a esse respeito ([19 §5](19-kos-e-reversao-da-bios.md)). A
questão **C** ficou *estreitada* — a BIOS de retail não toca em
`SB_G1GDRC`, o que aponta para PIO por omissão, mas não a fecha.

---

## 8. O que mudou no plano

1. **L3 está em boa forma** — 28 casos, todos os valores de resposta conferidos.
2. **L4 (CD-DA + subcode) implementado** — 11 casos novos, com o contraste BCD/binário
   testado directamente.
3. **O DMA não é o que eu pensava** — rearchitectar, não acrescentar.
4. **O sniffer subiu de prioridade** — resolve A, B, C, E, F de uma vez.
5. **`GetBaseFAD() = 45150` e `GD_LEADOUT_FAD`** — a primeira falta no `fw/`.

---

Ver também: [06b-scope-pio-vs-dma](06b-scope-pio-vs-dma.md) ·
[09-riscos-e-licencas](09-riscos-e-licencas.md) · [fw/README.md](../fw/README.md) ·
[11-estrategia-de-validacao](11-estrategia-de-validacao.md)
