# 15 — Formatos de imagem GD-ROM: GDI e CUE

> Data: 2026-09-28. Implementado em `fw/src/gd_format.h`, `gd_gdi.*`, `gd_cue.*`,
> `gd_fs.*`, e na ferramenta `fw/tools/cue2gdi.c`.
> Fontes: SEGA *GD-ROM Format Basic Specifications Ver. 2.14* (GDP-0000-02, 17/03/1999),
> https://segaretro.org/images/5/5d/Gdfm_k214e.pdf; e o código de referência
> (Flycast, GPL-2.0 — só factos e constantes).

---

## 1. A decisão: GDI no runtime, CUE na entrada

| Formato | Papel | Porquê |
|---|---|---|
| **CUE** (Redump) | entrada | é o formato de **preservação**. Guarda pregap, postgap, índices e as marcas de área. |
| **GDI** | runtime | é uma lista de **LBA absolutos por track**. O mapeamento FAD → ficheiro é aritmética, não lógica. |

O GDEMU, o MODE, o iceGDROM, o Dreamdrive e este ODE lêem **GDI**. O Redump considera o
formato insuficiente para preservation — *"GDI format lacks of track pregap length"* — mas
isso não importa para um ODE, que não precisa de gerar o CUE de volta.

**CUE entra, GDI sai.** A ferramenta `cue2gdi` fecha o fluxo numa só dependência.

⚠️ **O bug do chdman não nos afecta.** A causa **não** é o multi-cue nem os `REM`: é que o
chdman não descarta os 150 sectores de Pause que o Redump embebe no `.raw`, produzindo uma
imagem com 27 540 sectores a menos e metadados CHT2 errados. Como nós **não lemos CHD**, o
problema não existe. Se algum dia precisarmos: GDI → chdman 0.227+.

---

## 2. A armadilha principal: 45000 não é onde os dados começam

Este é o erro que custa mais tempo a quem começa, e está em todo o lado como
"LBA 45000 = alta densidade". É verdade no **GDI**, e falso no **FAD**.

Tabela 4-1 da spec, verbatim:

| Marco | FAD | ATime |
|---|---|---|
| Track No. 03, **head** | `00AFC8H` = **45000** | 10:00:00 |
| **System ID 1** | `00B05EH` = **45150** | 10:02:00 |
| Primary Volume Descriptor 1 | `00B06EH` = 45166 | 10:02:16 |
| Lead Out 1 (máx) | `0861B4H` = **549300** | 122:04:00 |

Portanto:

- **FAD 45000** = o **INDEX 00** da track 3, a zona de Pause de 2 s (150 sectores)
- **FAD 45150** = o **INDEX 01**, onde começam os **dados**

Um ODE que aponte para 45000 em vez de 45150 lê 150 sectores de pause antes do primeiro byte
de dados, e a BIOS não encontra a tabela de boot. `GetBaseFAD()` = **45150**.

E o Redump embebe esses 150 sectores de pause **no início do `track03.bin`**. O `OFFSET 0`
do GDI aponta para eles, e ficam órfãos. É por isso que o mesmo GDI funciona para dumps
Redump (com pause) e TOSEC (sem pause).

### 2.1 Aritmética verificada

```
10 min x 60 x 75 = 45 000 sectors = FAD 45000  (o INDEX 00)
+ 150 sectors de pause       = FAD 45150       (o INDEX 01)
45 000 + 504 300              = 549 300         (o lead-out)
```

Testado no caso 48. Capacidade útil: 36 000 KB (LD) + 1 008 600 KB (HD) = **1 044 600 KB**,
~1,0 GiB. O "~1,2 GB" que circula é o tamanho do ficheiro **com** os 96 bytes de subcode por
sector (549 156 × 2448 = 1 344 333 888 B), não capacidade de dados.

---

## 3. Formato GDI

**Não existe especificação oficial.** Inferido de três implementações independentes
(nullDC, Flycast, e o gdidrop, que é BSD-2). Documentado em
https://dreamcast.wiki/GDI_format

```
<track_count>                                        3..99
<TRACK> <LBA> <CTRL> <SSIZE> <"file"> <OFFSET>       x track_count
```

| Campo | Valor |
|---|---|
| `TRACK` | 1..track_count |
| `LBA` | **LBA, não FAD.** = FAD − 150. Track 1 = 0, track 3 = 45000 |
| `CTRL` | 0 = áudio, 4 = dados |
| `SSIZE` | 2352 (raw, com sync) ou 2048. **0 = track ausente** |
| `file` | opcionalmente entre aspas; o Redump usa sempre |
| `OFFSET` | byte do primeiro sector **da track**. 0 = início do sector 2352, **com o sync** |

