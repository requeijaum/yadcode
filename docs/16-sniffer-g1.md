# 16 — Sniffer do barramento G1

Documento de trabalho. Descreve o que o sniffer mede, como está
implementado, e — o mais importante — **quais das perguntas em aberto
só podem ser respondidas com hardware real**.

## 1. Para que existe

O projecto tem sete questões que a documentação não fecha (doc 13 §7).
Cinco delas só se respondem observando um Dreamcast e um GD-ROM reais:

| # | Pergunta | Quem tem a resposta |
|---|----------|---------------------|
| A | O `0x71` responde com 6 bytes ou 1012? | Nenhuma fonte permissiva |
| B | O `0xA1` aborta e devolve os 80 bytes? | idem |
| C | O host real usa DMA? | idem |
| E | O lead-out é sempre 549300? | idem |
| F | O CRC do subcode é XMODEM ou a variante complementada? | idem |

Nada disto se responde lendo o código do Flycast, do iceGDROM ou do
MAME: os três implementam o que escolheram implementar, e nenhum
documenta o comportamento que o hardware faz. O sniffer existe para
medir em vez de adivinhar.

Sem placa, o sniffer é **código revisto e testado, sem silício**. É
entregável como Ferramenta para quem tiver hardware, não como resultado.

## 2. O problema: 33 sinais, 180 ns, FIFO de 8

O bus G1 tem 33 sinais (16 de dados, 3 de endereço, e o resto de
controlo). Um `mov x, pins` da PIO amostra 32 GPIO contíguos de uma
vez — daí o pinout contíguo em GPIO 0..27 — e cada instrução gasta um
ciclo de 4,72 ns a 211,68 MHz.

A soma não fecha para amostragem bruta (`g1_sample`): cada amostra
custa 2 instruções (`mov` + `push`), por isso a FIFO de 8 palavras
enche em 16 ciclos, uns 76 ns. Um só strobe ATA (80 ns no mínimo) já
transborda a FIFO. Não há margem para ver uma transacção inteira,
quanto mais uma transferência.

A alternativa é a amostragem por mudança de estado (`g1_trace`), com
um custo: perdem-se os timestamps absolutos. Só se sabe *que* algo
mudou, não *quando*.

A escolha é `g1_trace`. A ideia é simples: o host está parado a maior
parte do tempo, e enquanto o bus não muda não há nada a registrar. Um
evento por transição é suficiente para reconstruir cada transacção,
e o volume cai em ordens de grandeza face à amostragem contínua.

## 3. O programa PIO

`fw/pio/g1_sniff.pio`, tal como o `pioasm` o monta:

```
0: a040  mov y, pins
1: a0c2  mov isr, y
2: 8020  push
3: a020  mov x, pins
4: 00a6  jmp x != y, 6
5: 0003  jmp 3
6: a0c1  mov isr, x
7: 8020  push
8: a041  mov y, x
9: c000  irq 0
10: 0003  jmp 3
```

O `mov isr, y` do índice 1 não é enfeite: o `push` empurra o ISR,
não o Y, e sem ele o evento inicial seria lixo (ISR = 0). Foi um bug
real, apanhado em revisão (ver §8c).

Lido de cima: guarda o estado em `y`, carrega-o, compara com `x`, e se
mudou salta para a parte que emite. Se não mudou, `jmp 3` volta a
esperar. O `irq 0` acorda a CPU para ir esvaziar a FIFO.

### 3.1 Custos medidos

Dois custos importam, e ambos estão no teste (caso 72):

| Medida | Valor | Porquê importa |
|--------|-------|----------------|
| Instruções por varrimento em repouso | 3 | `mov x, pins` + `jmp x != y` não-tomado + `jmp 3` |
| Tempo por varrimento | 14,17 ns | 3 × 4,7241 ns |
| Amostras na janela do strobe t2 (80 ns) | 5,6 | folga confortável |

