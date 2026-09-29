# 25 — Estudo do OpenGDEMU: o quarto ODE, e o timeout de 10 KB

Data: 2026-09-29. OpenGDEMU clonado em `ref/opengdemu`, HEAD `900f97b9` (2026-09-22).
Ficheiro principal: `mcu/opengdemu/src/ide.rs` (3290 linhas).

> 🔴 **O OpenGDEMU é GPL-3.0** (verificado no `LICENSE`, "Version 3, 29 June 2007").
> Nada foi copiado. Extraí-se comportamento, sequências de pacotes e constantes
> numéricas. A regra de [09](09-riscos-e-licencas.md) aplica-se: comportamento e
> constantes entram; funções não.

> **Por que este estudo e não o `.FW` do GDEMU.** O firmware que o utilizador tem
> (`GDEMU_052005.FW`, 98304 bytes) é **cifrado** — medido: entropia 7,998 bits/byte,
> zero magic bytes, zero strings. Não é analisável, e o autor distribui-o cifrado de
> propósito. O OpenGDEMU é a única implementação **aberta** de hardware de GD-ROM
> que existe: a referência que faltava depois do Flycast, da BIOS e do Dreamdrive.

---

## 1. O que o OpenGDEMU é (e o que não é)

Contra o que se supõe à primeira: **o GDEMU não é FPGA, e o OpenGDEMU também não
produz um bitstream funcional.** A arquitectura real é split:

| Camada | Onde | Linguagem | Papel |
|---|---|---|---|
| **MCU** | `mcu/opengdemu/src/` | **Rust** (ARM ATSAM3U4E) | toda a lógica de comando GD-ROM, TOC, CDDA, filesystem |
| FPGA | `fpga/amaranth/hw/` | Python (Amaranth) | temporização G1, máquina de estados de sinais, arbitragem |
| Bitstream | `fpga/Quartus` | Verilog gerado | **quebrado** — o README diz "Open source bitstream generation when :(" |

O README é honesto sobre o estado: *"WIP. DO NOT ATTEMPT to flash unless you're
okay with having a non-functional GDEMU"*.

A consequência prática: **a lógica de comando está em Rust legível, não numa DSL de
hardware.** O `ide_device.py` (64 KB) é temporização, não protocolo.

> **Proveniência, com honestidade.** O OpenGDEMU cita o **iceGDROM**
> (`rv32/source/ide.c:197-199`) como origem do magic de `0x71`, e afirma ter
> verificado contra um **GDEMU stock** via dcload-serial. O yadcode já conhecia o
> iceGDROM ([13](13-estudo-flycast.md)). Isto é a **mesma** fonte vista por outro
> caminho, não evidência independente. Ver §3.

---

## 2. Confirmações: o yadcode já estava certo

O resultado mais valioso foi **não** encontrar erros no modelo do projecto. Cada
ponto foi confirmado linha-a-linha contra `ide.rs`.

| # | Afirmação do yadcode | Onde no OpenGDEMU | Estado |
|---|---|---|---|
| 1 | Packet de **exactamente 12 bytes** (6 words), byte 0 = opcode | `recv_packet()` lê 6 words | ✅ |
| 2 | Opcodes `10`–`40` (REQ_STAT…GET_SCD) | `ide.rs:1020-1032`, **idênticos** | ✅ |
| 3 | `0x70` é probe **sem dados**, sempre seguido por `0x71` | `ide.rs:1406-1413` | ✅ |
| 4 | `CD_READ`: FAD em [2..4], count em [8..10], big-endian 24-bit | `ide.rs:1456-1462` | ✅ |
| 5 | `LBA = FAD − 150` (offset CD-frame) | `ide.rs:1461`, `saturating_sub(150)` | ✅ |
| 6 | Os nomes Flycast (`SPI_CD_READ`…) são convenção, não spec | mesmos valores em `ide.rs` | ✅ |

O ponto 3 tem um valor que os outros não têm. O OpenGDEMU **corrigiu um bug próprio**
e deixou o comentário: o branch de `0x70` chegou a devolver o magic de 6 bytes, e
estava errado — *"the magic is for 0x71 only"*. Confirma
[01](01-protocolo-spi-sega.md) §4.1, e avisa: **este par é fácil de baralhar**, e o
erro é silencioso, porque a BIOS pode avançar na mesma.

---

## 3. A questão A: `0x71` responde com 6 bytes ou 1012?

**O OpenGDEMU responde 6 bytes, exactamente** — e decide a ambiguidade a favor do
lado minoritário.