```
byte_pos(FAD) = OFFSET + (FAD − (LBA + 150)) × SSIZE
```

Não há mais nada: sem `REM`, sem cabeçalho, sem índice, sem newlines obrigatórios. O parser
é um fluxo de tokens.

### 3.1 Regras de validação

| Regra | |
|---|---|
| 3 ≤ track_count ≤ 99 | obrigatória |
| `CTRL ∈ {0, 4}` | obrigatória |
| track 1 = dados, track 2 = áudio, track 3 = dados | obrigatória |
| **track 3 com LBA = 45000** | obrigatória |
| `SSIZE ∈ {2352, 2048}` | obrigatória (excepto `0` = ausente) |
| ficheiro existe | obrigatória |

LBA de referência em GDI reais: track 1 = 0, **track 3 = 45000 sempre**, track 2 varia
(Crazy Taxi 600, Sonic Adventure 11361, Elemental Gimmick Gear 11511).

### 3.2 O sector de 2352

```
SYNC(12) HEAD(4) data(2048) EDC(4) SPACE(8) ECC(276)  = 2352
                   ^ 0x10 = Mode 1
                     ^ 0x18 = Mode 2
```

O byte 15 distingue Mode 1 (`== 1`) de Mode 2. O `OFFSET 0` aponta para o **sync**, não para
o user data. Testado no caso 56.

### 3.3 O gap entre as duas áreas

O sector entre `EndFAD(track 2)` e `StartFAD(track 3)` **não existe em lado nenhum** — é o
gap entre a área de baixa e a de alta densidade. O backend tem de devolver "ausente", não
zeros. O nosso `track_of_fad` devolve `0xAA` lá no meio. Testado no caso 53.

---

## 4. Formato CUE

### 4.1 Multi-Cue do Redump (o formato de hoje)

```
REM SINGLE-DENSITY AREA
FILE "Game (Track 1).bin" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
REM HIGH-DENSITY AREA
FILE "Game (Track 3).bin" BINARY
  TRACK 03 MODE1/2352
    INDEX 01 00:00:00
```

O marcador `REM HIGH-DENSITY AREA` é o que diz ao parser que é um GD-ROM e onde começa a
área de alta densidade (**FAD 45150**). Sem ele, um CUE de GD é indistinguível de um CD.
Definição do admin do Redump (F1ReB4LL), não da Sega.

Padrão: áudio tem sempre `INDEX 00 00:00:00` + `INDEX 01 00:02:00` (150 sectores de pause).

### 4.2 TOSEC

```
FILE "track03.bin" BINARY
  TRACK 03 MODE1/2352
    PREGAP 10:00:00
    INDEX 01 00:00:00
```

O ficheiro **não** contém o pregap; o leitor tem de o sintetizar.

⚠️ **O briefing dizia `PREGAP 10:00:00`. Correto — mas note-se que 10:00:00 = 45000 = o
INDEX 00, e há folhas Redump com `10:02:00` = 45150, que já aponta para o INDEX 01.**
Aceitamos os dois e distinguimos.

⚠️ **O Flycast não descodifica este formato.** Não tem handler para `PREGAP`, e nunca marca o
disco como GD-ROM. É uma das razões pelas quais este parser existe.

### 4.3 Modos de sector

| Modo | Bytes | CTRL |
|---|---|---|
| `AUDIO` | 2352 | 0 |
| `MODE1/2048` | 2048 | 4 |
| `MODE1/2352` | 2352 | 4 |
| `MODE2/2336` | 2336 | 4 |
| `MODE2/2352` | 2352 | 4 |
| `CDI/2336` | 2336 | 4 |
| `CDI/2352` | 2352 | 4 |

A spec é explícita: *"The High-Density Area data track disc format is compatible only with
Mode1 format, so it does not support Mode2 format"*. Em Dreamcast só se vê `MODE1/2352` e
`AUDIO`.

### 4.4 O que **não** fazemos, e porquê

- **`REM SESSION 01/02`** — errado para GD-ROM. O admin do Redump é explícito: *"GDs aren't
  multisessional, those are 2 separate images written on the same media"*.
- **Áreas, não sessões.** Ver §5.
- **`POSTGAP`** — aceite e registado, mas o GDI não o consegue representar. Perdido.
- **CHD** — não suportado, e não faz falta.

---

## 5. Duas áreas, não duas sessões

Isto é uma correcção ao que escrevemos em `doc 13` e no nosso próprio `gd_taskfile.c`.

A spec define **quatro** marcos: `Lead_In 0`, `Lead_Out 0`, `Lead_In 1`, `Lead_Out 1`. São
**duas áreas com lead-in e lead-out próprios**, mais o security ring. A palavra "session" não
aparece na spec para o GD-ROM.

