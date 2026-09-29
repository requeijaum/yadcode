# 19 — O que a BIOS e o KallistiOS revelam sobre o GD-ROM

> Documento de trabalho, escrito em 2026-09-28 a partir de duas fontes:
> engenharia reversa da BIOS de retail (Ghidra 12.1) e leitura do código do
> KallistiOS. **Nada aqui foi confirmado em silício** — ver
> [16 §8](16-sniffer-g1.md). O que é medido está marcado como medido; o que
> é inferência, como inferência.

## 1. Resumo do que ficou provado

| # | Afirmação | Estado |
|---|---|---|
| 1 | A BIOS carrega em `0x8C000000`, não em `0x00000000` | **medido** |
| 2 | O Ghidra 12.1 descompila SH4 sem extensões | **medido** |
| 3 | O driver de GD-ROM **não** está na BIOS | **medido** |
| 4 | A BIOS de retail não toca em `SB_G1GDRC` | **medido** — evidência para C |
| 5 | O KOS fala com o GD-ROM por syscalls do BIOS | **medido** no código |
| 6 | A BIOS traduz entre dois namespaces de comando | **medido** no código |
| 7 | Recompilar a BIOS 1:1 é impossível | **argumento** (§6) |

## 2. A BIOS: o que se pode e o que não se pode extrair

Imagem analisada: `dc_boot.bin`, md5 `e10c53c2f8b90bab96ead2d368858623`
= **v1.01d, VA1, Japão** (identificação por `SEGAKATANA KABUTO Ver.1.01d` e
pelas strings de módulo `gdFs Ver 1.07` / `GDC Version 1.10 1999-03-31`).

### 2.1 O endereço de carga é obrigatório

Importar o dump no endereço por omissão do Ghidra dá um resultado enganador:

| | `0x00000000` | `0x8C000000` |
|---|---|---|
| funções detectadas | 339 | **1717** |
| cobertura | 1,5% | **13,9%** |
| instruções disassembladas | — | 199.284 |

A causa: 323 ponteiros absolutos para `0x8C00xxxx` no dump, que só fecham
com o endereço certo. Confirma o [dreamcast.wiki, Boot process][boot]: a
BIOS e as syscalls vivem em `0x8C000000`–`0x8C004000`.

[boot]: https://dreamcast.wiki/Boot_process

### 2.2 O Ghidra descompila SH4 sem tocar em nada

> Método completo, onde está cada artefacto e as seis armadilhas:
> [24](24-ghidra-re.md).

O Ghidra 12.1 traz o módulo `SuperH4:LE:32:default` já compilado
(`SuperH4_le.sla`). Testado: **1717/1717 funções descompilam**, com C
legível — assinaturas correctas, variáveis de FPU, fluxo de controlo.

O que o Ghidra **não** faz sozinho, e o que custou mais tempo a este
estudo: os registos do barramento G1 e do ASIC precisam de ser *definidos*
antes de aparecerem no C, e as técnicas-ingénuas (criar rótulos) **não
funcionam** — ver §2.4.

### 2.3 O driver de GD-ROM não está na BIOS — [medido]

Este é o achado que redireciona o trabalho. Contagem de constantes de 32
bits no dump:

| Janela | Ocorrências |
|---|---|
| P4 / Holly (`0xA0xxxxxx`) | **3402** |
| G1 (`0x8C000060`–`0x8C001000`) | **75** |

As 75 ocorrências da janela G1 concentram-se na faixa `0x8C0000B0`–
`0x8C0000C0`, que é a inicialização da janela, não o driver. O dispatcher de
comandos (`GDC Version 1.10`) é a superfície de syscalls da BIOS, e é
**quem executa** a leitura do disco para depois carregar o IP.BIN.

A cadeia real de boot, conforme o [dreamcast.wiki][boot] e confirmado pelo
`ip.bin` do SDK (`katana/shinobi/lib/ip.bin`, cujo header bate com
`katana/read1st.htm`):

```
BIOS (0x8C000000)
  └─ lê o GD-ROM por SPI, usando os packets
      └─ carrega o IP.BIN para 0x8C008000
          └─ o IP.BIN carrega o 1ST_READ.BIN
              └─ o 1ST_READ.BIN traz a biblioteca GD e executa
```

Ou seja: **o binário que o ODE tem de entregar correctamente é o
`1ST_READ.BIN`**, não a BIOS nem o IP.BIN. A BIOS dá o *contrato*; o
`1ST_READ.BIN` traz a *implementação*.

