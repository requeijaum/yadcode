# 22 — Porquê os rótulos dos registos G1 não chegam ao C, e o que resolve

> Documento de trabalho, 2026-09-29. Complementa [19 §2.4](19-kos-e-reversao-da-bios.md),
> onde se registou que os rótulos não apareciam no C. Aqui está o
> diagnóstico completo, e **o que ficou resolvido e o que não**.
>
> Resumo: **o W3 não ficou feito**, e a causa é mais simples do que se
> pensava: o loader não acede aos registos G1. Vai por syscalls da BIOS.
>
> ⚠️ **Aviso, 2026-09-29:** a §4 foi corrigida depois de escrita. As
> contagens de constantes G1 estavam erradas (feitas sem alinhamento) e a
> conclusão foi revista. Ver a nota de correcção na §4.

## 1. O que foi tentado, e o que cada passo revelou

Quatro hipóteses, todas testadas no `1ST_READ.BIN` importado em
`0x8C008000` (2196 funções, 2195 descompiladas):

| # | Hipótese | Resultado | O que revelou |
|---|---|---|---|
| 1 | Faltam os rótulos | 0 funções com `G1GDRD` | rótulos aplicados, símbolo existe |
| 2 | Falta `setVolatile(true)` | 0 funções | bloco volatile, mas ainda 0 |
| 3 | Falta tipar como `dword` | **tipo é `null`** | ver §2 |
| 4 | Acesso é indirecto, Ghidra não materializa | **confirmado** | ver §3 |

## 2. O erro silencioso: `DWORD` não existe na SuperH4

O `dc-re-ghidra` usa `createDWord()`, que cria um tipo **Standard Data
Type** de nome `DWORD`. A SuperH4 do Ghidra **não tem esse tipo**. Todos
estes caminhos devolvem `null`:

```
/unsigned_dword  NULO      /uint       NULO      /DWORD   NULO
/ulong           NULO      /uint32_t   NULO      /dword   NULO
/Unsigned DWORD  NULO      /long       NULO      /int     NULO
/undefined4      /undefined4 len=4   <-- o unico
```

`createData(addr, null)` não dá erro: **não faz nada**, em silêncio. Daí o
sintoma de "o rótulo existe mas não aparece no C" — o símbolo estava lá, a
tipagem nunca aconteceu, e o Ghidra emitia o acesso como `DAT_8C000080`.

Aplicado `/undefined4` + `CreateDataCmd(a, true, u4)`, o dado é criado
(`data: /undefined4 len=4`). **Este passo está corrigido e é reutilizável.**

## 3. A causa raiz: o acesso é por tabela, não directo

Contagem de referências a `0x8C000080` no Ghidra, depois de tudo aplicado:
**0**. Mas a constante existe no binário, em `0x8C0656D9`. E o contexto
em torno mostra que é uma **tabela de dados**, não código:

```
off 0x05D6D9:  03a008a8 01f00300 8000008c 701a8ca0 25088c00
off 0x0B8589:  c8290332 240b0000 e000008c 685fa0e6 2fd62ff4
off 0x0D0D3C:  32602b40 264f0000 e000008c 90ea0f8c 48d200e3
```

Repare no padrão `... 0000 8c00 ...` e no alinhamento a 4. Estas são
**tabelas de vectores** (ponteiros para handlers, ou tabelas de registos
indexados) que o loader usa com `mov.l @(disp,PC)` seguido de acesso
indirecto.

O Ghidra materializa a referência da *instrução* ao *literal* (a
constante na tabela), mas **não** do literal ao *endereço de destino*,
porque o acesso final é `@Rn` — um registo cujo valor o decompiler
trata como dado, não como endereço de memória. `setVolatile` e
`/undefined4` tornam o literal legível, mas não transformam um
ponteiro-em-registo num acesso a memória nomeado.

**Consequência:** para aparecer `G1GDRD` no C é preciso que o
decompiler faça *value propagation* do literal para o registo, e depois
trate esse registo como ponteiro para o bloco volatile. Isto é um problema
**do decompiler para SH4**, não do script, e o `dc-re-ghidra` só o
resolve porque trabalha em binários onde os endereços são
`mov.l #imm,Rn` (imediato directo, não tabela).

