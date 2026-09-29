# 06b — Scope: PIO-only ou PIO + DMA?

> **ESTE DOCUMENTO É UMA QUESTÃO ABERTA, NÃO UMA DECISÃO.**
> A auditoria a documentos da Sega encontrou evidência **contra** a hipótese inicial
> ("PIO-only chega"). Ver §2. A resolução exige **medição num GD-ROM real**.

---

## 1. A hipótese inicial

Comecei por assumir que um ODE v1 podia implementar **só PIO com IORDY**, dispensando
`DMARQ`/`DMACK`. O raciocínio era:

- O **bit 0 de Features** (`0x005F7084`) é o selector DMA/PIO, e é escrito pelo **host**.
- O driver Linux tem de o setar explicitamente para usar DMA:
  ```c
  #define GDROM_ERROR_REG (GDROM_BASE_REG + 0x84)   // == 0xA05F7084 == GD_FEATURES
  ...
  __raw_writeb(1, GDROM_ERROR_REG);   /* set for DMA */
  ```
  Se o caminho normal fosse DMA, não precisaria de o activar.
- O Flycast escolhe o estado por esse bit:
  ```c
  if (Features.CDRead.DMA == 1) { gd_set_state(gds_readsector_dma); }
  else                         { gd_set_state(gds_readsector_pio); }
  ```
- A Sega descreve DMA como **opcional**: *"DMA transfers are **possible** in the GD-ROM
  area"*.
- O Dreamdrive tem o DMA **inacabado**, e o que funciona é o PIO.

**Se isto estiver certo, o âmbito reduz-se substancialmente** e o item mais instável do
projeto desaparece.

## 2. A evidência contrária

⚠️ **Sega, *Dreamcast Dev. Box System Architecture*, §2.6.3 "GD-ROM Data Transfers", passo (2):**

> "SB_G1GDRC (0x005F74A0) register setting — Set the access wait value when reading by a DMA.
> **Write 0x00001001, which is equivalent to "Multi Word-DMA Mode 2."**"

Isto é o documento que descreve **o que o host faz a sério**, passo a passo, para as
transferências de dados do GD-ROM. E diz: **MWDMA mode 2.**

Corroborado por *Hardware Specification Outline* §6.1, que dá *"Approx. 13.3 MB/s (Multi
word DMA Mode2)"* contra *"From the buffer approx. 11.1 MB/s (PIO Mode3)"*.

E o *Hardware Specification Outline* §3.8 diz *"The bus operation is **not synchronised**"* —
o que é estranho para um modo assíncrono com IORDY, e mais parecido com um barramento de
transferência DMA.

### 2.1 Estado da questão

| Evidência | Aponta para |
|---|---|
| Dev.Box §2.6.3: host escreve `0x00001001` = MWDMA-2 | **DMA** |
| HW Outline §3.8: bus "not synchronised" | **DMA** |
| HW Outline §6.1: 13.3 MB/s (MWDMA2) vs 11.1 MB/s (PIO3) | ambos, DMA mais rápido |
| Linux `gdrom_readdisk_dma()` tem de setar bit 0 explicitamente | PIO por omissão |
| Sega: *"DMA transfers are possible"* (condicional) | DMA opcional |
| Dreamdrive: DMA inacabado, PIO funciona | PIO suficiente? |
| GDEMU (produto comercial, todas as BIOS de retail) — alegadamente sem DMA | PIO suficiente |

⚠️ **Nenhuma destas é conclusiva.** A hipótese mais forte agora é: **o host usa MWDMA-2, e o
PIO-only funciona apenas porque o GD drive real degrada graciosamente quando o host
solicita DMA** — ou porque a alegação sobre o GDEMU está errada.

**Não se deve assumir PIO-only.** Ver §4.

## 3. O que o modo muda tecnicamente

| | PIO | DMA |
|---|---|---|
| Quem gera o clock | o host (`/RD`) | **o device** (`/DMARQ` + strobe) |
| Pinos | IORDY | IORDY, DMARQ, DMACK |
| t0 | 180 ns (PIO-3) | 120 ns (MWDMA-2) |
| Setup dados | `t5` ≥ 20 ns | `tG` ≥ 20 ns, `tE` ≤ 50 ns (max) |
| Strobe | `t2` ≥ 80 ns | `tD` ≥ 70 ns |
| Pré-carregamento | não | **sim, obrigatório** |
| Risco no Dreamdrive | baixo | **alto** — ver §3.1 |

⚠️ `tB` (IORDY pulse width max = 1250 ns) aplica-se a PIO. Em DMA não há IORDY, o que
**elimina** a restrição mais apertada do modo PIO mas introduz a exigência de `tE` ≤ 50 ns
(ver [03 §4](03-timing-ide.md)) e de `tLr` ≤ 35 ns para assertar `DMARQ` a tempo.

### 3.1 O `dma_bus_handler` do Dreamdrive está errado

