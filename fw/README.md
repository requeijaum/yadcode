# `fw/` — núcleo do emulador GD-ROM

**Licença: Apache-2.0.** Ver [../09-riscos-e-licencas.md §2.1](../09-riscos-e-licencas.md).

Implementação do protocolo a partir da **spec Ver.1.30 da SEGA**, não a partir de código de
terceiros. O iceGDROM é GPL-3.0, o MAME é GPL-2.0+ e o Linux é GPL-2.0 — todos foram lidos
para conferir comportamento, e nada foi copiado.

## O que é isto

O núcleo de protocolo (task file + Sega Packet Interface) é **portável**: não depende do
RP2350, de FreeRTOS nem de pico-sdk. Corre num PC. É isso que permite validar a camada mais
arriscada do projecto sem comprar placa nenhuma.

```
src/gd_spec.h      constantes e tipos, cada um com a secção da spec de origem
src/gd_hal.h       interface de hardware: o que o núcleo precisa do mundo
src/gd_disc.h      interface da camada de imagem (L5)
src/gd_taskfile.*  L2: os 8 registos e a máquina de estados da secção 7.1
src/gd_spi.*       L3: os 16 comandos da Tabela 6.1, mais 0x70 e 0x71
src/gd_cdda.*      L4: reprodução de CD-DA e geração de subcode
src/gd_format.h    constantes da spec GD-ROM Format Basic Specs Ver. 2.14
src/gd_fs.*        abstracção de ficheiros (stdio no PC, SDIO no RP2350)
src/gd_gdi.*       L5: leitor de GDI, o formato de runtime
src/gd_cue.*       L5: leitor de CUE (Multi-Cue do Redump e TOSEC) + CUE→GDI

tools/hostsim.*    simulador de host: põe sinais nos registos e respeita a spec
tools/memdisc.*    imagem de GD-ROM em memória, para os testes
tools/cue2gdi.*    converte um dump Redump (CUE+BIN) em GDI
tools/gdsniff.*    analisador de capturas do sniffer (doc 16)
tests/test_spec.c  28 casos de conformidade do doc 11
tests/test_cdda.c  11 casos de CD-DA e subcode
tests/test_timing.c 8 casos de timing do PIO contra a ATA-3
tests/test_imgread.c 10 casos de GDI
tests/test_cue.c   12 casos de CUE
tests/test_sniffer.c 12 casos do sniffer (doc 16)
```

## Correr

```sh
make test      # 434 checks em seis suites
make asan      # o mesmo, com AddressSanitizer + UBSan (recompila de raiz)
make firmware  # firmware RP2350B real (.uf2)
make clean
```

## Cadeia de ferramentas

| Componente | Versão | Onde |
|---|---|---|
| pico-sdk | 2.3.1 (`079c6f3`) | `~/pico-sdk` |
| picoasm | 2.3.1 | `~/.local/bin/pioasm` (symlink para `pico-sdk/build/pioasm-install/pioasm/pioasm`) |
| picotool | 2.3.1 | `~/.local/bin/picotool` |
| arm-none-eabi-gcc | 14.2.1 | `/usr/bin` |
| cmake | 3.31 | `/usr/bin` |

Sem `sudo`. O SDK constrói `pioasm` e `picotool` localmente e o firmware é
gerado em `build-rp/`.

**Cinco coisas que o SDK 2.3.1 faz diferente do que se espera** (todas custaram
tempo e estão comentadas no `CMakeLists.txt`):

1. `include(pico_sdk_import.cmake)` **não existe**. É `pico_sdk_init.cmake`, e tem
   de ser seguido de uma chamada explícita a `pico_sdk_init()`.
2. `pico_sdk_init()` tem de ser chamada **depois** de `project()`.
3. `PICO_PLATFORM` tem de estar definido **antes** do include, senão a plataforma
   não carrega e o gcc compila em modo ARM — o newlib usa `cpsid`/`msr` e o
   Cortex-M33 falha.
4. A toolchain tem de vir de
   `cmake/preload/toolchains/pico_arm_cortex_m33_gcc.cmake` num
   `CMAKE_TOOLCHAIN_FILE`, senão o CMake escolhe o gcc do host e tenta
   assemblar ARM com o `as` x86.