```text
SEGA_VENDOR_71 => {
    // Sega disc-auth challenge. The reply is a fixed 6-byte
    // magic and the BIOS expects the full 6 bytes regardless
    // of what's in packet[4] (per iceGDROM, which ignores the
    // alloc-len field for this command).
    send_data_in(dev, &VENDOR_HANDSHAKE_REPLY).await;
}
static VENDOR_HANDSHAKE_REPLY: [u8; 6] = [0xBA, 0x06, 0x0D, 0xCA, 0x6A, 0x1F];
```

### Como ler isto contra o Flycast

O Flycast devolve **1012 bytes** ([13](13-estudo-flycast.md) §1). Isso **não é
necessariamente um erro do Flycast**, e o yadcode não deve mudar de posição por isto.
A leitura honesta:

- Os 6 bytes são o que a BIOS **precisa**.
- Uma drive real pode continuar a transferir o resto do buffer; a BIOS lê o Byte
  Count e para. O OpenGDEMU é minimalista porque o seu objectivo é funcionar, não
  ser bit-exacto com o hardware.

🔶 **A questão A não está fechada — está estreitada.** O que se pode dizer com
confiança: *6 bytes é suficiente para o boot avançar*, e é o que duas implementações
abertas e um GDEMU stock convergentemente fazem. O que continua aberto é o que uma
drive *real* põe no buffer além dos 6, e de onde vêm os 1012 do Flycast. **Só a
sniffer decide** ([26](26-sniff-gdemu-real.md), Fase C).

---

## 4. A questão B: `0xA1` aborta e devolve os 80 bytes?

🟢 **Respondida, e a hipótese estava errada.** O `0xA1` não aborta — e devolve
**512 bytes**, não 80.

O mecanismo é o inverso do que a pergunta supunha, e a ATA impõe-o:

```text
CMD_IDENTIFY => match role {            // 0xEC IDENTIFY DEVICE
    Role::Packet => { dev.set_error(ERR_ABRT); ... }   // ABORTA
    Role::Disk   => { /* responde */ }
}
CMD_IDENTIFY_PACKET => match role {     // 0xA1 IDENTIFY PACKET DEVICE
    Role::Packet => { stage_identify_packet(dev); dev.set_atapi_byte_count(512); ... }
    Role::Disk   => { dev.set_error(ERR_ABRT); ... }   // ABORTA
}
```

> *"ATA spec: a packet device shall ABRT IDENTIFY DEVICE so the host falls back to
> IDENTIFY PACKET DEVICE."* — `ide.rs:701-702`

A resposta correcta à pergunta é: **o abort está no `0xEC`, não no `0xA1`.** O
`0xA1` num GD-ROM devolve os 512 bytes do ATAPI padrão (256 palavras) com
`DRQ=1`; os 80 bytes nunca houve. A pergunta supunha que `0xA1` era a resposta com
blob, e essa parte estava errada desde a formulação.

**Impacto no firmware:** o yadcode tem de responder `ABRT` a `0xEC` e 512 bytes a
`0xA1`. Se inverter, a BIOS não encontra o disco. Isto é comportamento de
arranque, não um detalhe.

---

## 5. O achado que não estava previsto: o DMA da BIOS tem timeout de ~10 KB

Este é o resultado real do estudo, e **não está em nenhum documento do yadcode**.
Afecta directamente a questão C e a [06b](06b-scope-pio-vs-dma.md).

```text
/// If a CD_READ matches this exactly we skip the SD
/// read entirely, giving the host's DMA controller data to pull
/// the moment it issues DMA_STATUS=1 — which is what the BIOS does
/// right after the CDB. With SD-read latency in the path the BIOS's
/// DMA channel times out at ~10 KB; cached, it drains all 14 KB.
```

Três factos, todos medidos contra hardware:

1. **O canal de DMA da BIOS aborta a ~10 KB.** Se a latência da drive estiver no
   caminho — isto é, se a drive não tiver os sectores prontos quando o host começa a
   puxar — o canal morre. É um tecto do *host*, e é um número observado, não
   inferido.
2. **O primeiro read da BIOS são 7 sectores (14 KB) — o IP.BIN.** Confirmado e
   dimensionado: `CD_READ_BUF_BYTES = 14 * 1024`, comentado como *"Sized to fit the
   BIOS's 7-sector IP.BIN read"*. Dá **tamanho exacto** à boot chain de
   [23](23-gdrom-syscall-table.md), que antes só tinha a sequência.
