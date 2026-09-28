# SPDX-License-Identifier: Apache-2.0
#
# Nucleo do emulador GD-ROM - core portavel + simulador de host.
#
# Compila com -Wall -Wextra e como C99 estrito. A lição do Dreamdrive e
# que o build tem de funcionar a partir de um clone limpo, sempre.

CC      ?= cc
CSTD    := -std=c99
WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Wpointer-arith -Wcast-align
OPT     ?= -O2 -g
CFLAGS  += $(CSTD) $(WARN) $(OPT) -Isrc -Itools

CORE    := src/gd_taskfile.c src/gd_spi.c src/gd_cdda.c
SIM     := tools/hostsim.c tools/memdisc.c
IMGL    := src/gd_fs.c src/gd_gdi.c src/gd_cue.c
SNIFF   := src/sniffer.c
L1L     := src/gd_l1.c
TESTS   := tests/test_spec.c tests/test_cdda.c tests/test_timing.c tests/test_imgread.c tests/test_cue.c tests/test_sniffer.c tests/test_l1.c

BINS    := build/test_spec build/test_cdda build/test_timing build/test_imgread build/test_cue build/test_sniffer build/test_l1
TOOLS   := build/cue2gdi build/gdsniff

.PHONY: all test clean asan firmware tools

# ---------------------------------------------------------------------------
# Firmware real para RP2350B. Ver README.md para a cadeia de ferramentas.
# ---------------------------------------------------------------------------
# Caminho do SDK: via ambiente, ou ~/pico-sdk por omissao. O CMake
# falha com mensagem clara se nao existir (ver fw/CMakeLists.txt).
PICO_SDK_PATH ?= $(HOME)/pico-sdk

firmware:
	cmake -B build-rp -S . -DCMAKE_BUILD_TYPE=Release
	cmake --build build-rp -j$$(nproc)
	@echo
	@ls -la build-rp/dreamcast_gdrom.uf2 build-rp/dreamcast_gdrom.elf

all: test tools

build:
	@mkdir -p build

build/test_spec: $(CORE) $(SIM) tests/test_spec.c | build
	$(CC) $(CFLAGS) -o $@ $^

build/test_cdda: $(CORE) $(SIM) tests/test_cdda.c | build
	$(CC) $(CFLAGS) -o $@ $^

# O teste de timing nao precisa do nucleo: so o interpretador de PIO.
build/test_timing: tools/pio_vm.c tests/test_timing.c | build
	$(CC) $(CFLAGS) -o $@ $^

build/test_imgread: $(IMGL) $(CORE) tests/test_imgread.c | build
	$(CC) $(CFLAGS) -o $@ $^

build/test_cue: $(IMGL) tests/test_cue.c | build
	$(CC) $(CFLAGS) -o $@ $^

build/test_sniffer: $(SNIFF) tools/pio_vm.c tests/test_sniffer.c | build
	$(CC) $(CFLAGS) -o $@ $^

# O teste do L1 usa o harness do hostsim (CORE+SIM), como o test_spec.
build/test_l1: $(L1L) $(CORE) $(SIM) tools/pio_vm.c tests/test_l1.c | build
	$(CC) $(CFLAGS) -o $@ $^

build/cue2gdi: $(IMGL) tools/cue2gdi.c | build
	$(CC) $(CFLAGS) -o $@ $^

test: $(BINS)
	@./build/test_spec
	@echo
	@./build/test_cdda
	@echo
	@./build/test_timing
	@echo
	@./build/test_imgread
	@echo
	@./build/test_cue
	@echo
	@./build/test_sniffer
	@echo
	@./build/test_l1

build/gdsniff: $(SNIFF) tools/gdsniff.c | build
	$(CC) $(CFLAGS) -o $@ $^

tools: $(TOOLS)

# O ASan tem de recompilar de raiz: sem o `clean`, o make considera os
# binarios existentes actualizados e corre os testes SEM sanitizers,
# o que invalida a verificacao em silencio.
asan: clean
	$(MAKE) OPT="-O1 -g -fsanitize=address,undefined" \
	         CFLAGS="$(CSTD) $(WARN) -O1 -g -fsanitize=address,undefined -Isrc -Itools" test

clean:
	rm -rf build

# Dependencias
$(CORE) $(SIM) $(TESTS): $(wildcard src/*.h tools/*.h)
