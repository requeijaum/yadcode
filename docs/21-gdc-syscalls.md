# 21 — A superfície de syscalls GDC: o que o Katana SDK realmente chama

> Documento de trabalho, 2026-09-29. Fecha o W1 do plano: a sequência de
> funções que a biblioteca de alto nível da Sega usa para falar com o
> GD-ROM. Tudo aqui é leitura de `shinobi.elf.lib` (SeGa, 1999-11-09),
> com o offset da secção e o valor lidos byte a byte do ELF32 — não do
> `objdump`, que desalinha estes objectos (ver [20 §6.3](20-shinobi-crc-subcode.md)).
>
> **Nada validado em hardware.** Ver [16 §8](16-sniffer-g1.md).
>
> ⚠️ **Aviso, 2026-09-29:** este documento está correcto no que mede — a
> tabela de syscalls do `gdc_lib_`. O `docs/19` §2.3, que o complementa,
> tinha uma inversão IP.BIN/1ST_READ.BIN **já corrigida** nesse dia, e o
> `docs/22` §4 tinha contagens erradas **já corrigidas** também.
> Complemento: [23](23-gdrom-syscall-table.md).
>
> ⚠️ **Aviso, 2026-09-29:** este documento é correcto no que mede — a
> tabela de syscalls do `gdc_lib_`. O `docs/19` §2.3, que o complementa,
> tinha uma inversão IP.BIN/1ST_READ.BIN **já corrigida** nesse dia.

## 1. O achado

A biblioteca GD do Katana SDK, `gdc_lib_.obj.elf`, **não toca nos registos
do GD-ROM**. Cada uma das suas funções é um *thunk* de 20 bytes:

```asm
_gdGdcReqCmd:
   0:  00 e6        mov    #0,r6              ; retorno = 0 (void)
   2:  02 d7        mov.l  c <+0x0c>,r7       ; r7 = número do syscall
   4:  02 d0        mov.l  0x8C0000BC,r0      ; r0 = vector de trap
   6:  02 60        mov.l  @r0,r0             ; r0 = handler
   8:  2b 40        jmp    @r0
   a:  09 00        nop
   c:  00 00        .word 0x0000              ; <- o número do syscall
  10:  bc 00 00 8c  .word 0x8C0000BC          ; <- o vector
```

`0x8C0000BC` é o vector de system call do SH4 (o trap que o BIOS
intercepta). O número do syscall é o literal em `+0x0C`; o vector, em
`+0x10`. Os bytes crus da função 0, para quem quiser conferir:

```
00 e6 02 d7 02 d0 02 60 2b 40 09 00 00 00 00 00 bc 00 00 8c
```

**Isto confirma, por um terceiro caminho independente, a conclusão de
[19 §2.3](19-kos-e-reversao-da-bios.md).** Já tínhamos:

- a BIOS de retail usa 3402 constantes P4 contra 75 na janela G1;
- o KallistiOS fala com o GD-ROM por `syscall_gdrom_*`, não por registos.

Agora a **biblioteca da própria Sega** faz exactamente o mesmo: syscalls,
zero registos. Três fontes, o mesmo resultado — a superfície de software do
GD-ROM é a API de syscalls da BIOS, e o protocolo de fio é interno dela.

## 2. A tabela

Lida de `gdc_lib_.obj.elf`, secção `.text` (offset `0x34`, tamanho
`0x104` = 260 bytes = 13 × 20). Números extraídos dos literais em `+0x0C`.

| # | Símbolo | Syscall | Nota |
|---|---|---|---|
| 0 | `_gdGdcReqCmd` | 0 | **pedir um comando SPI** — o caminho de B |
| 1 | `_gdGdcGetCmdStat` | 1 | estado do comando em curso |
| 2 | `_gdGdcExecServer` | 2 | executar o servidor de comandos |
| 3 | `_gdGdcInitSystem` | 3 | inicialização |
| 4 | `_gdGdcGetDrvStat` | 4 | **status do drive** — o blob de `0xA1`, caminho de B |
| 5 | `_gdGdcG1DmaEnd` | 5 | **fim de DMA no G1** — caminho de C |
| 6 | `_gdGdcReqDmaTrans` | 6 | **pedir DMA** — caminho de C |
| 7 | `_gdGdcCheckDmaTrans` | 7 | **verificar DMA** — caminho de C |
| 8 | `_gdGdcReadAbort` | 8 | **abort** — caminho de B |
| 9 | `_gdGdcReset` | 9 | reset |
| 10 | `_gdGdcChangeDataType` | 10 | tipo de dados (áudio vs dados) |

Onze funções, números **0–10, sequenciais e distintos**. O que a Sega
chama de GDC é uma tabela indexada, não um conjunto de rotinas nomeadas —
o que explica porquê que o namespace de syscalls do KOS ([19 §3.1](19-kos-e-reversao-da-bios.md))
e este sejam o mesmo com numeração diferente.