3. **A drive tem de servir os 14 KB sem stall.** O OpenGDEMU só arranca porque
   pré-carrega o IP.BIN em SRAM antes de a BIOS pedir.

### Consequência para o nosso firmware

Um emulador que responda a `CD_READ` a partir de flash **com latência no caminho
vai reproduzir exactamente esta falha e o boot vai morrer.** Isto deixa de ser uma
optimização e passa a ser um **requisito de arranque**: ou os sectores estão prontos
antes do `DMA_STATUS=1`, ou o host aborta.

Não prova que uma GD-ROM real cacheie. Pode ser que tenha latência intrínseca menor
que um cartão SD (µs de RAM contra ms de seek), e nesse caso o problema é do OpenGDEMU
e não do conceito. A pergunta útil deixa de ser "que latência tem a drive" e passa a
ser: **"consigo servir 14 KB sem stall?"** — e a resposta tem de ser sim, por desenho.

### Sobre a questão C (`o host real usa DMA?`)

🟡 **Forte evidência indirecta, mas não é medição directa.** O OpenGDEMU responde a
`CD_READ` por DMA como via normal, e o timeout de 10 KB só existe *se* o host usar
DMA. Isto torna "o host usa DMA" a leitura provável, e é uma razão a mais para não
tratar PIO como o caminho principal. Mas a confirmação continua a ser a sniffer a
contar escritas em `FEATURES` com bit 0 ([16](16-sniffer-g1.md) §6 C) — o
OpenGDEMU assume-o em vez de o medir.

---

## 6. Divergência encontrada no nosso firmware: o IDENTIFY tem 64 bytes, deveria ter 512

Esta é a consequência accionável de §4, e **o yadcode está errado** — ou, com mais
cuidado, **está diferente de uma forma que ainda não sabemos se importa**.

| | yadcode | OpenGDEMU / ATAPI |
|---|---|---|
| Resposta a `0xA1` | **64 bytes** (`gd_spi_buf_len`, testado em `test_spec.c:118`) | **512 bytes** (256 palavras) |
| `0xEC` | cai no `default:` e aborta | aborta explicitamente |

O `0xEC` está **certo por acidente** — o `default:` aborta, que é o que a ATA
exige de um packet device. Mas o tamanho é uma divergência real.

**Por que isto importa.** A BIOS lê o Byte Count e lê o que lá está. Se a BIOS
consultar palavras acima da 31, com 64 bytes está a ler lixo. Não há forma de saber
a partir de código se a BIOS Dreamcast vai lá — e oprojecção de 64 bytes vem do Flycast
(que tem o mesmo modelo), o que significa que **duas fontes que copiam uma da outra
podem partilhar o mesmo erro.**

⚠️ **Não mudo isto agora, e recomendo que não se mude à pressa.** Tratar-se-ia de
uma alteração de firmware fundada numa única fonte GPL em estado WIP, verificada
contra um GDEMU mas não contra a BIOS de retail. O que fica é: **é uma divergência
conhecida, com uma acção de verificação concreta** — a pergunta para a sniffer é
simples: *quantas palavras DATA o host lê depois de um `0xA1`?* Se ler 256, mudamos.
Se ler 32, o Flycast está certo e o OpenGDEMU éminimalista.

Isto entra na lista como **requisito de arranque candidato**, da mesma classe do
timeout de 10 KB: coisas que, se erradas, a BIOS não encontra o disco.

---

## 7. Divergências adicionais em `REQ_MODE` e `SET_MODE` (achadas na verificação)

Estas apareceram ao rever as referências de linha, e são **as mais accionáveis de todas**,
porque são todas verify-before-boot contra um GDEMU stock.

### `REQ_MODE`: o BIOS só faz duas perguntas, e o resto aborta

```text
// packet[2] = offset, packet[4] = length. Per iceGDROM
// rv32/source/ide.c:202-213 the BIOS only ever asks for
// (offset=18, len=8) → "Rev 5.07" version string and
// (offset=0, len=10) → all zeros. Anything else aborts
// with status 0x50.
```

| Offset, len | yadcode | OpenGDEMU (verificado vs stock GDEMU) | Estado |
|---|---|---|---|
| (18, 8) | `"Rev 5.07"` no firmware, `"Rev 6.43"` no `docs/13` | **`"Rev 6.42"`** | 🔴 **três valores diferentes** |
| (0, 10) | não verificado | `[00 00 00 00 00 b4 19 00 00 08]` | 🔴 desconhecido no nosso lado |
| qualquer outro | responde genericamente | **ABRT, status `0x50`** | 🟡 divergência de comportamento |

