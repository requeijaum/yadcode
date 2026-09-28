/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_l1.c - O esqueleto L1 contra a Tabela 3.1 e contra o L2.
 *
 * O que se testa aqui e' tudo o que NAO precisa de hardware: que a
 * descodificacao e' a Tabela 3.1 linha a linha, que o despacho chama
 * o L2 certo com os args certos, e que o programa PIO monta e corre
 * no interpretador. O que precisa de silicio esta' marcado e nao e'
 * fingido.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pio_vm.h"
#include "gd_l1.h"
#include "gd_spec.h"
#include "gd_taskfile.h"
#include "sniffer.h"
#include "hostsim.h"
#include "memdisc.h"

static int g_fail, g_pass, g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) { printf("  FAIL  " fmt "\n", ##__VA_ARGS__); g_fail++; } \
        else             { g_pass++; }                                       \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

/* g1_dev_slow, tal como `pioasm -o hex -v 1` o produz. */
static const uint16_t g1_dev_slow[] = {
    0x2033,  /* 0  wait 0 pin 19 (CS0) */
    0xa020,  /* 1  mov x, pins         */
    0xa0c1,  /* 2  mov isr, x          */
    0x8020,  /* 3  push                */
    0xc000,  /* 4  irq 0               */
    0x20b3,  /* 5  wait 1 pin 19       */
    0x0000   /* 6  jmp 0 (idle)        */
};

/*
 * Niveis dos pinos (0 = asserted). Da' menos nos do que montar words:
 * o que se testa e' a tabela, nao o shifter.
 */
static gd_l1_reg_t acc(unsigned cs0, unsigned cs1, unsigned da,
                       unsigned rd, unsigned wr, int *isw)
{
    return gd_l1_decode(cs0, cs1, da, rd, wr, isw);
}

/* Leitura = RD asserted (0), WR inactivo (1). Escrita = ao contrario. */
static gd_l1_reg_t rd(unsigned cs0, unsigned cs1, unsigned da, int *isw)
{
    return acc(cs0, cs1, da, 0, 1, isw);
}
static gd_l1_reg_t wr(unsigned cs0, unsigned cs1, unsigned da, int *isw)
{
    return acc(cs0, cs1, da, 1, 0, isw);
}

/* Fakes de hw: registam chamadas para o caso 96. */
static int fake_intrq_seen = -1;
static int fake_init_seen;
static void fake_init(void) { fake_init_seen = 1; }
static void fake_intrq(int a) { fake_intrq_seen = a; }
static void fake_nop(int a) { (void)a; }
static uint32_t fake_micros(void) { return 0; }
static const gd_l1_hw_t hw_fake = {
    fake_init, fake_intrq, fake_nop, fake_nop, fake_micros
};

int main(void)
{
    pio_vm_t vm;
    hostsim_t hs;
    memdisc_t md;
    const char *nm;
    int isw = -1;
    int i;

    printf("L1: esqueleto device-side (Tabela 3.1 + despacho L2)\n\n");

    memdisc_init(&md, 0);
    hostsim_init(&hs, &md.disc);

    /* ---------------------------------------------------------------- */
    CASE(90, "Decode: a Tabela 3.1 linha a linha, nas duas direccoes");
    /* N/N e A/A: nunca e' acesso, com ou sem strobe. */
    CHECK(acc(1, 1, 0, 0, 1, &isw) == L1R_NONE, "N/N com RD devia ser NONE");
    CHECK(acc(1, 1, 0, 1, 0, &isw) == L1R_NONE, "N/N com WR devia ser NONE");
    CHECK(acc(0, 0, 7, 0, 1, &isw) == L1R_NONE, "A/A devia ser NONE");
    /* Sem strobe ou com os dois: nao e' acesso. */
    CHECK(acc(0, 1, 0, 1, 1, &isw) == L1R_NONE, "sem strobe devia ser NONE");
    CHECK(acc(0, 1, 0, 0, 0, &isw) == L1R_NONE, "RD+WR devia ser NONE");
    /* Control Block: so' DA=110 existe; o resto e' hi-Z/nao-usado. */
    CHECK(rd(1, 0, 0x6, &isw) == L1R_ALTSTATUS, "CS1+DA110+RD = ALTSTATUS");
    CHECK(wr(1, 0, 0x6, &isw) == L1R_DEVCONTROL, "CS1+DA110+WR = DEVCONTROL");
    CHECK(rd(1, 0, 0x0, &isw) == L1R_NONE, "CS1+DA000 devia ser NONE");
    CHECK(rd(1, 0, 0x4, &isw) == L1R_NONE, "CS1+DA100 devia ser NONE");
    CHECK(rd(1, 0, 0x7, &isw) == L1R_NONE, "CS1+DA111 devia ser NONE");
    CHECK(wr(1, 0, 0x7, &isw) == L1R_NONE, "CS1+DA111+WR devia ser NONE");
    /* Command Block, leitura: os 8 enderecos. */
    {
        static const gd_l1_reg_t want_rd[8] = {
            L1R_DATA, L1R_ERROR, L1R_INTREASON, L1R_SECTORNUM,
            L1R_BYTECOUNTL, L1R_BYTECOUNTH, L1R_DRIVESEL, L1R_STATUS
        };
        for (i = 0; i < 8; i++)
            CHECK(rd(0, 1, (unsigned)i, &isw) == want_rd[i] && isw == 0,
                  "leitura DA=%d devia ser %d (isw=%d)", i, want_rd[i], isw);
    }
    /* Command Block, escrita: DATA, FEATURES, (RO, RO), BC, BC, DRV, CMD. */
    {
        static const gd_l1_reg_t want_wr[8] = {
            L1R_DATA, L1R_FEATURES, L1R_NONE, L1R_NONE,
            L1R_BYTECOUNTL, L1R_BYTECOUNTH, L1R_DRIVESEL, L1R_COMMAND
        };
        for (i = 0; i < 8; i++)
            CHECK(wr(0, 1, (unsigned)i, &isw) == want_wr[i] && isw == 1,
                  "escrita DA=%d devia ser %d (isw=%d)", i, want_wr[i], isw);
    }

    /* ---------------------------------------------------------------- */
    CASE(91, "Larguras: 16 no Data, 8 no resto, 0 no NONE");
    CHECK(gd_l1_width(L1R_DATA) == 16, "DATA devia ser 16 bits");
    CHECK(gd_l1_width(L1R_STATUS) == 8, "STATUS devia ser 8 bits");
    CHECK(gd_l1_width(L1R_BYTECOUNTL) == 8, "BCNTL devia ser 8 bits");
    CHECK(gd_l1_width(L1R_ALTSTATUS) == 8, "ALTSTATUS devia ser 8 bits");
    CHECK(gd_l1_width(L1R_COMMAND) == 8, "COMMAND devia ser 8 bits");
    CHECK(gd_l1_width(L1R_NONE) == 0, "NONE devia ser 0 bits");

    /* ---------------------------------------------------------------- */
    CASE(92, "Despacho de escritas: o L2 recebe o que o fio diz");
    hostsim_init(&hs, &md.disc);
    gd_l1_write(&hs.dev, L1R_FEATURES, 0x01);
    CHECK(hs.dev.reg[GD_R_FEATURES] == 0x01, "FEATURES nao chegou ao L2");
    gd_l1_write(&hs.dev, L1R_BYTECOUNTL, 0x06);
    gd_l1_write(&hs.dev, L1R_BYTECOUNTH, 0x00);
    CHECK(hs.dev.reg[GD_R_BYTECOUNTL] == 0x06, "BCNTL nao chegou");
    gd_l1_write(&hs.dev, L1R_DRIVESEL, 0x00);
    CHECK(hs.dev.reg[GD_R_DRIVESEL] == 0x00, "DRIVESEL nao chegou");
    /* INTREASON e' read-only: escrever ignora, como o L2 faz. */
    hs.dev.reg[GD_R_INTREASON] = 0x00;
    gd_l1_write(&hs.dev, L1R_INTREASON, 0xff);
    CHECK(hs.dev.reg[GD_R_INTREASON] == 0x00, "INTREASON devia ignorar escrita");
    /* COMMAND NOP pelo despacho: igual ao caminho do hostsim. */
    gd_l1_write(&hs.dev, L1R_COMMAND, GD_CMD_NOP);
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "NOP mudou a fase");
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "NOP pos CHECK");
    /* Packet completo pelo despacho: 0xA0 + 6 palavras de NOP. */
    gd_l1_write(&hs.dev, L1R_COMMAND, GD_CMD_PACKET);
    for (i = 0; i < GD_PKT_WORDS; i++)
        gd_l1_write(&hs.dev, L1R_DATA, 0x0000);
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "packet NOP nao voltou a IDLE");
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "packet NOP pos CHECK");

    /* ---------------------------------------------------------------- */
    CASE(93, "Despacho de leituras: valores do L2 e o clear do INTRQ");
    hostsim_init(&hs, &md.disc);
    hs.dev.reg[GD_R_ERROR] = 0x40;
    CHECK(gd_l1_read(&hs.dev, L1R_ERROR) == 0x40, "ERROR nao veio do L2");
    /* STATUS limpa o INTRQ; ALTSTATUS nao. E' o efeito colateral que
     * mais importa no L1 (ver gd_l1_read). */
    hs.dev.irq = 1;
    (void)gd_l1_read(&hs.dev, L1R_STATUS);
    CHECK(hs.dev.irq == 0, "ler STATUS devia limpar o INTRQ");
    hs.dev.irq = 1;
    (void)gd_l1_read(&hs.dev, L1R_ALTSTATUS);
    CHECK(hs.dev.irq == 1, "ler ALTSTATUS nao pode limpar o INTRQ");

    /* ---------------------------------------------------------------- */
    CASE(94, "g1_dev_slow monta, espera CS0 e emite um evento por seleccao");
    for (i = 0; i < (int)(sizeof g1_dev_slow / sizeof g1_dev_slow[0]); i++)
        CHECK(pio_vm_decode(g1_dev_slow[i], &nm),
              "instrucao %d = 0x%04x nao reconhecida", i, g1_dev_slow[i]);
    CHECK(g1_dev_slow[0] == 0x2033, "wait 0 pin 19 = 0x%04x", g1_dev_slow[0]);
    CHECK(g1_dev_slow[5] == 0x20b3, "wait 1 pin 19 = 0x%04x", g1_dev_slow[5]);
    CHECK(g1_dev_slow[6] == 0x0000, "jmp idle = 0x%04x", g1_dev_slow[6]);
    {
        uint32_t b;
        pio_vm_load(&vm, g1_dev_slow, sizeof g1_dev_slow / sizeof g1_dev_slow[0], 0);
        vm.pins = ~0u;                 /* tudo inactivo: CS0 alto */
        pio_vm_run(&vm, 0xffff, 200);
        CHECK(vm.blocked == 1, "sem seleccao o programa tem de bloquear");
        CHECK(vm.pushed == 0, "sem seleccao nao ha eventos");
        /* O host selecciona: CS0 baixo + DA=111 + WR. */
        vm.pins &= ~(1u << SN_CS0);
        vm.pins |= (1u << SN_CS1) | (1u << SN_RD);
        vm.pins &= ~(1u << SN_WR);
        vm.pins |= 7u << SN_DA(0);
        b = vm.pushed;
        pio_vm_run(&vm, 0xffff, 40);
        CHECK(vm.pushed == b + 1, "uma seleccao = um evento (houve %u)",
              vm.pushed - b);
        CHECK(pio_vm_fifo_avail(&vm) == 1, "a FIFO devia ter o evento");
        CHECK(pio_vm_fifo_pop(&vm) == vm.pins, "o evento nao traz os pinos");
        /* Largar o CS rearma; sem largar nao ha segundo evento. */
        b = vm.pushed;
        pio_vm_run(&vm, 0xffff, 200);
        CHECK(vm.pushed == b, "com CS preso nao pode haver 2o evento");
        vm.pins |= (1u << SN_CS0);
        pio_vm_run(&vm, 0xffff, 40);
        CHECK(vm.blocked == 1, "largado o CS, volta a bloquear");
    }

    /* ---------------------------------------------------------------- */
    CASE(95, "Pinout: o L1 e o sniffer veem o mesmo bus");
    CHECK(SN_CS0 == 19 && SN_CS1 == 20, "CS0/CS1 mudaram?");
    CHECK(SN_RD == 21 && SN_WR == 22, "RD/WR mudaram?");
    CHECK(SN_DA(0) == 16, "DA0 mudou?");
    CHECK(SN_DD(0) == 0 && SN_DD(15) == 15, "DD mudou?");
    /* A unica regra do placeholder: fora do bus, para nao colidir. O
     * numero em si vai mudar com a placa, por isso nao se fixa aqui. */
    CHECK(GD_L1_PIN_INTRQ > (int)SN_LAST, "INTRQ dentro do bus colidiria");

    /* ---------------------------------------------------------------- */
    CASE(96, "RESET repoe a task file e desasserta as saidas");
    gd_l1_set_hw(&hw_fake);
    fake_init_seen = 0; fake_intrq_seen = -1;
    hostsim_init(&hs, &md.disc);
    hs.dev.reg[GD_R_ERROR] = 0x40;
    hs.dev.irq = 1;
    gd_l1_reset(&hs.dev);
    CHECK(hs.dev.reg[GD_R_STATUS] == GD_RESET_STATUS, "STATUS nao resetou");
    CHECK(hs.dev.reg[GD_R_ERROR] == GD_RESET_ERROR, "ERROR nao resetou");
    CHECK(fake_intrq_seen == 0, "RESET devia desassertar INTRQ");
    gd_l1_set_hw(NULL);   /* stubs: nao pode falhar */
    gd_l1_reset(&hs.dev);
    CHECK(hs.dev.reg[GD_R_STATUS] == GD_RESET_STATUS, "reset com stubs falhou");

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
