# 24 — Engenharia reversa do GD-ROM: onde está e como se repete

> Documento de trabalho, 2026-09-29. Escrito para quem (ou que LLM) queira
> retomar a análise sem repetir os erros que custaram a sessão. As conclusões
> estão nos documentos 19 a 22; **aqui está o método**.

## 1. Onde está cada coisa

**As conclusões estão no repositório. O trabalho, não — estava em `/tmp`.**

| | Onde | Sobrevive a reboot? |
|---|---|---|
| Conclusões (docs 19–22) | `yadcode/docs/` | ✅ versionado |
| Tabela CRC + algoritmo | `fw/src/gd_crctbl.c`, `gd_crc.c` | ✅ versionado |
| Testes (casos 97–101) | `fw/tests/test_crc.c` | ✅ versionado |
| Projectos Ghidra (6, 65 MB) | `~/backups/yadcode/re-ghidra/` | ✅ copiado |
| Scripts Ghidra (15 `.java`) | `~/backups/yadcode/re-ghidra/scripts/` | ✅ copiado |
| `pull_ip.py` (extractor) | `~/backups/yadcode/re-ghidra/tools/` | ✅ copiado |
| `1ST_READ.BIN` × 6 (25 MB) | `/tmp/opencode/1st_reads/` | ❌ **md5 no manifesto** |
| Ghidra 12.1 | `~/projects/dkwdrv_hacking/ghidra_install/` | ✅ |
| Biblioteca Sega (`shinobi.elf.lib`) | `~/projects/katana-sdk-lab/` | ✅ |
| Dump da BIOS | `~/RetroArch/system/dc/dc_boot.bin` | ✅ |
| Wiki do SDK | `~/projects/katana-sdk-lab/wiki/` | ⚠️ herda erros meus |

Os projectos estão **fora do repo** de propósito: contêm binários de
jogos de retail e código da Sega, e o `yadcode` é Apache-2.0. Mesma
política do `ref/`.

`~/backups/yadcode/re-ghidra/MANIFESTO.md` diz o que é cada projecto e
como reconstruir. Nenhuma conclusão depende dos projectos sobreviverem —
recalculam-se a partir dos binários de origem.

## 2. Subir o Ghidra com SH4

**Não há passo de instalação.** O Ghidra 12.1 já traz o módulo
`SuperH4:LE:32:default` compilado (`SuperH4_le.sla`) e descompila SH4 sem
nenhuma extensão. 2193 de 2194 funções no `1ST_READ.BIN`.

```sh
GH=~/projects/dkwdrv_hacking/ghidra_install/ghidra_12.1_PUBLIC

# A BIOS. O --loader-baseAddr é obrigatório (ver armadilha 1)
"$GH/support/analyzeHeadless" proj bios \
  -import ~/RetroArch/system/dc/dc_boot.bin \
  -loader BinaryLoader -processor "SuperH4:LE:32:default" \
  -loader-baseAddr 0x8C000000

# O 1ST_READ.BIN: o driver, a 0x8C008000
"$GH/support/analyzeHeadless" proj ip \
  -import /caminho/1st_read.bin \
  -loader BinaryLoader -processor "SuperH4:LE:32:default" \
  -loader-baseAddr 0x8C008000
```

Tempos: 15 s a 53 s, conforme o programa. Para ver o C interactivamente,
`"$GH/ghidraRun"` e abrir o projecto.

Validar o que se fez:

```sh
"$GH/support/analyzeHeadless" proj bios -process dc_boot.bin -noanalysis \
  -scriptPath scripts/ -postScript IpStats.java
```

`IpStats.java` imprime funções, cobertura e taxa de descompilação.

## 3. As seis armadilhas

Esta secção vale mais que os comandos. Todas custaram tempo real.

### 1. `-loader-baseAddr` é obrigatório

Sem ele, o Ghidra usa `0x00000000` e obtém-se **1,5% de cobertura** em vez
de 13,9%, e 339 funções em vez de 1717. O resultado parece um binário
ofuscado ou um problema de formato, e não é.

O sinal de que o endereço está errado: o vector de reset é um `BRA` que se
auto-aponta. Buscar ponteiros absolutos para `0x8C00xxxx` no dump confirma
que a base é `0x8C000000`.

### 2. `objdump -s` mostra os dados em endianness trocada

Em objectos CodeWarrior (`shinobi.elf.lib`), o `sh4-linux-gnu-objdump -s`
imprimiu `0x2110, 0x4220, 0x6330…` quando os valores reais são
`0x1021, 0x2042, 0x3063…`.

**Consequência grave:** os valores errados não correspondem a nenhum
polinómio de 16 bits, testei todos os 65536, e escrevi no `docs/20` que o
GD-ROM "não usa um polinómio convencional". Era o `0x1021` o tempo todo.

Extrair secções pelo **ELF32**, nunca pelo `objdump -s`:

```python
import struct
d = open("obj.elf", "rb").read()
shoff = struct.unpack_from("<I", d, 0x20)[0]      # e_shoff
shent  = struct.unpack_from("<H", d, 0x2E)[0]      # e_shentsize
o = shoff + 6 * shent                                # secção 6
name, typ, flags, addr, off, size = struct.unpack_from("<IIIIII", d, o)
corpo = d[off:off + size]
```