> ⚠️ **Correcção de 2026-09-29.** Este parágrafo dizia antes que o driver
> estava no `IP.BIN`. **Estava errado**, e a inversão altera o que o ODE
> tem de servir. O que se EXTRAIU e se analisou (`/tmp/opencode/dctest/1st_read.bin`,
> 3.564.486 bytes) é o `1ST_READ.BIN` — o **binário do jogo** — e não o
> `IP.BIN`. A prova:
>
> - o `1ST_READ.BIN` **não** tem o header `SEGA SEGAKATANA`, que o
>   `ip.bin` tem no offset `0x00` conforme `read1st.htm`; tem
>   `Lib Handle Start` no offset `0x20`, uma estrutura da biblioteca Sega;
> - o `ip.bin` do SDK tem **zero** ocorrências do vector de syscall
>   `0x8C0000BC` e **zero** registos G1 — é um *stub* de 32 KB com 13 KB
>   de código, o esqueleto do loader, não o driver;
> - o `1ST_READ.BIN` tem **13** ocorrências desse vector (ver
>   [23](23-gdrom-syscall-table.md)).
>
> A consequência é mais forte do que a frase original: o driver é
> **comum a todos os discos de retail**, porque todos os jogos ligam a
> mesma biblioteca da Sega. O contrato do ODE é fixo, não varia por jogo.

### 2.4 O que não funciona, para não repetir

- **Rótulos (`createLabel`) não chegam.** Aplicados `G1GDRD`, `G1GDRC`,
  `SB_G1GDRC` etc., resultedam em **0 funções** a mencioná-los no C. A BIOS
  acede ao barramento por `mov.l @(disp,PC)` seguido de acesso indirecto: o
  Ghidra resolve a constante para um registo intermédio e o rótulo nunca
  chega à decompilação.
- **Os endereços P4 têm duas formas.** O `dc-re-ghidra` escreve
  `0xA05F74A0`; o dump usa `0x005F74A0`. São o mesmo registo — o SH4 forma
  a constante com o bit P4 a 1. Criar o bloco em `0x005F0000` e rotular
  lá é correcto, mas o Ghidra precisa que os dados sejam **tipados** como
  `dword` (`createData`), não apenas rotulados.
- **A cobertura de 13,9% é o tecto com o alinhamento simples.** O resto é
  código de arranque, tabelas e dados. Não é limitação do decompiler.

## 3. O KallistiOS: a superfície de comandos

`kernel/arch/dreamcast/hardware/cdrom.c` **não** fala com o GD-ROM ao nível
dos pacotes SPI: usa syscalls do BIOS (`syscall_gdrom_*`).

### 3.1 Dois namespaces distintos — [medido]

Os mesmos números, comandos completamente diferentes:

| KOS (syscall) | valor | ODE (SPI no fio) | valor |
|---|---|---|---|
| `CD_CMD_PIOREAD` | 16 (`0x10`) | `GD_SPI_REQ_STAT` | `0x10` |
| `CD_CMD_DMAREAD` | 17 (`0x11`) | `GD_SPI_REQ_MODE` | `0x11` |
| `CD_CMD_GETTOC` | 18 (`0x12`) | `GD_SPI_SET_MODE` | `0x12` |
| `CD_CMD_NOP` | 29 (`0x1D`) | `GD_SPI_GET_TOC` | `0x14` |
| `CD_CMD_GETSCD` | 34 (`0x22`) | `GD_SPI_GET_SCD` | `0x40` |

**Consequência:** o KOS não pode servir para validar os códigos de comando
SPI que o firmware implementa. A BIOS traduz entre os dois, e essa tradução
está no firmware proprietário.

> **Confirmado por um terceiro caminho** ([21](21-gdc-syscalls.md)): a
> própria biblioteca da Sega (`gdc_lib_.obj.elf`) faz syscalls ao vector
> `0x8C0000BC` e não toca em registo nenhum. Três fontes independentes —
> contagem de constantes na BIOS, o KOS, e a biblioteca da SEGA — chegam
> à mesma conclusão. A superfície de software do GD-ROM é a API de
> syscalls da BIOS; o protocolo de fio é interno dela.

É também a melhor explicação já encontrada para **porque é que o protocolo
de fio está mal documentado**: é um detalhe interno da BIOS, que ninguém
teve incentivos para publicar. Confirma a hierarquia de fontes que o
projecto já adoptou — a [spec SPI da SEGA](01-protocolo-spi-sega.md) é a
verdade do fio, o [Flycast](13-estudo-flycast.md) é o comportamento, e
nenhum SDK serve de fonte para o fio.

### 3.2 Device select — [medido]

De `kernel/arch/dreamcast/include/dc/g1ata.h`:

| Constante | Valor | Significado |
|---|---|---|
| `G1_ATA_MASTER` | `0x00` | master — é o GD-ROM |
| `G1_ATA_MASTER_ALT` | `0x90` | master, bits reservados a 1 |
| `G1_ATA_SLAVE` | `0xB0` | slave — disco rígido |
| `G1_ATA_LBA_MODE` | `0x40` | endereçamento LBA |

> The GD-ROM really does not like the reserved bits being set in the device
> select register.

