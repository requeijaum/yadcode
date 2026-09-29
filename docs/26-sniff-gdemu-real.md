# 26 — Fase C: sniff de um GDEMU funcional, e os dois requisitos de arranque

Data: 2026-09-29. Console do utilizador: **VA1**. Placa da sniffer: **ainda não existe**.

> **Estado.** A sniffer continua a ser software sem silício. Este documento tem duas
> partes: o que **se fez sem hardware** (os dois requisitos de arranque, já no
> firmware) e o que **falta medir** quando a placa existir. As respostas de
> [25](25-opengdemu-comportamento.md) transformaram o que se procura na sniffer —
> duas das perguntas estavam mal feitas.

---

## 1. O que mudou no firmware por causa do OpenGDEMU

### 1.1 IDENTIFY: 64 → 512 bytes

A ATA define `IDENTIFY PACKET DEVICE` como **256 palavras**. O firmware devolvia 64.
Isso não era decorativo: se a BIOS consultasse uma palavra acima da 31, lia lixo.

| | Antes | Agora |
|---|---|---|
| Resposta ao `0xA1` | 64 B | **512 B** (`GD_IDENTIFY_SIZE`) |
| Buffer partilhada | `GD_TOC_SIZE` (408) | `GD_SPI_BUF_SIZE` (512) |
| `0xEC` | caía no `default:` e abortava | igual — **e agora está testado** |

O `GD_TOC_SIZE` continua 408: o GET_TOC anuncia 408, como antes. O que mudou foi a
capacidade da buffer, que tem de ser ≥ o maior payload. São agora duas constantes
distintas em vez de uma—a anterior era uma coincidência que ia estalar.

> **Por que 512 e não 64.** É superseguro: a BIOS lê o Byte Count e decide quantas
> palavras quer. Mandar 512 nunca é pior que mandar 64; mandar 64 pode ser fatal se
> ela ler além. A incerteza esta em *quantas* a BIOS lê, não em se 512 chega.

**Teste:** `test_spec.c` caso *"IDENTIFY: 512 bytes, o tamanho ATAPI"*, que conta as
palavras efectivamente servidas e valida o Byte Count em `0x0200` little-endian.

### 1.2 O `0xEC` deixa de ser um acidente

O abort de `0xEC` vinha do `default:` — certo por acidente. Passou a ter um teste
próprio, porque é **condição de arranque**: sem ele a BIOS não encontra o disco.

### 1.3 CD_READ: pré-buffer de 14 KB

Este era o problema real, e estava no código antes de a sniffer o revelar.

**Antes**, `src_cdread_read` servia **sector a sector, do disco, enquanto o host
puxava**. O `sec_cache` tinha um sector. Isso põe a latência do meio de
armazenamento (SD, flash) **dentro do caminho do DMA do host** — que morre aos
~10 KB ([25](25-opengdemu-comportamento.md) §5), e o arranque precisa de 14 KB.

**Agora**, se o pedido couber em `GD_CDREAD_BUF_BYTES` (8 sectores × 2352 =
**18816 B**), o `src_cdread_open` lê o pedido **inteiro** antes de o host poder puxar
uma palavra. Acima do limite, cai no caminho antigo.

**Teste:** `test_spec.c` caso *"CD_READ: os sectores ficam prontos ANTES de o host
puxar"*. Não testa o conteúdo — testa a **propriedade**: uma única chamada de leitura
no comando, e **zero** leituras de disco enquanto o host puxa os 14 KB. É a
invariant que impede a regressão.

```c
CHECK(after_cmd == before + 1, "leitura em bloco, nao sector a sector");
...
CHECK(md.read_count == after_cmd,
      "o host leu sectores do disco durante a transferencia; "
      "a latencia esta no caminho do DMA da BIOS");
```

### 1.4 Um bug latente que o pré-buffer expôs

`sec_fad` era a chave de cache de sector e **não incluía o `sector_size`**. Um
`CD_READ` de 2352 B servia os 2048 B do comando anterior, sem tocar no disco.

Isto não era teórico: o caso de teste *"CD_READ so produz 2048, 2340 ou 2352"* passava
**pelo motivo errado** — o `sec_fad` estático ainda apontava para o FAD do comando
anterior, então o `if` era falso e servia cache velho. O teste estava a medir o
comportamento errado desde que foi escrito.

Corrigido com `sec_size` na chave, e o `memdisc` passou a servir 2340/2352 (o fixture
recusava tudo o que não fosse 2048, o que tornava o teste impossível de satisfazer
honestamente).

⚠️ **Isto é o padrão de erro que já cometi três vezes neste projecto:** uma
afirmação forte, revista porque algo não batia certo, e o "algo" era um teste que
passava por acidente. O pré-buffer não foi escrito para arranjar isto — foi
acidentalmente exposto. Vale a pena procurar os outros.

---

## 2. O que a sniffer tem de procurar, corrigido

Em [16](16-sniffer-g1.md) §6 as perguntas estavam mal formuladas. Com o OpenGDEMU,
sabemos o que perguntar:

| Pergunta original | O que se sabe agora | O que a sniffer faz |
|---|---|---|
| **A** `0x71` → 6 B ou 1012? | 6 B é o mínimo; o resto é desconhecido | Conta as palavras lidas depois de `0x71` e compara com o Byte Count. Se o host ler 1012, o Flycast está certo |
| ~~**B** `0xA1` → 80 B?~~ | ✅ Respondida. São 512 B; o abort está no `0xEC` | Deixa de procurar 80 B. **Em vez disso:** confirma que `0xEC` → `ERROR` com `ABRT`, e que a assinatura `LBA_LOW/MID/HIGH` é `0x81\|status` / `0x14` / `0xEB` |
| **C** o host usa DMA? | Evidência forte pelo timeout de 10 KB | Conta escritas em `FEATURES` com bit 0 cujo comando seguinte seja PACKET. Este é o teste directo |
| **E** lead-out 549300? | Nada novo | Inalterado |
| **D** `GET_SCD` 2/3? | Nada novo | Inalterado |