Com 5,6 amostras dentro da janela mínima do strobe, uma transição é
detectada com margem. (Uma versão anterior desta tabela dizia 2
instruções e 8,5 amostras: estava errada, porque não contava o `jmp`
de retorno. O caso 72 agora mede o ciclo passo a passo em vez de
repetir a constante.)

## 4. Formato de captura

Texto, deliberadamente. Um ficheiro binário seria mais compacto, mas
quem vai ler isto é uma pessoa a depurar um cabo, e `less` e `grep`
resolvem o caso:

```
# comentário
3f0007000000a0 12      <- 16 dígitos hex, depois o contador
```

Cada linha é o estado dos 28 pinos no instante de uma transição. O
contador é o número de ordem do evento, e serve para confirmar que
não faltou nenhum: se houver um salto, houve perda.

**Nota sobre deduplicação:** não há deduplicação em software, e é
propósito. A PIO já só emite quando o estado muda, e é o único filtro
legítimo. Duas escritas de `0x0000` no registo DATA são transacções
distintas mesmo que produzam o mesmo estado de pinos; um filtro por
igualdade apagaria uma delas, o que num packet de 12 palavras seria
catastrófico (só a primeira e a última são não-nulas).

## 5. Descodificação

`sn_decode()` mapeia o estado dos pinos para o nome do registo,
com a convenção de que os pinos são **activo-baixo** — `/CS0`,
`/CS1`, `/RD` e `/WR` activam a 0:

| Condição | Resultado |
|----------|-----------|
| `/CS1` só, DA = 110, leitura | `ALTSTAT` |
| `/CS1` só, escrita | `DEVCTL` |
| `/CS0` só, DA = 000 | `DATA` |
| `/CS0` só, DA = 001 | `ERROR` na leitura, `FEATURES` na escrita |
| `/CS0` só, DA = 110 | `DRIVESEL` |
| `/CS0` só, DA = 111 | `STATUS` na leitura, `COMMAND` na escrita |
| `/RD` e `/WR` activos ao mesmo tempo | `NONE` (ruído) |

A confusão `ERROR`/`FEATURES` e `STATUS`/`COMMAND` não é nossa: vem da
Tabela 3.1 da spec, onde o registo partilha o endereço com a
direcção da operação. O teste 73 fixa este comportamento.

## 6. Análise: o que se responde e o que não

`sn_analyse()` percorre a captura e responde ao que for possível.
O princípio é não inventar: cada conclusão diz **como** foi medida.

### A — tamanho da resposta ao `0x71`

Mede-se contando as palavras lidas no registo DATA depois do packet.
É o que o host fez, medido, sem depender de o Byte Count ter sido
escrito antes ou depois. Se a resposta foi por DMA não há leituras
DATA nenhuma, e nesse caso o relatório diz exactamente isso em vez de
chutar.

O teste 75 confirma 6 bytes em modo PIO; o teste 78 confirma 1012
bytes em modo DMA, via Byte Count.

### B — abort e tamanho da resposta ao `0xA1`

Procura-se uma escrita do comando `0xA1` (IDENTIFY DEVICE) e a
leitura de `ERROR` que vem a seguir. O bit ABRT (0x04) do `ERROR`
diz se o comando foi abortado; o nibble alto traz a Sense Key, como
define a secção 2.3 da spec (ver `GD_ERR_*` em `gd_spec.h`).
Se não houver leitura de `ERROR`, conta-se quantas palavras DATA
foram lidas até ao comando seguinte — ou até ao fim da captura, se o
`0xA1` for o último comando.

### C — o host usa DMA?

Resposta directa mas com uma guarda: conta-se uma escrita em
`FEATURES` com o bit 0 **cujo comando seguinte seja um PACKET
(0xA0)**. Um SET FEATURES (0xEF) com Features = 0x03 também tem o bit
0 aceso e não é um pedido de DMA. Sem a guarda, qualquer programação
de modo dava um falso positivo. O caso 76 testa os dois lados.

### E e F — lead-out e CRC