```pio
transfer_data:
    wait 0 gpio 21 side 0     ;; strobe em RD (PIO faz o clock do host!)
    out pins, 16 side 1
    pull
    wait 1 gpio 21
    jmp x-- transfer_data
```

O PIO **gera o clock no pino `/RD` do host**. Em DMA o host não gera o clock — o device
tem de assertar `DMARQ`, o host responde com `DMACK` e os dados fluem a `tD` após cada
acknowledge. O autor nunca chegou a terminar isto (o commit diz *"Currently after the 5th
dma finishes, the ata bus handler loop doesn't ever restart"*).

**Isto não é reutilizável tal como está.** Requer reescrita.

## 4. Como resolver — experiência concreta

⚠️ **Não decidir por inferência. Medir.**

### 4.1 Experiência A: sniffer num GD-ROM real (a resposta definitiva)

Instrumentar o G1 de um Dreamcast com o GD original, capturing para SD:

1. Todos os sinais do G1: `DD0-15`, `DA0-2`, `/CS0`, `/CS1`, `/RD`, `/WR`, `IORDY`,
   `INTRQ`, `DMARQ`, `/DMACK`, `RESET`.
2. Guardar **todo o tráfego de registos**, com timestamps, durante um boot completo +
  arranque de um jogo.
3. Perguntas a responder:
   - O host escreve alguma vez **Features bit 0 = 1**? (log em `0x5F7084` writes)
   - Se sim, `DMARQ` é alguma vez asserted pelo drive?
   - O host assenta `SB_G1GDRC = 0x00001001`? (leitura do registo HOLLY `0x005F74A0`)
   - Qual é o `t0` real observado (tempo entre accesses de 32 B)?

O sniffer pode ser feito com o próprio RP2350 num PIO `rx` a 1 Nyquist (ver
[11-estrategia-de-validacao.md](11-estrategia-de-validacao.md)) ou com um analisador
lógico externo.

### 4.2 Experiência B: PIO-only e ver o que falha

Implementar PIO-only e testar com um jogo real. Se o `DMARQ` nunca for observado, a hipótese
PIO-only está confirmada para esse jogo — **mas não para todos**, e o
*Dev.Box* sugere que o caminho existe.

### 4.3 Experiência C: procurar firmware do BIOS

O BIOS do Dreamcast é dumpável. Procurar writes a `0x005F7084` com valor 1 nas rotinas de
leitura de CD. Resolve a questão sem hardware.

## 5. Recomendação

| Fase | Decisão |
|---|---|
| **Fase 0** (estudo) | Deixar em aberto. Levantar a questão com §4.1. |
| **Fase 1** (PIO) | Implementar PIO-3 + IORDY. É obrigatório de qualquer forma. |
| **Fase 2** | Medir com o sniffer. Se PIO-only confirmar, ficar por aí. |
| **Fase 3** (DMA) | Só se a medição mostrar que é necessário. Reescrever `dma_bus_handler` de raiz. |

**Mesmo que PIO-only chegue, implementar DMA continua a ser a decisão correcta**, porque:
1. O *Dev.Box* documenta que o host configura MWDMA-2.
2. 13.3 MB/s vs 11.1 MB/s é 20% de banda para o boot.
3. `DMARQ`/`DMACK` são 2 pinos, e já estão no pinout do Dreamdrive.

**Mas não fazer DMA antes de PIO.** O caminho de erro é gastar semanas em DMA e
descobrir que o problema era o PIO.

## 6. Nota sobre o `0x00001001`

🔶 Vale registar o valor exato: `SB_G1GDRC = 0x005F74A0` (offset `+0xA0` no bloco de
controlo G1, ver [02 §3](02-barramento-g1-pinout.md)), escrito com `0x00001001`,
"equivalent to Multi Word-DMA Mode 2". Isto é consistente com o layout de um registo de
timing G1: os bits altos dão o número de waits, os baixos o modo.

Evidência nova (2026-09-28): a BIOS de retail **não toca** neste registo —
`0x005F74A0` ocorre **zero vezes** no dump, contra 3402 constantes no P4/Holly
([19 §2.3](19-kos-e-reversao-da-bios.md)). E o KallistiOS, que usa DMA por
omissão, impõe **alinhamento de 32 bytes para DMA** contra 16 para PIO.

Isto não fecha a questão C, mas reforça PIO como baseline do v1. E gera uma
previsão testável: um programa compilado com KOS usaria DMA e não arrancaria
num ODE só-PIO ([19 §3.3](19-kos-e-reversao-da-bios.md)).

---

Ver também: [01-protocolo-spi-sega](01-protocolo-spi-sega.md) ·
[03-timing-ide](03-timing-ide.md) · [05-analise-dreamdrive](05-analise-dreamdrive.md) ·
[11-estrategia-de-validacao](11-estrategia-de-validacao.md)