Duas coisas a reter:

1. **A string de versão é `6.42` num GDEMU stock.** O `docs/13` §1 #2 diz `6.43` (do
   Flycast) e o firmware diz `5.07` (do iceGDROM). O OpenGDEMU diz explicitamente que
   o `5.07` do iceGDROM *"appears to be wrong"*. **Nenhum dos nossos dois valores está
   confirmado.** Isto é a mesma divergência de 64-vs-512 em outra forma: duas fontes
   open-source que uma copiou da outra, a divergir da hardware.
2. **O abort em `0x50` é comportamento, não robustez.** Se a BIOS nunca pergunta
   além destas duas, abortar o resto é correcto e é o que o OpenGDEMU faz. O nosso
   firmware responde a qualquer offset — o que é mais permissivo do que o hardware.
   Pode ser inofensivo; não está verificado.

### `SET_MODE`: uma fase de dados que não pode ser ignorada

```text
// Sega SET_MODE is a data-OUT command: host writes
// `packet[4]` bytes (typically 10) to the drive. Without
// draining that phase the host hangs waiting for DRQ to
// clear and the BIOS's post-CD_READ "configure drive" step
// never completes — game launch never proceeds.
```

O nosso firmware **tem** o caminho de escrita: `do_set_mode` arma `dev->sink`, e
`gd_taskfile_write_data` consome-o (`gd_taskfile.c:252-269`). ✅ funcionalmente
coberto.

⚠️ **Mas usa a fase errada, e isto é um risco real.** Não existe `GD_PHASE_DATA_OUT`:
o taskfile tem só `IDLE`, `PACKET_RECV` e `DATA_IN` (`gd_taskfile.h:32-34`), e
`do_set_mode` faz `dev->phase = GD_PHASE_DATA_IN` para um transfer que é de escrita.
Funciona porque o dispatch do registo DATA testa `dev->sink` **antes** de olhar para
a fase — mas isso é coincidência estrutural, não desenho. Qualquer refactor que
inverta essa ordem passa a aceitar leituras num transfer que é de escrita.

**Não é um bug hoje. É uma armadilha para quem mexer depois.** Registado como tal.

### Um item que NÃO afirmo ser bug

`do_set_mode` não limpa `dev->src`, e `gd_taskfile_data_pending` só tem
`GD_PHASE_DATA_IN` como guarda. Se um comando anterior deixou `dev->src` armado, o
host poderia ler durante o `SET_MODE`. **Não provei que isto seja alcançável** — o
`gd_taskfile_end_transfer` limpa `dev->src`, mas não confirmei que seja chamado entre
comandos. Fica como pergunta, não como achado.

---

## 8. Detalhes com valor operacional

| Detalhe | O que é | Onde | Porquê importa |
|---|---|---|---|
| **Bit 0 de FEATURES = modo DMA** | O host pede DMA por esse bit, não por um comando | `ide.rs:1116` | O handshake DMA é por registo, não por opcode. Confirma o desenho de [06b](06b-scope-pio-vs-dma.md) |
| **Byte-count limit em LBA Mid/High** | Limite PIO do host vem de `(lba_mid) \| (lba_high << 8)` **no momento do comando** | `ide.rs:1120` | Os setters de data-in **sobrescrevem** estes registos. Quem os escrever cedo corrompe o limite. Subtile, e fácil de errar |
| **UNIT_ATTENTION é obrigatório** | Após reset, todo o PACKET excepto REQUEST_SENSE/REQ_ERROR devolve CHECK_CONDITION + sense key 6 | `ide.rs:1159-1185` | **A BIOS nunca passa da validação de TOC sem isto.** O comentário cita medição: *"first response = STATUS=0x11, ERR=0x60"* |
| **Assinatura de packet device** | `LBA_LOW = 0x81 \| status`, `LBA_MID = 0x14`, `LBA_HIGH = 0xEB`, packed em addr 6 | `ide.rs:357-359, 596` | Como o host distingue GD-ROM de disco. `0x81` é o valor de reset do FPGA, não `0x82` — comentário explícito sobre um bug anterior |
| **O formato segue a imagem** | Não está fixado a GD-ROM: um MIL-CD XA precisa de outro valor | `ide.rs:360-363` | A assinatura **não é constante** entre imagens. Implica o nosso `gd_taskfile` |
| **O LBA do boot é dependente da imagem** | `gdi.boot_area_lba`, lido do GDI | `ide.rs:1869` | O read de 7 sectores do IP.BIN **não** está num LBA fixo. Não assumir 0 |
| **`0x71` ignora o alloc-len** | O BIOS quer os 6 bytes, independentemente de `packet[4]` | `ide.rs:1415-1421` | O campo Byte4 não é respeitado neste comando |
| **`GET_SCD` aloca 100 bytes** | `reply = [0u8; 100]`, comprimento por formato | `ide.rs:1395` | Dá a dimensão de trabalho para os 4 formatos de subcode |