**Não respondíveis com este modo.** O lead-out vive nos 408 bytes da
resposta ao `GET_TOC`, e o CRC do subcode nos dados do `GET_SCD`.
Nenhum dos dois cabe num trace de transacções de registo: são
conteúdo de bus de dados, não operações de registo.

O relatório diz isto explicitamente em vez de deixar a pergunta em
branco, e aponta para o modo de captura de dados como o que falta.

## 7. Analisador de PC

`fw/tools/gdsniff.c` transforma uma captura em relatório legível:

```console
$ build/gdsniff captura.txt
=== captura ===
ficheiro:   captura.txt
eventos:    13

=== transaccoes ===
evento dir  registo   dados notas
0      WR   STATUS   0x00a0 inicio de packet
1      WR   DATA     0x7100
...
=== sniffer: o que a captura permite responder ===
[x] 0x71 responde com 6 bytes ou 1012?
      resposta: 6 bytes
      porque:   medido nas palavras DATA lidas depois do packet
```

Cada linha das transacções tem um campo `notas` que decifra o
significado: um `0xA0` vira "inicio de packet", um `0xA1` vira
"IDENTIFY DEVICE", um `FEATURES` com bit 0 vira "DMA pedido".

## 8. O que falta

| Item | Estado | Bloqueio |
|------|--------|----------|
| `g1_trace` codificado e testado | Feito | — |
| `g1_window` com gatilho real | Feito | — |
| Buffer circular com contagem de perdas | Feito | — |
| Descodificação dos 10 registos | Feito | — |
| Análise das perguntas A, B, C | Feito | — |
| Resposta honesta a D e E | Feito | Falta o modo de dados |
| Formato de captura em texto | Feito | — |
| Analisador de PC | Feito | — |
| Detecção de perdas pelo contador | Feito | — |
| Integração no firmware (IRQ + UART) | Feito | — |
| Modo de captura de dados | **Por fazer** | Resolve D e E |
| Validação em silício | **Impossível** | Sem placa |

O modo de captura de dados é o próximo passo natural. Regista o bus DD
em vez dos strobes, o que responde a D e E mas deixa de caber na
memória do RP2350 para uma transferência inteira — daí a janela e o
buffer circular.

## 8b. Integração no firmware

`sniff_start()` e `sniff_poll()` em `src/fw_main.c`. O desenho
importa:

- **A IRQ só marca um flag.** Não lê a FIFO. A FIFO da PIO tem 8
  palavras: demasiado pouco para trabalho sério dentro de uma IRQ, e
  um handler lento atrasa o IRQ seguinte. Toda a leitura acontece em
  `sniff_poll`, no laço principal.
- **`g_fifo_hit` é um flag, não uma contagem.** Cada IRQ conta como
  "há trabalho". Perder um IRQ durante a leitura não perde dados, só
  atrasa o próximo poll — e o comment diz isso, para ninguém "consertar"
  o código para contar.
- **`pio_get_irq_num(pio, 0)`, não `0`.** O SDK mapeia (pio, irq) para
  um número de IRQ global que depende da variante. Passar `0` a olho
  seria frágil.
- **A PIO do sniffer usa uma SM separada** da do reader. Cada PIO tem
  4 SMs; o `main` só reclamou uma para o reader.
- **Sem `pio_set_irq0_source_enabled`, nada dispara.** O `irq 0` do
  programa põe o flag 0 da PIO, mas a saída IRQ0 para o NVIC vem
  desligada por omissão. E sem `pio_interrupt_clear` no handler, a
  saída continuava asserted e o handler reentrava em ciclo.

O firmware com o sniffer ligado ocupa 41984 bytes em .uf2 (medido nesta revisao; varia com o SDK).

## 8c. Correcções aplicadas durante a revisão

Vale a pena registar porque foram erros reais, não de estilo:

