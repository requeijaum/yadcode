# 23 — A tabela de syscalls está nos discos de retail, e é comum a todos

> Documento de trabalho, 2026-09-29. Fecha a W1 e corrige a inversão
> IP.BIN/1ST_READ.BIN que o [19 §2.3](19-kos-e-reversao-da-bios.md) tinha.
> Método e armadilhas: [24](24-ghidra-re.md).
>
> **Nada validado em hardware.** Ver [16 §8](16-sniffer-g1.md).

## 1. A cadeia de boot, correcta

O que se extrói e analisou é o **`1ST_READ.BIN`** — o binário do jogo, 3.5 MB
— e **não** o `IP.BIN`. São ficheiros distintos, e a distinção muda o que
o ODE tem de servir:

```
BIOS (0x8C000000)
  └─ lê o GD-ROM por SPI
      └─ carrega o IP.BIN para 0x8C008000
          └─ o IP.BIN carrega o 1ST_READ.BIN
              └─ o 1ST_READ.BIN traz a biblioteca GD e executa
```

A prova, toda verificável:

| | `1ST_READ.BIN` (o que extraí) | `ip.bin` do SDK |
|---|---|---|
| Header `SEGA SEGAKATANA` no offset 0 | **não** | **sim**, como `read1st.htm` |
| `Lib Handle Start` no offset 0x20 | **sim** | não |
| Vector de syscall `0x8C0000BC` | **13 ocorrências** | **zero** |
| Registos G1 | nenhum (alinhado) | nenhum |
| Tamanho | 3.564.486 B (SPEED_DEVILS) | 32.768 B, dos quais 13 KB de código |

O `ip.bin` do SDK (`katana/shinobi/lib/ip.bin`, md5
`ae4eec9dd39b7b6404bd1bc0440331c3`) é o **esqueleto do loader**: tem o
header documentado e o nome `1ST_READ.BIN` no offset `0x60`, mas nenhum
código de GD-ROM. Serve de referência para o endereço de carga e o
formato, que é o que [17](17-l1-esqueleto.md) precisa.

A tabela abaixo é medida por `fw/tools/gd_systable.c`, que é
re-executável e imprime o offset, os 13 números de syscall e o sha256. O
`docs/24` §6 explica porquê isto existir: já houve duas leituras erradas
ao nível do byte nesta sessão.

```sh
cd fw && make tools
./build/gd_systable /caminho/1st_read.bin
```

## 2. A tabela

Cada `1ST_READ.BIN` de retail traz a mesma tabela de 13 thunks, e é a
**mesma** que o `gdc_lib_` do SDK documentou em [21](21-gdc-syscalls.md).
Thunk, 20 bytes:

```
00 00 00 00              r7 = número do syscall
bc 00 00 8c              r0 = 0x8C0000BC, vector de trap do SH4
00 e6 02 d7 02 d0 02 60  mov.l @r0,r0 ; jmp @r0
2b 40 09 00
```

| Thunk | Syscall | | Thunk | Syscall |
|---|---|---|---|---|
| 0 `_gdGdcReqCmd` | 0 | | 7 `_gdGdcCheckDmaTrans` | 7 |
| 1 `_gdGdcGetCmdStat` | 1 | | 8 `_gdGdcReadAbort` | 8 |
| 2 `_gdGdcExecServer` | 2 | | 9 `_gdGdcReset` | 9 |
| 3 `_gdGdcInitSystem` | 3 | | 10 `_gdGdcChangeDataType` | 10 |
| 4 `_gdGdcGetDrvStat` | 4 | | 11 `_gdBtGdcReInitEntry` | **0** |
| 5 `_gdGdcG1DmaEnd` | 5 | | 12 `_gdBtGdcAddDesc` | **1** |
| 6 `_gdGdcReqDmaTrans` | 6 | | | |

Os dois últimos repetem 0 e 1: é o **mesmo namespace** notado em
[21 §2.1](21-gdc-syscalls.md) — o *GD bridge*, não a API GDC. Quem os
tratar como `ReqCmd` e `GetCmdStat` chama a syscall errada.

## 3. A tabela é comum a todos os jogos