---

## 9. O que este estudo **não** dá

Sendo honesto sobre os limites, porque é o que evita declarar fechado o que não está:

- **A questão H continua exactamente como estava.** Era o device select `0x90`/`0xB0`
  ([19](19-kos-e-reversao-da-bios.md) §5). **Não há implementação no OpenGDEMU** —
  procurei `0x90`, `0xB0`, `device_select`, e não existe. O `Role` acima é outra
  coisa: é a assinatura de packet-vs-disk, não o device select.
- **A questão G continua aberta.** `GetBaseFAD() = 45150` não aparece no OpenGDEMU.
- **A questão D não é tocada.** `GET_SCD` está implementado, mas o formato 2/3 (UPC,
  ISRC) ser usado por algum jogo continua a ser uma questão de testes, não de
  leitura de código.
- **A questão E não é tocada.** Nada aqui diz nada sobre `GD_LEADOUT_FAD = 549300`.
- **Nenhuma captura de timing real.** O OpenGDEMU mediu contra um GDEMU stock, mas
  via dcload-serial, não com analisador lógico no bus. Os números de latência são de
  quem os cita, não de quem os mediu.
- **A questão A continua aberta** (ver §3).

---

## 10. Resumo do efeito nas questões

| # | Pergunta | Antes | Agora |
|---|---|---|---|
| **A** | `0x71`: 6 bytes ou 1012? | aberta | 🟡 **estreitada** — 6 é suficiente; o resto do buffer continua desconhecido |
| **B** | `0xA1` aborta + 80 bytes? | aberta | 🟢 **respondida** — não aborta; 512 bytes. O abort está no `0xEC` |
| **C** | O host real usa DMA? | aberta | 🟡 **evidência forte** — o timeout de 10 KB só existe se usar; falta a medição |
| D | `GET_SCD` formato 2/3 usado? | aberta | ⚪ inalterada |
| E | `LEADOUT = 549300` universal? | aberta | ⚪ inalterada |
| **F** | CRC do subcode | ✅ fechada | ✅ inalterada |
| G | `GetBaseFAD() = 45150` | aberta | ⚪ inalterada |
| **H** | device select `0x90`/`0xB0` | aberta | ⚪ inalterada — não implementado no OpenGDEMU |

E uma questão nova, que não estava na lista e que é agora **requisito de
arranque** do nosso firmware:

> **"Conseguimos servir 14 KB num `CD_READ` sem stall, antes do `DMA_STATUS=1`?"**
> Se a resposta for não, a BIOS aborta o canal de DMA e o boot não arranca. Isto não
> estava em [08](08-arquitetura-proposta.md) e devia entrar no plano de firmware.

---

## 11. Correção a um erro meu, de registo

Ao iniciar este estudo escrevi que o OpenGDEMU era "SpinalHDL, 3,8 MB de Rust" e que
a lógica do GD-ROM estava no `ide_device.py` do Amaranth. **As duas coisas eram
falsas.** A lógica de comando está em `mcu/.../ide.rs` (Rust, 3290 linhas) e o
`fpga/amaranth/` é Python para temporização, não para o protocolo. Quando uma pasta
se chama pelo formato (`amaranth/`) e a linguagem maior do repositório é outra, a pasta
não é o protocolo. Lição já registada em [24](24-ghidra-re.md) §6, aqui confirmada:
**o caminho do código é uma hipótese, não um facto.**

---

## 12. Referência

- `ref/opengdemu/` — HEAD `900f97b9`, GPL-3.0
- Lidos: `mcu/opengdemu/src/ide.rs`, `fpga/amaranth/hw/ide_device.py`,
  `fpga/amaranth/hw/ide_decode.py`, `README.md`
- Fonte upstream do magic de `0x71`, segundo o próprio projecto: iceGDROM
  `rv32/source/ide.c:197-199` (GPL-3.0) — **mesma** fonte que o yadcode já conhecia