1. **`g1_window` era cópia byte a byte de `g1_sample.** O nome
   prometia uma janela armada e o programa empurrava eventos sem
   gatilho. Agora tem `wait 1 pin 28` e bloqueia até o host abrir a
   janela. O teste 81 fixa a diferença.
2. **O teste 72 validava uma constante escrita à mão.** Imprimia
   9,45 ns e depois fazia `CHECK(2 * CYCLE_NS < 80.0)` — a medição
   não entrava no check. Agora conta os ciclos de relógio de verdade e
   verifica que o programa gasta mesmo 2 instruções por ciclo.
3. **O firmware imprimia o mesmo valor duas vezes** em vez do estado
   dos pinos, o que produzia capturas que o `gdsniff` não conseguia
   ler. Era `w[i] >> 16` que faltava.
4. **O `gdsniff` ignorava o contador** que a doc diz servir para
    detectar perdas. Agora conta os saltos e avisa.
5. **O `push` inicial do `g1_trace` empurrava lixo.** O `push`
    empurra o ISR, não o Y, e o programa não tinha `mov isr, y`:
    o primeiro evento da FIFO era sempre 0. Agora tem, e o caso 71
    lê a FIFO do VM em vez de adivinhar o conteúdo.
6. **O varrimento em repouso são 3 instruções, não 2.** A tabela
    dizia 9,45 ns e 8,5 amostras porque não contava o `jmp` de
    retorno. São 14,17 ns e 5,6 amostras — continua a caber na
    janela de 80 ns, mas o número anterior estava errado.
7. **A IRQ do sniffer nunca disparava.** Faltavam o
    `pio_set_irq0_source_enabled` (o `irq 0` põe o flag mas a saída
    para o NVIC vem desligada) e o `pio_interrupt_clear` no handler
    (sem ele, reentrada em ciclo). Também se corrigiu o
    `get_default_config`, que recebia o número da SM em vez do
    offset do programa.
8. **A análise media para além do packet.** O `response_bytes`
    contava leituras DATA até ao fim da captura, por isso as
    leituras de um packet seguinte contaminavam a medida. Agora a
    janela fecha no COMMAND seguinte, e o DMA só conta para
    FEATURES cujo comando seguinte seja PACKET.

## 9. Testes

`fw/tests/test_sniffer.c`, doze casos:

| Caso | O que fixa |
|------|-----------|
| 70 | As palavras do `g1_trace` são as que o `pioasm` produz |
| 71 | A PIO só emite quando o estado muda; em repouso, silêncio |
| 72 | O varrimento cabe na janela do strobe t2 |
| 73 | A descodificação dos pinos em cada registo, e a confusão ERROR/FEATURES |
| 74 | O ring não perde nada até encher, e conta o que perde |
| 75 | A análise responde A e C numa captura PIO |
| 76 | A análise detecta um pedido de DMA |
| 77 | Uma captura vazia não inventa respostas |
| 78 | Resposta de 1012 bytes em modo DMA |
| 79 | Uma captura só de registos não inventa um lead-out |
| 80 | `g1_window` bloqueia até ao gatilho, e volta a bloquear quando fecha |
| 81 | `g1_window` é `g1_sample` precedido de um `wait` |

Os casos 70 e 72 são os que importam para a confiança no resto: se as
palavras não forem as do `pioasm`, e se o varrimento não couber na
janela do strobe, o resto das medições não significa nada.

## 10. Lições sobre o PIO VM

Ao estender `fw/tools/pio_vm.c` para suportar `mov` e `jmp`
condicional, apareceram três erros de encoding:

- `nop` **não** tem opcode próprio. É literalmente `mov y, y`
  (0xa042), e `nop [n]` é o mesmo com o campo de shift preenchido
  (0xa342 para 3, 0xa542 para 5). O critério é destino = Y e
  fonte = Y.
- Na fonte de um `mov`, o valor **0 é `pins`**, não `null`. Deduzido
  de `mov x, pins` = 0xa020, cujo campo de fonte é 000.
- O campo `op` de um `mov` são os bits 4..3. Ler o bit 0 — que
  pertence à fonte — faz `mov x, y` (0xa022) parecer uma operação
  especial e o VM parar.

Todos confirmados com `pioasm -o hex -v 1`, não de memória.