## 4. O que isto deixa de fazer, e o workaround

> ⚠️ **Correcção de 2026-09-29.** As contagens abaixo estavam erradas. Foram
> feitas por `bytes.find()` sobre o padrão de bytes, que casa também em
> posições **não alinhadas** dentro de outras constantes. A tabela real,
> alinhada a 4 bytes, é quase vazia. E a conclusão muda: **não há acesso
> por tabela — não há acesso directo.** Ver [23](23-gdrom-syscall-table.md).

O W3, tal como foi vendido, **não é alcançável neste binário**. O que é
alcançado:

| | Estado |
|---|---|
| Rótulos criados | 14/16 G1, 12/12 P4 — feito |
| Blocos volatile | feito |
| Tipagem `/undefined4` | feito (bug corrigido, reutilizável) |
| Nomes no C | **não** |

### 4.1 As contagens, corrigidas

No `1ST_READ.BIN` de SPEED_DEVILS, procurando o valor de 32 bits:

| Registo | Por `bytes.find()` (errado) | Alinhado a 4 B (real) |
|---|---|---|
| `G1RAM` `0x8C000000` | 19 | **1** |
| `G1GDRD` `0x8C000080` | 1 | **0** |
| `G1TBAL` `0x8C0000E0` | 3 | **1** |

O único "G1GDRD" era `80 00 00 8c` no offset `0x05D6D9`, **desalinhado**:
os valores de 32 bytes reais nessa zona são `0x8C1A708C`, `0x8C0825A0` e
`0x8C06CAE0`, e a zona é **código** (um pool de literais dentro de uma
função), não dados.

### 4.2 A conclusão correcta

O loader **não toca nos registos G1 de todo**. Vai por syscalls da BIOS
(vector `0x8C0000BC`), como o SDK da Sega ([21](21-gdc-syscalls.md)), como
o KOS e como a BIOS. Isto é a **mesma** convergência que o §2.3 mediu por
outro caminho.

Logo, `volatile` e a tipagem correcta não podem ajudar: o decompiler não
tem um acesso a registo para nomear. Não é uma limitação do Ghidra para
SH4, é que o acesso não existe nesta camada.

## 5. A lição, que vale mais que o W3

O `docs/19` §2.4 registou que os rótulos "não funcionam". A causa **não**
era a que parece: não era falta de volatile, nem de tipagem, nem de
indexação. Era que **o loader acede ao barramento por tabela, e o
decompiler do Ghidra para SH4 não segue o ponteiro através de registo**.

Isto generaliza: para SH4, o MMIO é legível no assembly e não no C. O
C é para o resto da lógica — a máquina de estados dos packets, as
sequências de comando, a lógica de retry — que é onde está a maior parte
do valor. O acesso ao bus tem de ser lido à mão, com as coordenadas
cima.

O mesmo se aplica à BIOS (`docs/19` §2.1), e explica porquê que a
contagem de 1717 funções é útil mas não vai dar os registos G1 por
nome.

## 6. O que fica por fazer, revisto

1. **Sequência de packets, lida do assembly** — substitui o W3 como
   caminho para B. Usa as coordenadas da §4.
2. **Comparar os seis `1ST_READ.BIN`** (`/tmp/opencode/1st_reads/`) —
   isolar o que é do loader da Sega do que é de cada jogo. É o que diz
   ao ODE qual é o contrato, e é leitura de dados, não de código.
3. **O dispatcher da BIOS** — a tabela de syscalls 0–10 do
   [docs/21](21-gdc-syscalls.md) está no bloco `0x8C000000`–`0x8C004000`
   da BIOS, que o `docs/19` §2.2 já tem importado. Mesmo problema de
   acesso-por-tabela, mesma solução: assembly.

O script reutilizável ficou em `/tmp/opencode/sh4test/MmapVolatile.java`,
com a correcção do `/undefined4` e o comentário a explicar porquê.