### 3. `objdump` desalinha a secção `PSG` no offset `0x10`

Imprime `.word 0x403c` onde há instrução, e a rotina seguinte é lida a
partir do sítio errado. É exactamente onde a máscara do byte é carregada, e
portanto onde a indexação é decidida.

Desensamblar com o **Ghidra**, que é fidedigno neste SH4. Ou validar a
sequência à mão, alinhada a palavras de 2 bytes.

### 4. A SuperH4 não tem `DWORD`

`dtm.getDataType("/unsigned_dword")` devolve `null`. Todos estes devolvem
`null`: `/DWORD`, `/uint`, `/ulong`, `/uint32_t`, `/dword`. O único tipo de
4 bytes é **`/undefined4`**.

E `createData(addr, null)` **não dá erro: não faz nada, em silêncio**. É
por isso que "criei o rótulo mas o nome não aparece no C" persistia: o
símbolo estava lá, a tipagem nunca aconteceu.

```java
DataType u4 = dtm.getDataType("/undefined4");
new CreateDataCmd(addr, true, u4).applyTo(currentProgram);
```

### 5. Procurar constantes alinhadas a 4 bytes

`bytes.find(struct.pack("<I", 0x8C000080))` encontra qualquer padrão de
bytes alinhado ou não, e produz falsos positivos em código. Das contagens
de registos G1 no `1ST_READ.BIN`:

| Registo | Por byte-pattern | Alinhado a 4 (real) |
|---|---|---|
| `G1RAM` `0x8C000000` | 19 | **1** |
| `G1GDRD` `0x8C000080` | 1 | **0** |
| `G1TBAL` `0x8C0000E0` | 3 | **1** |

O único "G1GDRD" era `80 00 00 8c` **desalinhado** dentro de outra
constante. Confirmar sempre por alinhamento, e por contexto.

### 6. `volatile` + tipo não resolvem o MMIO — e não são o problema

Aplicado correctamente (rótulos criados, bloco `setVolatile(true)`,
dados tipados), o decompiler **ainda emite 0 funções** com os nomes dos
registos.

A causa é mais simples do que parece: **o loader não toca nos registos G1
de todo**. Vai por syscalls da BIOS, como o SDK da Sega, como o KOS e
como a própria BIOS. Não há "acesso por tabela" nem acesso directo.

**Não gastar tempo em MMIO.** Ver [22](22-porque-os-rotulos-nao-chegam-ao-c.md).

## 4. O que não funciona, para não repetir

| Tentativa | Resultado |
|---|---|
| `ar x` do GNU em objectos CodeWarrior | *"erro interno estático"* — usar o extractor manual do formato `ar` |
| `createDWord()` do `dc-re-ghidra` | tipo inexistente na SuperH4 (armadilha 4) |
| Rótulos + volatile + tipagem | 0 resultados (armadilha 6) |
| Cinco indexações de CRC com a tabela errada | todas falharam; com a tabela certa, a **primeira** era a boa |
| Procurar `0x005F74A0` para fechar C | **zero** ocorrências na BIOS de retail — é essa a evidência |

## 5. Extrair um ficheiro de um GD-ROM de retail

`tools/pull_ip.py` (no backup). Os passos e a armadilha:

```sh
D="<pasta com disc.gdi e track*.bin>"
GDI_ROOT="$D" python3 pull_ip.py "$D/disc.gdi" 1ST_READ.BIN /tmp/1st_read.bin
```

**A conversão é `FAD = LBA + 150`, e o sector pedido pode cair noutra
track que não a que se está a ler.** Errar aqui manifesta-se como "o
ficheiro não existe", que é enganoso: o conteúdo está lá, noutro sítio.
Errei nesta conversão três vezes antes de a acertar.

## 6. Procedência — não confundir

| Fonte | O que é | Licença |
|---|---|---|
| Ghidra | ferramenta | Apache-2.0 |
| `shinobi.elf.lib` | biblioteca da **Sega**, 1999-11-09 | **proprietária** |
| `1ST_READ.BIN` | binários de **jogos de retail** | **proprietários** |
| `dc_boot.bin` | BIOS de retail, v1.01d VA1 Japão | **proprietária** |

Conclusão no `yadcode` = **comportamento e constantes, com citação**.
Nunca transcrever código para `fw/`. A tabela de CRC em
`fw/src/gd_crctbl.c` é a única coisa extraída, e está lá com a origem no
header e a política de não ligar ao firmware antes de medir.

## 7. Referências internas

- [19](19-kos-e-reversao-da-bios.md) — BIOS, KOS, onde o driver **não** está
- [20](20-shinobi-crc-subcode.md) — o CRC do subcode, fechado
- [21](21-gdc-syscalls.md) — os 11 syscalls GDC
- [22](22-porque-os-rotulos-nao-chegam-ao-c.md) — o MMIO, e porque falhou
- [16 §8](16-sniffer-g1.md) — o que só o hardware resolve
- `~/backups/yadcode/re-ghidra/MANIFESTO.md` — o que é cada projecto