> Do not use this constant to access the GD-ROM. It will not work.

Isto expõe uma divergência no firmware actual — ver §5.

### 3.3 DMA: estreita a questão C, não a fecha

O KOS lê sectores do GD-ROM com `CD_CMD_DMAREAD` (17) e
`CD_CMD_DMAREAD_STREAM` (28) **por omissão em hardware de retail**. E
documenta:

- alinhamento de **32 bytes para DMA** contra **16 para PIO**;
- `G1_ATA_DMA_PROTECTION = 0x005F74B8`, código de unlock `0x8843`;
- `SYSMEM = 0x8843407F`, `ALLMEM = 0x8843007F`.

Cruza com [06b](06b-scope-pio-vs-dma.md), que já registava que o lado Linux
tem de activar o bit 0 explicitamente. Os dois factos convivem: **PIO por
omissão na leitura normal, DMA disponível e usado em streaming.**

**Previsão testável:** um programa compilado com KOS usaria DMA, e portanto
não arrancaria num ODE só-PIO.

## 4. O que o KOS não resolve

Sendo explícito, porque muda a expectativa:

- **A** (`0x71`: 6 bytes ou 1012?) — detalhe de fio, invisível acima da BIOS.
- **B** (`0xA1`: abort + blob de 80 bytes) — idem.
- **E** (`GD_LEADOUT_FAD = 549300`) e **F** (CRC do subcode) — exigem um
  drive real, como já diz [13 §7](13-estudo-flycast.md).
- **C** — fica *estreitada*, não fechada. Ver §2.3: a ausência de
  `SB_G1GDRC` na BIOS de retail é evidência de que o caminho DMA não é o
  por omissão, mas não substitui um scope.

## 5. Divergência aberta no firmware

[medido] `fw/src/gd_taskfile.c:284` guarda o byte completo de
`GD_R_DRIVESEL`, incluindo `0x90` e `0xB0`.
[medido] `fw/src/gd_taskfile.c:396` preserva apenas `& 0x80` no soft-reset.

Ou seja: o firmware é internamente inconsistente, e responde a
device-selectes que um drive real rejeitaria (§3.2).

**Decisão em aberto (questão H):** deve o ODE deixar de responder a `0x90` e
`0xB0`? Um ODE é um master único, logo a resposta natural seria responder
apenas a `0x00` e `0x40`. **Não implementado** — sem silício para validar,
e o custo de estar errado é assimétrico. Registado em [13 §7](13-estudo-flycast.md)
como H.

## 6. Recompilar a BIOS 1:1: porquê não

- A BIOS foi compilada em 1999 com o compilador proprietário da Hitachi,
  que se perdeu.
- O `gcc-12-sh4-linux-gnu` (12.4.0, instalado) gera codegen diferente:
  outra selecção de instruções, outra distribuição de registos, outro
  alinhamento.
- Não existe caminho de ida e volta para os mesmos bytes, e ninguém o
  recuperou em 27 anos.

O que **é** alcançável, e é de onde sai o conhecimento: C legível,
renomeado, comentado. O toolchain serve para escrever código novo e testar
hipóteses — não para recriar a BIOS.

## 7. Método, para repetir

Ferramentas: Ghidra 12.1 (SuperH4:LE:32, sem extensões), `gcc-12-sh4-linux-gnu`
para código novo, `sp00nznet/dcrecomp` como referência de que SH4 é
recompilável.

Para a BIOS:

```sh
analyzeHeadless proj dc -import dc_boot.bin \
  -processor "SuperH4:LE:32:default" \
  -loader BinaryLoader -loader-baseAddr 0x8C000000
```

Sem `-loader-baseAddr 0x8C000000` o resultado é enganador (1,5%).

## 8. Passos seguintes

1. **Analisar o `1ST_READ.BIN` / `IP.BIN`**, que é onde está o driver de
   GD-ROM. Já foi extraído com sucesso de um dump de retail
   (`SPEED_DEVILS`, 3.564.486 bytes, md5 `f83a1466…`) — a extração está
   validada. Falta importá-lo em `0x8C008000` e mapeá-lo.
2. **Definir a address space MMIO** correctamente, para os registos G1
   aparecerem no C. É o que falta para renomear em escala.
3. Aplicar os nomes de [dc-re-ghidra][dcr] aos que corresponderem.

### 8.1 Nota sobre a conversão de LBA para FAD

A conversão é `FAD = LBA + 150` e o sector pedido pode cair **noutra track**
que não a que se estava a ler. Um erro aqui manifesta-se como "o ficheiro
não existe", e é enganoso: o conteúdo está presente, só está noutro sítio.
O dump usado (`track11.bin`, 780 MB) contém o `1ST_READ.BIN` a 775,9 MB
dentro do ficheiro.

[dcr]: https://github.com/iamsh4/dc-re-ghidra