O que o hardware faz:

| `GET_TOC` Select | Conteúdo | Lead-out |
|---|---|---|
| 0 (baixa densidade) | tracks 1..2 | `EndFAD(track 2) + 1` |
| 1 (alta densidade) | tracks 3..N | **549300** |

⚠️ **O `REQ_SES` continua a devolver 2**, porque é o que o emulador de referência sintetiza e
o que a BIOS aparentemente lê. Mas é um **campo de compatibilidade**, não a realidade física.
Documentado em `gd_format.h` e no comentário de `do_req_ses()`.

---

## 6. O que a implementação faz

### `gd_fs.[ch]` — abstracção de ficheiros

Vtable, para o mesmo código correr no PC (testes, ferramenta) e no RP2350 (SDIO). O contexto
é a raiz contra a qual os caminhos relativos se resolvem.

### `gd_gdi.[ch]` — leitor de GDI

Token stream, validação da geometria, `end_fad` inferido do tamanho do ficheiro
(`(size − offset) / ssize − 1`), e construção da `gd_disc_t` que o resto do emulador consome.

### `gd_cue.[ch]` — leitor de CUE

Multi-Cue e TOSEC. `arg_of()` trata nomes de ficheiro com espaços entre aspas — um
`sscanf` normal parte `"Game (Track 1).bin"` em `"Game` e `(Track`.

⚠️ **A posição das tracks não se calcula no parse.** Depende dos tamanhos dos ficheiros, e
só se sabe no fim. É uma segunda passagem em `gd_cue_to_gdi()`, que acumula os tamanhos a
partir da semente 150 (baixa densidade) ou 45150 (alta densidade).

### `cue2gdi` — a ferramenta

```
$ cue2gdi jogo.cue -v
  1   LBA 0       FAD 150     dados  2352  Game (Track 1).bin
  2   LBA 600     FAD 750     audio  2352  Game (Track 2).bin
  3   LBA 45000   FAD 45150   dados  2352  Game (Track 3).bin
  4   LBA 45500   FAD 45650   audio  2352  Game (Track 4).bin
  5   LBA 45800   FAD 45950   dados  2352  Game (Track 5).bin
```

Um dump TOSEC (só a track 3) gera um GDI de 3 tracks, com 1 e 2 marcadas como ausentes
(`SSIZE 0`, ficheiro `none`) — a convenção que o nullDC usava e que o GDEMU e o MODE comem.

A escrita do GDI vive em `gd_gdi_write()`, na biblioteca, para a ferramenta e os testes
partilharem exactamente o mesmo formato.

---

## 7. Bugs que os testes apanharam

| | |
|---|---|
| **LBA 4294967146** | As tracks 2, 4 e 5 saíam com `GD_FAD_TO_LBA(0)`, que faz underflow em `uint32_t`. Produzia um GDI que **ninguém conseguiria ler**, e sem erro nenhum. Causa: o `start_fad` era fixado no parse em vez de acumulado. Regressão no caso 68. |
| **Nomes com espaços** | `sscanf("%s")` partia `"Game (Track 1).bin"` em dois tokens, e o ficheiro não era encontrado. |
| **`arg_of` não avançava** | Primeira versão aceitava `const char *` em vez de `const char **`, e a segunda `TRACK 01 MODE1/2352` devolvia `a1 == a2 == "01"`. |
| **FAD num `uint8_t`** | No teste, um FAD de 14511 truncava para 175. Lição: FAD é `uint32_t`, sempre. |
| **Reordenar sem aviso** | O padding de tracks ausentes reordenava o array, e o teste procurava a track 3 pelo índice 0. Agora procura pelo número. |

---

## 8. Em aberto

| # | Questão | Como resolver |
|---|---|---|
| H | `OFFSET` não-zero: só se vê `0` (Redump/TOSEC) e `-8` (dumps cdrwin) | Dump antigo, ou Assume-se 0 |
| I | Regra de 150 vs 225 sectores de gap no Pattern III (Shenmue, Bust-A-Move) | É o ponto mais mal documentado; **é onde o chdman parte** |
| J | `LBA` das tracks 4..N: Redump vs TOSEC diferem em 150, 225 e 75 sectores | Testar com imagens das duas origens |
| K | Lead-out: 549300 (spec) ou 549150 (CloneCD)? | Medir num drive real |
| L | A posição de um dump TOSEC: a track 3 é a única, ou há Pattern III sem áudio? | Mais fixtures |

---

Ver também: [13-estudo-flycast](13-estudo-flycast.md) ·
[01-protocolo-spi-sega](01-protocolo-spi-sega.md) ·
[fw/README.md](fw/README.md)
