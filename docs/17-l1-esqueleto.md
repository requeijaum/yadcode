# 17 — L1: esqueleto device-side, o que se inferiu e o que falta

Documento de trabalho. Descreve o esqueleto da camada L1
(`fw/src/gd_l1.*`, `fw/pio/g1_dev.pio`, casos 90–96), **o que foi
inferido dos documentos, o que foi chutado, e que impacto cada
lacuna tem no resto do projecto**. A convenção de tags
`[DOC-xx]` / `[INFERRED]` / `[UNKNOWN]` / `[TODO-L1]` está definida
em `fw/src/gd_l1.h` e é usada em todo o código do L1.

## 1. Onde vive o L1

```
fw/src/gd_l1.h/.c    decode (Tabela 3.1) + despacho + gd_hal_rp2350.
                     Puro: compila e testa no PC.
fw/src/gd_l1_hw.c    GPIO + PIO + IRQ do RP2350. Só no firmware.
fw/pio/g1_dev.pio    g1_dev_slow: uma selecção = um evento.
fw/tests/test_l1.c   casos 90–96: tabela, despacho, PIO, pinout, reset.
```

A costura é o `gd_hal_t` (`fw/src/gd_hal.h`): o núcleo L2/L3 chama a
HAL sem saber se está num RP2350 ou num PC, e o L1 é a implementação
RP2350 dessa HAL. A regra de dependência da arquitetura mantém-se
[doc 08 §1]: L3 não sabe o que é um strobe, L1 não sabe o que é um
packet.

## 2. O que está inferido (e de onde)

| Peça | Fonte | Estado |
|------|-------|--------|
| Decode CS0/CS1/DA + direcção, 12 linhas | [DOC-01 Tabela 3.1] | Facto. Teste 90 fixa linha a linha. |
| Larguras 16/8 | [DOC-01 §2.1] | Facto. Teste 91. |
| Status-lê-limpa-INTRQ, AltStatus-não | [DOC-01 §3.4], [DOC-13] | Facto. Teste 93. |
| Mapeamento ERROR/FEATURES, STATUS/COMMAND | [DOC-01 Tabela 3.1] | Facto. Teste 92. |
| Packet: 6 palavras → executa sozinho | [DOC-01 §7.1 passo 5] | Facto (herdado do L2). Teste 92. |
| Reset = taskfile reset + saídas em repouso | [DOC-01 §3.3.1.1] | Facto. Teste 96. |
| Pinos 0..27 do bus | [DOC-02] + sniffer | Facto. Teste 95. |
| IORDY-baixo = wait, INTRQ-alto = IRQ | ATA (convenção) | [INFERRED]. Por confirmar no scope. |
| Slow-path chega para o bring-up passo 2 | [DOC-08 §8] + análise | [INFERRED]. Ver §3. |
| INTRQ = GPIO 28 | nada | Placeholder. Ver §4. |

Nada acima foi copiado de firmware alheio: o Dreamdrive é citado
como referência de ideias, nunca de código [doc 05, doc 09].

## 3. A decisão estrutural: slow-path deliberado

O programa `g1_dev_slow` espera `/CS0`, amostra, empurra, acorda a
CPU — e a descodificação vive em C. Isto **viola t5 (20 ns) e t6
(5 ns)** por construção: ir a C e voltar custa microssegundos.

É deliberado, por três motivos:

1. **O passo 2 do bring-up não precisa de tempo real.** Com um host
   lento (ou strobes manuais) e um analisador lógico nos pinos, o
   slow-path prova cablagem + decode + despacho — que é tudo o que o
   passo 2 pede [doc 08 §8].
2. **O fast-path exige decisões que só o hardware fecha**
   (`.origin` livre, turnaround do bus, IORDY). Escrevê-lo agora seria
   fingir.
3. **O desenho do fast-path está registado**, não perdido: a
   NOTA-FAST-PATH no `.pio` descreve a jump table por `mov exec, y`
   (16 entradas: `wr<<3 | da`), com os pré-requisitos listados.

Alternativas rejeitadas e porquê:

- *Descodificar na PIO já (exec-table completa)*: rejeitada porque o
  `.origin`, o turnaround e o IORDY são [UNKNOWN]; acertar tudo à
  primeira sem scope é lotaria.