| Disco | Jogo | Offset | sha256 da tabela |
|---|---|---|---|
| 02 | DINO_CRISIS | `0x16DE40` | `5afa69191882059a` |
| 03 | MKGOLD1 | `0x15F5E4` | `5afa69191882059a` |
| 05 | SPEED_DEVILS | `0x0A1190` | `5afa69191882059a` |
| 06 | NEO | `0x0A4620` | `5afa69191882059a` |
| 08 | ZOMBIE_REVENGE | `0x214538` | `9ee324095d824490` |
| 01 | GDMENU | — | ausente (6 vectores, não 13) |

**Quatro de cinco são byte-idênticos** — confirmado por
`gd_systable`, não por leitura manual. O 08 difere só no *layout* dos
literais (o número de syscall vem noutro offset), não no código. O 01 é
`GDMENU`, um menu de diagnóstico e não um jogo — coerente não ter o driver.

**Isto é a consequência prática para o ODE:** o driver é comum a todos os
discos de retail, porque todos os jogos ligam a mesma biblioteca da Sega.
O contrato de entrada é **fixo** — não varia por jogo, nem por região, nem
por geração de hardware. É a melhor notícia que este projecto teve sobre
o que o firmware tem de suportar.

## 4. A convergência, por quarta vez

O mesmo facto medido de quatro maneiras independentes:

| Fonte | O que mostra |
|---|---|
| [19 §2.3](19-kos-e-reversao-da-bios.md) | BIOS de retail: 3402 constantes P4 contra 75 na janela G1 |
| [19 §3.1](19-kos-e-reversao-da-bios.md) | KallistiOS fala por `syscall_gdrom_*`, não por registos |
| [21](21-gdc-syscalls.md) | a biblioteca da Sega faz syscalls, não acede a registos |
| **Este** | os discos de retail trazem a mesma tabela de syscalls |

Conclusão: **a superfície de software do GD-ROM é a API de syscalls da
BIOS.** O protocolo de fio é interno à BIOS, o que explica porque está mal
documentado — ninguém tinha incentivos para publicar um detalhe interno.

Por isso o [22](22-porque-os-rotulos-nao-chegam-ao-c.md) estava certo ao
concluir que o MMIO não aparece no C, e a §4 desse documento tinha a razão
errada: não é acesso por tabela, é **ausência de acesso**. O loader não
toca nos registos G1.

## 5. O que isto diz sobre B e C

- **B** (`0xA1`: abort + blob de 80 bytes). O abort é o **syscall 8**,
  `_gdGdcReadAbort`, um passo explícito e separado na API. Falta saber se o
  `ReqCmd` do `0xA1` o emite por conta própria, ou se a sequência é
  abort → cmd. **Só a BIOS responde** — está no bloco `0x8C000000`–
  `0x8C004000`, que [19 §2.2](19-kos-e-reversao-da-bios.md) já tem importado.
- **C** (DMA). Três syscalls dedicadas de DMA (5, 6, 7), sem nenhuma de
  PIO, e `gdctl_` chama `CheckDmaTrans` explicitamente. Combinado com a
  BIOS de retail não tocar em `SB_G1GDRC`, a hipótese "PIO por omissão, DMA
  disponível" ganha peso. **Não fecha C.**

## 6. O que falta

1. **O dispatcher da BIOS** — o que cada syscall 0–10 faz. Está no bloco
   `0x8C000000`–`0x8C004000` da BIOS, já importada. Resolve B.
2. **A sequência de packets** — os callers de `_gdGdcReqCmd` nas 2196
   funções descompiladas do `1ST_READ.BIN`, com os bytes SPI que empurram.
3. **Medição** — `docs/16` §8.

## 7. Proveniência

Binários de jogos de retail e biblioteca da Sega: **proprietários**, fora
do repositório. Os md5 estão em
`~/backups/yadcode/re-ghidra/MANIFESTO.md`; os ficheiros estão em
`/tmp/opencode/1st_reads/` e **perdem-se no reboot** — reextrair com
`tools/pull_ip.py`, instruções no [24 §5](24-ghidra-re.md).

Regra de [24 §6](24-ghidra-re.md): conclusões no repo são comportamento e
constantes com citação; nada transcrito para `fw/`.