5. O `CMAKE_EXECUTABLE_SUFFIX = .elf` é propagado ao scope do chamador *depois*
   de o target ser criado, por isso tem de ser definido à mão, ou o picotool
   recusa o binário.

E a API do PIO mudou: o programa gerado é um `pio_program_t` carregado com
`pio_add_program()`, e não há mais `<prog>_program_init()`. Também não existe
`pio_claim_sm()` — é `pio_claim_unused_sm()`.

## Prova de que o timing e' real

`make firmware` gera um `.uf2` que corre numa RP2350B. O `pioasm` é o **mesmo**
que valida a interpretação do `test_timing.c`, e as palavras do programa estão
verificadas byte a byte dentro do ELF.

| Parâmetro | Valor |
|---|---|
| alvo | `RP2350`, ARM Secure |
| `binary start` | `0x10000000` |
| tamanho | 39 936 B (.uf2) |

Compila com `-std=c99 -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes
-Wmissing-prototypes -Wpointer-arith -Wcast-align`.

> O Dreamdrive compila com `-O0` e `-Wall` desligado. Não é um exemplo a seguir.

## Como testar um comando

```c
hostsim_t hs;
memdisc_t md;
uint8_t pkt[GD_PKT_SIZE] = {0}, resp[4096];

memdisc_init(&md, 0 /* high_density */);
hostsim_init(&hs, &md.disc);

pkt[0] = GD_SPI_CD_READ;
pkt[1] = GD_READ_SEL_DATA;      /* Byte1[7:4] = 0x20: so o campo Data */
pkt[2] = 0; pkt[3] = 0; pkt[4] = 5;   /* FAD 5, big-endian */
pkt[8] = 0; pkt[9] = 0; pkt[10] = 1;  /* 1 sector, 24-bit */

uint32_t n = hostsim_spi_command(&hs, pkt, resp, sizeof resp);
/* n == 2048, e resp contem o sector 5 da imagem */
```

`hostsim_dump(&hs)` imprime o trace de acessos com timestamps.

## Sete bugs que os testes apanharam

Vale a pena registar: **todos eram silenciosos**. Compilavam, e o código parecia correcto.

1. **`gd_spi_buf` fazia dois papéis.** Era o packet *e* o buffer de resposta. Os handlers
   faziam `buf_reset()` e escreviam a resposta a partir do offset 0, apagando o packet que
   ainda precisavam de ler. Sintoma: `REQ_STAT` devolvia 10 bytes com o Disc Format a zero.
   Correcção: `gd_spi_buf` é só resposta; o packet vive em `dev->packet` e passa-se como
   argumento.

2. **Ordem de byteslittle-endian na serialização.** O registo Data entrega palavras de
   16 bits **big-endian** (§3.4), mas o harness gravava-as com um `uint16_t *` directo,
   produzindo cada par de bytes trocado. Sintoma: todos os campos ASCII saíam invertidos.

3. **Clamp errado em `gd_taskfile_read_data`.** Comparava `transferred + nwords*2 > remaining`
   quando `remaining` já é o total em falta. A transferência truncava a meio e o device
   ficava **permanentemente** em `DATA_IN` — o que bloqueava todos os comandos seguintes.
   Este era o pior: um bug de transferênciaTimeouts que se manifestava como "depois do
   GET_TOC, nada mais responde".

4. **`GET_TOC` parava a 216 bytes de 408.** Efeito secundário do #3.

5. **`CD_READ2` lia o comprimento no sítio errado.** `CD_READ` (0x30) tem o transfer length em
   Bytes 8..10 (24-bit); `CD_READ2` (0x31) tem-no em Bytes 6..7 (16-bit), e o next address
   em 8..10. Usar o layout de um no outro dava 0 sectores.

6. **`GET_SCD` formato 0 devolvia 28 bytes em vez de 100.** O subcode P..W são 8 canais × 12
   bytes = 96, não 24. E o campo length dizia 100, portanto o host lia para fora do buffer.

7. **`IDENTIFY` escrevia as strings no offset errado.** Ficavam em 0x04 em vez de 0x10, e a
   resposta tinha 60 bytes em vez de 64.

Um oitavo era do próprio teste: um `resp[1024]` mais pequeno que o sector de 2048 bytes que
se tentava ler. Buffer transbordado silenciosamente.

## Porquê o simulador