**Nova, e é a que mais vale:**

> **O host consegue puxar 14 KB num `CD_READ` sem abortar?**
> Se o `DMA_STATUS` vier e os 14 KB não saírem, o diagnóstico é o nosso pré-buffer
> a falhar, não a sniffer. Esta é a primeira pergunta que testa o firmware novo.

---

## 3. Procedimento quando houver placa

### 3.1 Porquê farejar do lado do GDEMU, e não da consola

O GDEMU tem **level-shifters** ([02](02-barramento-g1-pinout.md) §2.5): a sua saída
G1 é **3.3 V**, igual ao RP2350. Uma **consola VA1** pode ter 5 V no G1, e ligar o
RP2350 a 5 V sem level-shifter é a forma de matar a placa.

Farejar **do lado do GDEMU** evita o problema por completo. É a decisão que este
documento recomenda, e é a razão pela qual o GDEMU é melhor alvo que um disco
original: os dois servem o mesmo bus, o GDEMU é mais seguro de medir.

### 3.2 A sniffer tem de ser passiva — condição de aceitação

Uma sniffer que conduza **um único** sinal corrompe o bus. Antes de ligar:

- [ ] A PIO nunca configura um pino do G1 como output (verificar os `set_dir` /
      `gpio_set_dir` do programa PIO, e não assumir)
- [ ] Nenhum pino de IRQ/ack é puxado; a sniffer **lê** DMARQ/DMAACK, não os gera
- [ ] Nenhum pull-up/pull-down forte no lado do bus
- [ ] Alimentação separada, massas comuns

Isto não está verificado. [16](16-sniffer-g1.md) §1 assume passividade; **ninguém
auditou**. É a primeira coisa a fazer quando a placa existir, antes de a ligar a
uma consola.

### 3.3 Sequência

1. Montar a placa da sniffer. Confirmar que arranca e que a PIO não conduz (§3.2).
2. Desligar a consola da corrente. Ligar a sniffer **em paralelo** ao conector do
   GDEMU, lado 3.3 V.
3. Ligar o GDEMU com um dos 6 discos (o `01.bin` é o mais simples, sem tricks).
4. Ligar a consola, arrancar a captura **antes** do power-on.
5. Deixar o boot correr até ao ecrã, ou até a travar — as duas são dados.
6. Repetir com um jogo mais pesado (CD-DA) para ter transferências grandes.

### 3.4 O que extrair da captura

- o comando `0x71` e quantas palavras DATA vêm a seguir (**A**)
- o par `0xEC` → abort, e a assinatura nos registos LBA (**B**, substituto)
- escritas em `FEATURES` bit 0 (**C**)
- o maior `CD_READ` que arranca, e se os 14 KB passam (**requisito novo**)
- `GET_TOC` completo, para comparar com os 15 achados do [13](13-estudo-flycast.md)

---

## 4. O que continua aberto

- **A, C, D, E** — precisam da sniffer, e a placa não existe.
- **G, H** — [25](25-opengdemu-comportamento.md) §8 confirmou que o OpenGDEMU não
  ajuda. H é uma decisão de firmware que continua por tomar.
- **A string de versão do `REQ_MODE`** — o firmware diz `"Rev 5.07"`, o `docs/13`
  diz `"Rev 6.43"`, um GDEMU stock diz `"Rev 6.42"`. **Não mudou**, e é uma
  divergência que agora é a mais visível de todas. Ver [25](25-opengdemu-comportamento.md) §7.
- **Leituras acima de 18816 B** — continuam a servir do disco durante a transferência.
  Se a BIOS abortar o DMA nelas, é o mesmo bug noutro sítio. **Não verificado**,
  e é a limitação conhecida do pré-buffer.
- **REQ_MODE (0,10)** stock = `[00 00 00 00 00 b4 19 00 00 08]`. Não implementado.
  Também não medido.

---

## 5. Porquê o pré-buffer é só um palpite fundamentado, não uma certeza

Sendo explícito, porque é o que evita declarar isto fechado:

O timeout de ~10 KB é um número que o **OpenGDEMU mediu contra um GDEMU stock** e
publicou num comentário. Não é um número que tenhamos medido. A inferência
"logo, tenho de servir 14 KB sem stall" é sólida, mas há uma alternativa: a
GD-ROM real pode ter latência baixa **sem** pré-buffer, e o OpenGDEMU só precisa do
cache porque um cartão SD é lento.

As duas leituras diferem no que devia construir:

- **Se for um tecto do host** → o pré-buffer é obrigatório, e leitas maiores também
  precisam de ser pré-bufferizados.
- **Se for só do OpenGDEMU** → o pré-buffer é optimização, não correctness, e
  chega.

Não há como distinguir sem a placa. Fizeste-se a alteração na direcção **segura**
— com pré-buffer, o caso mau nunca acontece; sem ele, acontece sempre que a
latência do meio for alta. E é reversível: `GD_CDREAD_BUF_BYTES` é uma constante.

O teste **não** prova que o firmware está certo. Prova que a propriedade que
precisamos está imposta no código. Que a propriedade chega é medição.