### 2.1 As duas funções que *não* são GDC

`_gdBtGdcReInitEntry` (0xDC) e `_gdBtGdcAddDesc` (0xF0) usam o **mesmo
vector** `0x8C0000BC`, mas os seus literais são **0** e **1** — repetem os
números 0 e 1 da tabela GDC.

Conclusão: **é um segundo namespace**, o *GD bridge* (a API de
descritores de blocos, usada pelo `gdFs` para mapear ficheiros), não a
API GDC. Registado porque colidir números é uma armadilha: quem tratar
`gdBtGdcAddDesc` como `gdGdcReqCmd` chama a syscall errada.

## 3. O que isto diz sobre B e C

O [20 §4](20-shinobi-crc-subcode.md) apontou `_gdGdcReadAbort`,
`_gdGdcGetDrvStat`, `_gdGdcReqDmaTrans` e `_gdGdcCheckDmaTrans` como
indícios. Agora sabemos o que são com precisão:

- **B** (`0xA1`: abort + blob de 80 bytes). O abort é o **syscall 8**, um
  passo próprio e explícito da biblioteca, não um efeito colateral do
  `IDENTIFY`. Isto é mais forte que o indício anterior: o SDK **pede**
  o abort. Falta saber se o `ReqCmd` do `0xA1` o emite por conta própria
  antes, ou se a sequência é abort → cmd. **Isso só o BIOS responde.**
- **C** (DMA). Três syscalls dedicadas de DMA (5, 6, 7) ao lado de
  nenhuma de PIO, e `gdctl_` chama `CheckDmaTrans` explicitamente. A
  biblioteca **tem** um caminho de DMA em vez de código condicional.
  Combinado com a BIOS de retail não tocar em `SB_G1GDRC`, a hipótese
  "PIO por omissão, DMA disponível" ganha peso — mas continua a não
  ser medição.

## 4. O que falta

- **O dispatcher da BIOS.** Os números 0–10 são a chave; o que cada um
  faz está no firmware da BIOS (o bloco `0x8C000000`–`0x8C004000`, que o
  [19 §2.2](19-kos-e-reversao-da-bios.md) já tem importado). Mapear essa
  tabela é o próximo passo natural, e é o mesmo `GDC Version 1.10` que
  o `docs/19` §2.3 identificou por strings.
- **Medição.** Nenhuma destas leituras foi confrontada com um drive real.
  O [16 §8](16-sniffer-g1.md) continua a ser o caminho, e a sequência de
  syscalls observada tem de bater com a tabela 0–10.

## 5. W2: `_gdFsGetGdDcf` não tem o leadout

Objecto `gdctl_.obj.elf`, secção `PSG` (offset `0x34`, `0x360` bytes).
Importado no Ghidra em `0x8C400000` e descompilado em `0x8C400340`:

```c
undefined4 gdFsGetGdDcf(void) {
    return uRam8c40035c;    /* relocacao R_SH_DIR32 -> CSG + 0 */
}
```

**Quatro bytes. É um stub** — não chama `GetDrvStat` nem `CheckDmaTrans`
nesse ponto; as cinco relocações R_SH_DIR32 para `_gdGdc*` que o
`readelf` mostra em `0x348`–`0x358` estão **fora** do corpo da função
(que acaba em `0x344`). Pertencem ao código que segue.

O `CSG` de `gdctl_` são 80 bytes: uma **tabela de 20 offsets** para a
`PSG`, com os cinco últimos relocados. Os valores são
`0, 14, 142, 170, 258, 444, 450, 480, 532, 560, 574, 608, 642, 658, 688,
716, 750, 764, 770, 802` — saltos dentro do próprio objecto, isto é, uma
tabela de **handlers**, não de constantes de disco.

`549300` (`GD_LEADOUT_FAD`) **não aparece**.

**Conclusão sobre a questão E:** o `gdFsGetGdDcf` do SDK **não contém o
leadout**. É um dispatcher de handlers por código de erro, e o leadout
either vem do BIOS (via `GetDrvStat`, syscall 4) ou é uma convenção do
formato de imagem, não uma constante do SDK. **E não se resolve aqui** —
fica para o `docs/16` §8, como estava.

Isto é um resultado negativo, e é informação: fechamos onde procurar
(não é aqui) em vez de deixar em aberto.

## 6. Proveniência

| Ficheiro | Offset | Conteúdo |
|---|---|---|
| `shinobi.elf.lib` → `gdc_lib_.obj.elf` | `.text` @ `0x34`, `0x104` | 13 thunks de syscall |
| `gdctl_.obj.elf` | `PSG` @ `0x34`, `0x360` | o dispatcher de alto nível |

Regra de [20](20-shinobi-crc-subcode.md) e de `ref/README.md`:
comportamento e constantes, com citação. Nada transcrito, nada copiado
para o `fw/` — isto é estudo, não firmware.