O host real é o HOLLY, e a única forma de saber o que ele espera é ler o código de
referência. O simulador impõe a sequência da §7.1, o que significa que **um device que
passe estes testes produz a mesma sequência de transacções que um device correcto**.

O que isto **não** valida: timing eléctrico. `t0` = 180 ns, `t5` = 20 ns, `tB` = 1250 ns
(ver [../03-timing-ide.md](../03-timing-ide.md)) só se valida com um analisador lógico ou
com o loopback PIO-a-PIO descrito no doc 11 §5.

## Estado

| | |
|---|---|
| L2 task file | ✅ 28 casos |
| L3 SPI | ✅ 16 comandos + 0x70/0x71, valores de resposta conferidos contra a referência |
| L4 CD-DA + subcode | ✅ 11 casos: 4 formatos, CRC, BCD vs binário, avanço temporal |
| L1 PIO | ⬜ não iniciado |
| L5 GDI | ✅ 10 casos. Leitor completo, `end_fad`, TOC por área, validação |
| L5 CUE | ✅ 12 casos. Multi-Cue do Redump + TOSEC com PREGAP |
| `cue2gdi` | ✅ ferramenta. Round-trip testado CUE → GDI → releitura |
| `GetBaseFAD() = 45150` | ✅ em `gd_format.h`, alimentado pelo GDI/CUE |

**434 checks, 0 falhas, limpo sob ASan+UBSan** (94+39+31+64+100+106).

## O que o estudo do Flycast mudou

15 diferenças de comportamento, todas corrigidas — ver
[../13-estudo-flycast.md](../13-estudo-flycast.md). As que mais doeram:

- **`REQ_STAT` Byte 4 (Index) tem de ser `1`.** A spec não diz; um `0` faz o host achar que
  está antes do primeiro sector útil.
- **Um GD-ROM tem sempre 2 sessões**, não 1. A sessão 1 começa na track 1, a 2 na track 3.
- **`GET_TOC` força ADR a 1** e põe o número da track no campo FAD das entradas
  [396..403].
- **Alternate Status não limpa o INTRQ; só Status limpa.** Vários jogos dependem disso.
- **`REQ_ERROR` limpa o erro ao responder**, não no comando seguinte.
- **Soft Reset escreve Byte Count `0x14`/`0xEB`** — "DC Checker expects these values".

E um bug nosso que só a comparação apanhou: `sync_status()` reconstruía o registo Status
inteiro, **apagando o bit CHECK** que o erro acabara de por. O host nunca via o CHECK, só
fazia polling.

## Não reproduzido, de propósito

- **`0xA1`**: o comportamento real inclui um abort e um blob de 80 bytes que a spec não
  menciona. Sem fonte permissiva nem dump de hardware, inventar os bytes seria pior que
  ficar pelo IDENTIFY sensato.
- **`0x71`**: mantidos os 6 bytes (`BA 06 0D CA 6A 1F`) porque são o que o iceGDROM e o
  Dreamdrive usam, ambos em hardware real. A referência GPL devolve 1012.

## Detalhe que merece atenção: o CRC do subcode

| Variante | xorout | `crc("123456789")` |
|---|---|---|
| CRC-16/XMODEM (padrão) | `0x0000` | 0x31C3 |
| Variante do GD-ROM | `0xFFFF` | **0xCE3C** |

A referência implementa `return ~crc`, ou seja, com complemento final. O XMODEM padrão não
tem. Como o subcode é copiado verbatim pelos jogos, isto importa e não é adivinhável sem um
dump de hardware. Mantida a variante observada, e o teste 29 fixa as duas constantes lado a
lado.

## Os quatro bugs que a suite de L4 apanhou

1. **Overrun da TOC.** `track_of_fad` devolve `0xAA` quando o FAD não pertence a nenhuma
   track, e o código indexava `d->toc[0xAA - 1]` sem limite. Adicionado `num_tracks` e
   validação em `build_q_bcd` e `scd_q`.
2. **CD_SEEK param type 3.** O `set_state(PAUSE)` no fim da função sobrescrevia o
   `STANDBY` que o "stop → home" devia deixar.
3. **Tracks sobrepostas no fixture.** `memdisc_track_of_fad` devolvia sempre a track 1
   porque os intervalos se sobrepunham — o mesmo FAD pertence a duas tracks.
4. **Testes meus com índices errados.** A expansão do P: `0x40` tem o bit a 1 no índice 1, não
   no 6.
