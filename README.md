# yadcode — yet another dreamcast ODE

Emulador de GD-ROM para Dreamcast sobre RP2350B, mais um sniffer do
barramento G1 para responder — medindo, não adivinhando — o que a
documentação pública não fecha.

## Estado

- **434 checks, 0 falhas**, limpo sob ASan+UBSan (94+39+31+64+100+106).
- Firmware RP2350B compila (`dreamcast_gdrom.uf2`, alvo confirmado).
- **Sem validação em silício**: não há placa. O PIO, o ring, a análise
  e o firmware compilam e são testados em simulação, mas nunca correram
  num RP2350B nem contra um GD-ROM real. Ver `16-sniffer-g1.md` §8.

## Layout

```
fw/            firmware + núcleo portátil + testes + ferramentas
  src/         task file, SPI, CD-DA, GDI/CUE, sniffer, main RP2350B
  pio/         g1_timing.pio (ciclos ATA-3) e g1_sniff.pio (captura)
  tests/       seis suites de host, sem hardware
  tools/       cue2gdi, gdsniff, interpretador de PIO (pio_vm)
00-README.md   índice e estado global do projecto
01-…16-*.md    estudos: protocolo, G1, timings, referências, formatos
```

`ref/` (clones de referência como o Flycast, GPL) **não vai para o
repo**: são 130 MB para consulta local. Para reproduzir, ver
`07-referencias-codigo.md`.

## Pré-requisitos

- `gcc`, `cmake ≥ 3.13`, `arm-none-eabi-gcc`
- `PICO_SDK_PATH` a apontar para o pico-sdk 2.3.1
- `pioasm` e `picotool` no `PATH`

## Correr

```sh
cd fw
make test      # 434 checks em seis suites
make asan      # o mesmo, com AddressSanitizer + UBSan (recompila de raiz)
make tools     # cue2gdi + gdsniff
make firmware  # firmware RP2350B real (.uf2 em fw/build-rp/)
```

Detalhes da cadeia e da metodologia em `fw/README.md`.

## Documentação

Começar por `00-README.md` (índice), depois:

- `01-protocolo-spi-sega.md`, `02-barramento-g1-pinout.md`,
  `03-timing-ide.md` — o hardware que se emula
- `13-estudo-flycast.md` §7 — as sete questões em aberto (A–G);
  cinco (A, B, C, E, F) só se resolvem com hardware real
- `16-sniffer-g1.md` — o sniffer: PIO de captura, análise e o que falta
- `15-formatos-imagem-gd.md` — GDI/CUE, FAD/LBA, TOSEC

## Licença

Apache-2.0 — ver `LICENSE.md`. Aplica-se ao código e aos estudos
deste repo. Referências a projectos de terceiros (Dreamdrive, Flycast,
iceGDROM, ZuluIDE, MAME, Linux) mantêm as licenças próprias e são
citadas como consulta, não redistribuídas aqui.

## Remoto

Ainda sem remoto configurado. Quando houver conta/repo no GitHub:

```sh
git remote add origin git@github.com:<user>/yadcode.git
git push -u origin main
```
