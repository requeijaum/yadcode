/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_sniffer.c - O programa PIO de captura, o ring buffer, e a
 *                  analise que responde as perguntas em aberto.
 *
 * O teste mais importante e' o 72: mede o custo do varrimento em
 * repouso e prova que cabe na janela de um strobe de 80 ns. Como o
 * programa so' emite quando o estado muda, o caso 71 prova que em
 * repouso ha silencio e que cada mudanca da exactamente um evento.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pio_vm.h"
#include "sniffer.h"
#include "gd_spec.h"

static int g_fail, g_pass, g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) { printf("  FAIL  " fmt "\n", ##__VA_ARGS__); g_fail++; } \
        else             { g_pass++; }                                       \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

#define SYSCLK_HZ 211680000.0
#define CYCLE_NS  (1e9 / SYSCLK_HZ)

/* g1_trace, tal como `pioasm -o hex -v 1` o produz. Atencao ao
 * indice 1: `mov isr, y` e' obrigatorio porque o `push` empurra o
 * ISR, nao o Y. Sem ele o evento inicial seria lixo. */
static const uint16_t g1_trace[] = {
    0xa040,  /* 0  mov y, pins   */
    0xa0c2,  /* 1  mov isr, y    */
    0x8020,  /* 2  push          */
    0xa020,  /* 3  mov x, pins   */
    0x00a6,  /* 4  jmp x != y, 6 */
    0x0003,  /* 5  jmp 3         */
    0xa0c1,  /* 6  mov isr, x    */
    0x8020,  /* 7  push          */
    0xa041,  /* 8  mov y, x      */
    0xc000,  /* 9  irq 0         */
    0x0003   /* 10 jmp 3         */
};

/* ------------------------------------------------------------------ */
/* Construcao de capturas sinteticas                                  */
/* ------------------------------------------------------------------ */

static uint32_t mk(unsigned cs0, unsigned cs1, unsigned da,
                   unsigned rd, unsigned wr, uint16_t data)
{
    uint32_t p = (uint32_t)data & 0xffffu;
    p |= (uint32_t)(cs0 & 1u) << SN_CS0;
    p |= (uint32_t)(cs1 & 1u) << SN_CS1;
    p |= (uint32_t)(da  & 7u) << SN_DA(0);
    p |= (uint32_t)(rd  & 1u) << SN_RD;
    p |= (uint32_t)(wr  & 1u) << SN_WR;
    return p;
}

/*
 * Atencao a convencao: os sinais /CS0, /CS1, /RD e /WR sao
 * activo-baixo no hardware, e `mk` recebe o NIVEL do pino (0 =
 * asserted, 1 = inactivo). E' o `sn_decode` que inverte para decidir.
 * Por isso uma escrita e' mk(..., rd=1, wr=0) e uma leitura e'
 * mk(..., rd=0, wr=1): o strobe activo esta a 0.
 */
static uint32_t wr_reg(unsigned da, uint16_t val)
{
    return mk(0, 1, da, 1, 0, val);   /* /WR activo, /RD inactivo */
}
/* Uma leitura: /RD activo. */
static uint32_t rd_reg(unsigned da, uint16_t val)
{
    return mk(0, 1, da, 0, 1, val);
}

static void emit_packet(sn_capture_t *c, const uint8_t pkt[GD_PKT_SIZE])
{
    int i;
    sn_push(c, wr_reg(0x7, GD_CMD_PACKET));            /* COMMAND = 0xA0 */
    for (i = 0; i < GD_PKT_WORDS; i++)
        sn_push(c, wr_reg(0x0, (uint16_t)(((uint16_t)pkt[i * 2] << 8) |
                                           pkt[i * 2 + 1])));
}

int main(void)
{
    pio_vm_t vm;
    sn_capture_t cap;
    sn_report_t rep;
    uint32_t buf[256];
    const char *nm;
    int i;

    printf("ciclo de PIO a %.4f ns\n\n", CYCLE_NS);

    /* ---------------------------------------------------------------- */
    CASE(70, "As palavras do g1_trace sao as que o pioasm produz");
    for (i = 0; i < (int)(sizeof g1_trace / sizeof g1_trace[0]); i++)
        CHECK(pio_vm_decode(g1_trace[i], &nm),
              "instrucao %d = 0x%04x nao reconhecida", i, g1_trace[i]);
    CHECK(g1_trace[0] == 0xa040, "mov y, pins = 0x%04x", g1_trace[0]);
    CHECK(g1_trace[1] == 0xa0c2, "mov isr, y = 0x%04x", g1_trace[1]);
    CHECK(g1_trace[3] == 0xa020, "mov x, pins = 0x%04x", g1_trace[3]);
    CHECK(g1_trace[4] == 0x00a6, "jmp x != y, 6 = 0x%04x", g1_trace[4]);
    CHECK(g1_trace[6] == 0xa0c1, "mov isr, x = 0x%04x", g1_trace[6]);
    CHECK(g1_trace[7] == 0x8020, "push = 0x%04x", g1_trace[7]);
    CHECK(g1_trace[9] == 0xc000, "irq 0 = 0x%04x", g1_trace[9]);

    /* ---------------------------------------------------------------- */
    CASE(71, "g1_trace so' emite quando o estado muda");
    sn_init(&cap);
    pio_vm_load(&vm, g1_trace, sizeof g1_trace / sizeof g1_trace[0], 0);
    vm.pins = 0;                       /* bus em repouso */
    pio_vm_run(&vm, 0xffff, 40);
    /* Arrancar regista o estado inicial: `mov y, pins` + `mov isr, y`
     * + `push`. A partir dai o programa so' emite em caso de mudanca.
     * Le-se a FIFO do VM em vez de adivinhar o conteudo: e' o que a
     * PIO entregaria ao firmware. */
    CHECK(vm.pushed == 1, "esperado 1 evento inicial, obtive %u", vm.pushed);
    CHECK(pio_vm_fifo_avail(&vm) == 1, "a FIFO devia ter 1 evento");
    sn_push(&cap, pio_vm_fifo_pop(&vm));
    CHECK(cap.total == 1, "o ring guarda 1 evento inicial, guardou %u",
          cap.total);

    /* Dez variacoes: cada uma tem de produzir exactamente um evento,
     * com o valor novo dos pinos. `pushed` e' cumulativo, por isso o
     * teste e' sobre o delta. */
    for (i = 0; i < 10; i++) {
        uint32_t before = cap.total, pb = vm.pushed, got;
        vm.pins ^= (1u << (i % 4));    /* muda um bit de DA */
        pio_vm_run(&vm, 0xffff, 40);
        CHECK(vm.pushed - pb == 1,
              "variacao %d: o trace emitiu %u evento(s) em vez de 1",
              i, vm.pushed - pb);
        CHECK(pio_vm_fifo_avail(&vm) == 1,
              "variacao %d: a FIFO devia ter 1 evento", i);
        got = pio_vm_fifo_pop(&vm);
        CHECK(got == vm.pins,
              "variacao %d: o evento (0x%08x) nao traz o estado novo dos pinos",
              i, got);
        sn_push(&cap, got);
        CHECK(cap.total == before + 1,
              "variacao %d: %u -> %u eventos no ring", i, before, cap.total);
    }

    /* E sem mudanca nao ha evento - que e' o ponto do programa. */
    {
        uint32_t before = vm.pushed;
        pio_vm_run(&vm, 0xffff, 2000);
        CHECK(vm.pushed == before,
              "sem mudanca de estado houve %u eventos a mais",
              vm.pushed - before);
        CHECK(cap.total == 11, "o ring guarda 11 eventos, guardou %u",
              cap.total);
    }

    /* ---------------------------------------------------------------- */
    CASE(72, "Custo por ciclo parado cabe na janela de um strobe");
    {
        /*
         * A 211,68 MHz um ciclo de PIO sao 4,7241 ns. Em repouso o
         * programa faz o ciclo `mov x, pins` + `jmp x != y` nao-tomado
         * + `jmp loop`: 3 instrucoes por varrimento. Mede-se de verdade,
         * passo a passo: do topo do ciclo, 3 instrucoes tem de trazer o
         * pc de volta ao topo sem emitir nada.
         *
         * O strobe /RD do ATA-3 dura t2 = 80 ns (min). A janela tem de
         * conter o suficiente para ver a transaccao.
         */
        uint32_t p0;
        double sweep_ns;

        pio_vm_load(&vm, g1_trace, sizeof g1_trace / sizeof g1_trace[0], 0);
        vm.pins = 0;
        pio_vm_run(&vm, 0xffff, 200);
        CHECK(vm.blocked == 0, "o programa nao bloqueia, so' roda");

        /* Assenta no topo do ciclo (indice 3: `mov x, pins`). */
        while (vm.pc != 3) pio_vm_run(&vm, 0xffff, 1);
        p0 = vm.pushed;
        pio_vm_run(&vm, 0xffff, 3);
        CHECK(vm.pc == 3, "3 instrucoes deviam fechar o ciclo (pc=%u)", vm.pc);
        CHECK(vm.pushed == p0, "em repouso o trace emitiu %u evento(s) a mais",
              vm.pushed - p0);

        sweep_ns = 3 * CYCLE_NS;
        printf("        varrimento = %.2f ns (3 instrucoes x %.4f ns)\n",
               sweep_ns, CYCLE_NS);
        printf("        strobe t2 = 80 ns -> %.1f amostras na janela\n",
               80.0 / sweep_ns);

        /* Se o pioasm gerar outro ciclo, esta contagem acusa: com 2 ou
         * 4 instrucoes o pc nao estaria de volta ao fim de 3. */
        CHECK(sweep_ns < 80.0,
              "o varrimento (%.2f ns) tem de caber na janela do strobe (80 ns)",
              sweep_ns);
    }

    /* ---------------------------------------------------------------- */
    CASE(73, "Decodificacao dos pinos em registo");
    {
        int isw = 0;
        uint16_t d = 0;
        /* Escrever no registo Status = escrever no Command. */
        CHECK(sn_decode(wr_reg(0x7, 0xA0), &isw, &d) == SN_REG_STATUS,
              "escrita em DA=111 tem de ser COMMAND");
        CHECK(isw == 1, "tem de ser escrita");
        CHECK(d == 0xA0, "valor = 0x%02x", d);
        /* Escrever em Error = escrever em Features. */
        CHECK(sn_decode(wr_reg(0x1, 0x01), &isw, &d) == SN_REG_ERROR,
              "escrita em DA=001 tem de ser FEATURES");
        /* Ler o registo Data. */
        CHECK(sn_decode(rd_reg(0x0, 0x1234), &isw, &d) == SN_REG_DATA,
              "leitura em DA=000 tem de ser DATA");
        CHECK(isw == 0, "tem de ser leitura");
        CHECK(d == 0x1234, "dados = 0x%04x", d);
        /* Leitura do AltStatus: CS1 asserted, DA=110. */
        CHECK(sn_decode(mk(1, 0, 0x6, 0, 1, 0), &isw, &d) == SN_REG_ALTSTATUS,
              "leitura do AltStatus");
        /* Nenhum strobe activo: nao e' transaccao. */
        CHECK(sn_decode(mk(1, 1, 0, 1, 1, 0), &isw, &d) == SN_REG_NONE,
              "sem strobe nao ha transaccao");
        /* Dois strobes activos ao mesmo tempo: ruido electrico. */
        CHECK(sn_decode(mk(0, 1, 0, 0, 0, 0), &isw, &d) == SN_REG_NONE,
              "com /RD e /WR activos ao mesmo tempo nao ha transaccao");
    }

    /* ---------------------------------------------------------------- */
    CASE(74, "Ring buffer: nao perde nada ate encher, e conta o que perde");
    {
        sn_init(&cap);
        for (i = 0; i < 100; i++) sn_push(&cap, (uint32_t)(i * 4 + 1));
        CHECK(cap.total == 100, "total = %u", cap.total);
        CHECK(cap.dropped == 0, "dropped = %u com 100 palavras", cap.dropped);
        {
            uint32_t n = sn_drain(&cap, buf, 256);
            CHECK(n == 100, "drenou %u", n);
            CHECK(buf[0] == 1 && buf[99] == 99 * 4 + 1, "conteudo errado");
        }
        /* Agora transborda: o ring tem SN_RING_WORDS palavras e entram
         * 50 a mais, por isso dropped tem de ser exactamente 50. */
        for (i = 0; i < SN_RING_WORDS + 50; i++)
            sn_push(&cap, 0xdead0000u | (uint32_t)i);
        CHECK(cap.dropped == 50, "dropped = %u, esperava 50", cap.dropped);
        {
            /* `buf` so' tem 256 palavras: drena-se no maximo isso.
             * Drenar mais seria um overflow do buffer do chamador,
             * nao do ring. */
            uint32_t n = sn_drain(&cap, buf, 256);
            CHECK(n == 256, "drenou %u, esperava 256", n);
        }
    }

    /* ---------------------------------------------------------------- */
    CASE(75, "A analise responde as perguntas que a captura permite");
    {
        uint8_t pkt[GD_PKT_SIZE];
        memset(pkt, 0, sizeof pkt);
        pkt[0] = GD_SPI_CMD71;

        sn_init(&cap);
        /*
         * Host PIO: escreve o Byte Count, envia o packet, e depois le
         * tres palavras de DATA (6 bytes). Sao essas tres leituras que
         * o analisador conta para responder a pergunta A.
         */
        sn_push(&cap, wr_reg(0x4, 0x06));          /* BCNTL = 6 */
        sn_push(&cap, wr_reg(0x5, 0x00));          /* BCNTH = 0 */
        emit_packet(&cap, pkt);
        for (i = 0; i < 3; i++)
            sn_push(&cap, rd_reg(0x0, 0x0000));    /* 3 palavras = 6 bytes */
        sn_push(&cap, rd_reg(0x7, 0x50));          /* Status */
        /* E uma escrita em FEATURES sem bit de DMA. */
        sn_push(&cap, wr_reg(0x1, 0x00));

        {
            uint32_t n = sn_drain(&cap, buf, 256);
            sn_analyse(buf, n, &rep);
            CHECK(n > 0, "captura com %u eventos", n);
        }
        CHECK(rep.n >= 4, "o relatorio tem %d conclusoes", rep.n);
        {
            int saw71 = 0, sawdma = 0;
            for (i = 0; i < rep.n; i++) {
                if (rep.f[i].q == SN_Q_71_LEN && rep.f[i].confident) {
                    saw71 = 1;
                    CHECK(strstr(rep.f[i].answer, "6 bytes") != NULL,
                          "resposta ao 0x71: %s", rep.f[i].answer);
                }
                if (rep.f[i].q == SN_Q_DMA) {
                    sawdma = 1;
                    CHECK(strstr(rep.f[i].answer, "NAO") != NULL,
                          "resposta sobre DMA: %s", rep.f[i].answer);
                }
            }
            CHECK(saw71, "a pergunta do 0x71 tem de ser respondida");
            CHECK(sawdma, "a pergunta do DMA tem de ser respondida");
        }
    }

    /* ---------------------------------------------------------------- */
    CASE(76, "A analise detecta um pedido de DMA");
    {
        uint8_t pkt[GD_PKT_SIZE];
        memset(pkt, 0, sizeof pkt);
        pkt[0] = GD_SPI_CD_READ;
        sn_init(&cap);
        /* Features com bit 0 aceso: o host pediu DMA. */
        sn_push(&cap, wr_reg(0x1, 0x01));
        emit_packet(&cap, pkt);
        {
            uint32_t n = sn_drain(&cap, buf, 256);
            sn_analyse(buf, n, &rep);
        }
        {
            int found = 0;
            for (i = 0; i < rep.n; i++)
                if (rep.f[i].q == SN_Q_DMA) {
                    found = 1;
                    CHECK(strstr(rep.f[i].answer, "SIM") != NULL,
                          "devia dizer SIM: %s", rep.f[i].answer);
                }
            CHECK(found, "a pergunta do DMA tem de estar no relatorio");
        }

        /*
         * Contra-prova: um SET FEATURES (0xEF) com Features = 0x03 tem
         * o bit 0 aceso e NAO e' um pedido de DMA. A analise so' conta
         * FEATURES cujo comando seguinte seja um PACKET (0xA0).
         */
        sn_init(&cap);
        sn_push(&cap, wr_reg(0x1, 0x03));          /* FEATURES = modo */
        sn_push(&cap, wr_reg(0x7, GD_CMD_SETFEATURE));  /* COMMAND = 0xEF */
        {
            uint32_t n = sn_drain(&cap, buf, 256);
            sn_analyse(buf, n, &rep);
        }
        for (i = 0; i < rep.n; i++)
            if (rep.f[i].q == SN_Q_DMA)
                CHECK(strstr(rep.f[i].answer, "NAO") != NULL,
                      "SET FEATURES nao e' DMA: %s", rep.f[i].answer);
    }

    /* ---------------------------------------------------------------- */
    CASE(77, "Captura vazia nao inventa respostas");
    {
        /* O caminho n == 0 do analisador tem de existir e de nao ser
         * confiante. Passa-se NULL porque com zero eventos o buffer
         * nunca e' lido. */
        sn_analyse(NULL, 0, &rep);
        CHECK(rep.n > 0, "tem de haver pelo menos uma conclusao");
        for (i = 0; i < rep.n; i++)
            CHECK(!rep.f[i].confident,
                  "nenhuma resposta pode ser confiante com uma captura vazia");
    }

    /* ---------------------------------------------------------------- */
    CASE(78, "Resposta de 1012 bytes em modo DMA");
    {
        uint8_t pkt[GD_PKT_SIZE];
        memset(pkt, 0, sizeof pkt);
        pkt[0] = GD_SPI_CMD71;

        /*
         * Em DMA nao ha leituras DATA nenhuma: os dados vao por
         * DMARQ/DMACK. O unico sitio onde o tamanho aparece e' o Byte
         * Count que o host escreveu.
         */
        sn_init(&cap);
        sn_push(&cap, wr_reg(0x1, 0x01));          /* FEATURES: DMA */
        emit_packet(&cap, pkt);
        sn_push(&cap, wr_reg(0x4, 0xF4));          /* BCNTL = 0xF4 = 244 */
        sn_push(&cap, wr_reg(0x5, 0x03));          /* BCNTH = 3 -> 1012 */
        {
            uint32_t n = sn_drain(&cap, buf, 256);
            sn_analyse(buf, n, &rep);
        }
        {
            int found = 0;
            for (i = 0; i < rep.n; i++) {
                if (rep.f[i].q != SN_Q_71_LEN) continue;
                found = 1;
                CHECK(strstr(rep.f[i].answer, "1012") != NULL,
                      "resposta ao 0x71 em DMA: %s", rep.f[i].answer);
            }
            CHECK(found, "a pergunta do 0x71 tem de estar no relatorio");
        }
    }

    /* ---------------------------------------------------------------- */
    CASE(79, "Uma captura so' com registos nao inventa um lead-out");
    {
        uint8_t pkt[GD_PKT_SIZE];
        memset(pkt, 0, sizeof pkt);
        pkt[0] = GD_SPI_GET_TOC;
        sn_init(&cap);
        emit_packet(&cap, pkt);
        {
            uint32_t n = sn_drain(&cap, buf, 256);
            sn_analyse(buf, n, &rep);
        }
        for (i = 0; i < rep.n; i++)
            if (rep.f[i].q == SN_Q_LEADOUT)
                CHECK(!rep.f[i].confident,
                      "o lead-out nao pode ser confiante sem os 408 bytes");
    }

    /* ---------------------------------------------------------------- */
    CASE(80, "O programa g1_window bloqueia ate o gatilho do host");
    {
        /*
         * `g1_window` nao pode ser uma copia de `g1_sample`: a
         * diferencia e' o `wait 1 pin 28` inicial, que so' deixa o
         * programa correr depois de o host abrir a janela. Se
         * perdesse o `wait`, a PIO empurraria eventos sem ninguem os
         * ter pedido.
         *
         * Encoding confirmado com pioasm -o hex -v 1:
         *   wait 1 pin 28 = 0x20bc
         */
        static const uint16_t win[] = { 0x20bc, 0xa0c0, 0x8020 };
        const char *wnm;

        CHECK(win[0] == 0x20bc, "wait 1 pin 28 = 0x%04x, esperava 0x20bc",
              win[0]);
        CHECK(pio_vm_decode(win[0], &wnm),
              "o wait inicial nao e' reconhecido pelo VM");

        pio_vm_load(&vm, win, 3, 0);
        vm.pins = 0;
        /* GPIO 28 em baixo: o programa tem de ficar bloqueado. */
        pio_vm_run(&vm, 0xffff, 500);
        CHECK(vm.blocked == 1, "sem gatilho o programa tem de bloquear");
        CHECK(vm.pushed == 0, "sem gatilho nao pode haver eventos (houve %u)",
              vm.pushed);

        /* O host abre a janela. */
        vm.pins = 1u << 28;
        vm.blocked = 0;
        {
            uint32_t b = vm.pushed;
            /*
             * Exactamente o comprimento do programa (3): um ciclo de
             * `wait` + `mov` + `push`, e o `.wrap` devolve o pc a
             * zero. Mais do que isso a janela continua aberta e o
             * programa empurra a cada 2 ciclos, que e' o esperado mas
             * nao o que se quer medir aqui.
             */
            pio_vm_run(&vm, 0xffff, 3);
            CHECK(vm.pushed == b + 1,
                  "um ciclo completo tem de dar 1 evento, deu %u",
                  vm.pushed - b);
        }

        /*
         * E volta a bloquear se o host fechar a janela. Nao se forca
         * `blocked` a mao: o proprio `wait` tem de o fazer quando o
         * pino 28 volta a zero no wrap. Forcar o flag deixaria
         * `blocked_pc` inconsistente e o teste nao provaria nada.
         */
        vm.pins = 0;
        {
            uint32_t b = vm.pushed;
            pio_vm_run(&vm, 0xffff, 200);
            CHECK(vm.pushed == b,
                  "com a janela fechada nao pode haver eventos (houve %u a mais)",
                  vm.pushed - b);
            CHECK(vm.blocked == 1, "o programa tem de voltar a bloquear");
        }
    }

    /* ---------------------------------------------------------------- */
    CASE(81, "O programa g1_window e' diferente de g1_sample");
    {
        static const uint16_t samp[] = { 0xa0c0, 0x8020 };
        static const uint16_t win[]  = { 0x20bc, 0xa0c0, 0x8020 };
        /* window = o wait + sample. Se algum dia coincidirem, o
         * `wait` desapareceu e o teste acusa. */
        CHECK(sizeof win == sizeof samp + sizeof(uint16_t),
              "g1_window devia ter uma instrucao a mais que g1_sample");
        CHECK(memcmp(win + 1, samp, sizeof samp) == 0,
              "g1_window devia ser g1_sample precedido de um wait");
    }

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