- *Uma SM por registo*: rejeitada porque há 12 registos e 4 SMs.
- *`jmp pin` para a direcção*: rejeitada porque o pino testado vem da
  configuração da SM, não do campo — e o VM nem o modela (limitação
  documentada em `pio_vm.c`).

## 4. Os UNKNOWNs, o que bloqueiam e como se resolvem

| # | Desconhecido | Bloqueia | Resolve-se com |
|---|--------------|----------|----------------|
| U1 | Turnaround do bus (quando conduzir DD) | Resposta eléctrica a leituras | Scope: medir RD↓→dados e dados→RD↑ contra t5/t6 |
| U2 | IORDY é preciso? Com que polaridade? | Espera inserida pelo device | Analisador: ver se o host tolera resposta lenta sem IORDY |
| U3 | O host larga /CS entre acessos? | O `wait 1` de rearme (se não, FIFO inunda) | Analisador: dois acessos colados |
| U4 | Pinos de placa (INTRQ 28 é placeholder) | PCB | Esquemático; trocar o define |
| U5 | DMA real? (questão C) | Toda a maquinaria DMARQ/DMACK + DATA SM | Sniffer num drive real [doc 06b] |
| U6 | `.origin` livre para a jump table | Fast-path | Mapa de SMs na placa final |
| U7 | nIEN hi-Z no GPIO | Fidelidade do INTRQ | Scope: INTRQ com nIEN=1 |

Guia de correcção (esforço mínimo, como pedido):

- Cada UNKNOWN tem tag no código exacto onde mora (`rg UNKNOWN fw/src/gd_l1* fw/pio/g1_dev.pio`).
- Corrigir é: medir, trocar o valor/comportamento, correr `make test`.
  Os casos 90–96 acusam regressões no que já está fixo.
- Se U3 responder "não larga": tirar o `wait 1 pin 19` do `.pio`,
  e a dedup passa para o C (o teste 94 tem de ser actualizado —
  está comentado lá).

## 5. Impactos no resto do projecto

### 5.1 Orçamento de SMs (apertado, mas cabe)

Cada PIO tem 4 SMs. Contabilidade no `pio0`:

| Consumidor | SMs |
|------------|-----|
| `g1_read` (timing/loopback) | 1 |
| `g1_trace` (sniffer) | 1 |
| `g1_dev_slow` (L1, bloco CS0) | 1 |
| 2ª SM L1 para o bloco CS1 [TODO-L1] | 1 |
| **Total** | **4 de 4** |

[INFERRED] Cabe à justa no `pio0`. Se aparecer mais alguma SM
(DATA SM do fast-path, MCK do clock), transborda para `pio1`/`pio2`
— o RP2350 tem três blocos. Quem ligar o L1 no `fw_main` tem de
rever esta tabela; hoje o `main` só usa 2 (reader + sniffer).

### 5.2 Firmware: +0 bytes até ser ligado

O esqueleto compila no firmware mas o `fw_main` não o chama, e o
linker descarta o não-referenciado: o `.uf2` continua com 41984
bytes. Quando se ligar, medir de novo — o caminho crítico (ISR +
despacho) tem de caber em `__not_in_flash_func` se for para o core1
[doc 08 §2]. [TODO-L1].

### 5.3 Se a questão C responder SIM (DMA)

O esqueleto assume PIO (Fase 1 do doc 06b, obrigatória de qualquer
forma). Se o host usar MWDMA-2, muda:

- Pinos DMARQ (25, saída) e /DMACK (26, entrada) passam a vivos; hoje
  são entradas inertes por segurança.
- Precisa de DATA SM dedicada + `tD ≥ 70 ns` em vez de t5/t6.
- O `hw_set_dmarq` existe como stub para esse dia.
- Nada no L2/L3 muda: a HAL esconde o transporte [doc 08 §1].

### 5.4 Testes: 504 checks, sete suites

Os casos 90–96 são a rede de segurança do L1: tabela (90), larguras
(91), despacho de escritas incluído packet completo (92), leituras +
clear do INTRQ (93), PIO monta e emite (94), pinout partilhado (95),
reset (96). O que é [UNKNOWN] não tem teste que finja — tem teste
que acusa se o fixo mexer.

### 5.5 O que NÃO mudou

- L2/L3/L4/L5: intocados. O L1 só chama API pública existente.
- Sniffer: intocado; partilha os pinos do bus por definição.
- Licença: Apache-2.0; continua sem código de terceiros no repo.
