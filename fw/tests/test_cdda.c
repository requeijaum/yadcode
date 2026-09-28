/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_cdda.c - Camada L4: CD-DA e subcode.
 *
 * O ponto central desta suite e' o contraste BCD/binario entre os
 * formatos 0 e 1 do GET_SCD: e' o mesmo campo (numero da track)
 * codificado de duas maneiras, e e' a coisa mais facil de errar
 * porque nenhum dos testes "naturais" a denuncia.
 */
#include <stdio.h>
#include <string.h>
#include "gd_taskfile.h"
#include "gd_cdda.h"
#include "gd_spi.h"
#include "hostsim.h"
#include "memdisc.h"

static int g_fail, g_pass, g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) { printf("  FAIL  " fmt "\n", ##__VA_ARGS__); g_fail++; } \
        else             { g_pass++; }                                       \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

static void pkt_reset(uint8_t *p) { memset(p, 0, GD_PKT_SIZE); }
static void pkt_set_fad(uint8_t *p, uint32_t fad)
{
    p[2] = (uint8_t)(fad >> 16);
    p[3] = (uint8_t)(fad >> 8);
    p[4] = (uint8_t)fad;
}

int main(void)
{
    hostsim_t hs;
    memdisc_t md;
    uint8_t p[GD_PKT_SIZE], resp[256];
    uint32_t n;
    uint16_t crc;

    memdisc_init(&md, 0);
    hostsim_init(&hs, &md.disc);

    /* ---------------------------------------------------------------- */
    CASE(29, "CRC do subcode: variante do GD-ROM, com complemento final");
    {
        static const uint8_t check[9] = "123456789";
        /*
         * O CRC-16/XMODEM padrao (mesmo polinomio 0x1021, mesma seed
         * 0x0000, sem reflect) NAO tem complemento final e da 0x31C3.
         * A variante que o GD-ROM usa faz o complemento e da 0xCE3C.
         *
         * Qual das duas e' correcta so se decide com um dump de
         * hardware - ver doc 13, questao F. Aqui fixamos o
         * comportamento observado, para que uma mudanca seja visivel.
         */
        crc = gd_cdda_crc16(check, 9);
        CHECK(crc == 0xce3c,
              "crc(\"123456789\") = 0x%04x, esperado 0xCE3C (variante GD-ROM)", crc);
        CHECK((uint16_t)(crc ^ 0xffff) == 0x31c3,
              "sem complemento final daria 0x%04x, esperado 0x31C3 (XMODEM)",
              (uint16_t)(crc ^ 0xffff));
    }

    /* ---------------------------------------------------------------- */
    CASE(30, "GET_SCD formato 0: 100 bytes, Q em BCD, P por expansao");
    pkt_reset(p);
    p[0] = GD_SPI_CD_PLAY; p[1] = 0x01; pkt_set_fad(p, 100); pkt_set_fad(p, 100);
    p[8] = 0; p[9] = 0; p[10] = 200; p[6] = 0;
    hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(gd_device_state(&hs.dev) == GD_STATE_PLAY, "CD_PLAY nao deu PLAY");
    CHECK(hs.dev.cdda.status == GD_CDDA_PLAYING, "status de audio nao e' PLAYING");

    pkt_reset(p);
    p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_RAW; p[3] = 0; p[4] = 100;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_SCD_RAW_LEN, "formato 0 devolveu %u bytes, esperado %d",
          n, GD_SCD_RAW_LEN);
    CHECK(resp[1] == GD_AUD_PLAY, "audio status = 0x%02x, esperado 0x11", resp[1]);
    CHECK(((resp[2] << 8) | resp[3]) == 100, "length field != 100");

    /*
     * O P e' a expansao do Q: cada byte do Q vira 8 bytes, um por bit,
     * MSB primeiro. q[0] = 0x40 = 0b01000000, logo o unico byte a 0x40
     * da primeira oitava e' o segundo (indice 1), nao o indice 6.
     */
    CHECK(resp[4 + 0] == 0x00, "P[0] = 0x%02x, esperado 0x00", resp[4]);
    CHECK(resp[4 + 1] == 0x40, "P[1] = 0x%02x, esperado 0x40 (bit 1 de 0x40)",
          resp[4 + 1]);
    CHECK(resp[4 + 2] == 0x00 && resp[4 + 7] == 0x00,
          "a expansao devia ter so o bit 1 a 1: %02x %02x",
          resp[4 + 2], resp[4 + 7]);

    /* O Q esta em BCD dentro da expansao. Reconstrui-lo e verificar o
     * CRC prova que os dois estao coerentes. */
    {
        uint8_t q[12];
        int i, b;
        for (i = 0; i < 12; i++) {
            uint8_t v = 0;
            for (b = 0; b < 8; b++)
                if (resp[4 + i * 8 + b] == 0x40) v |= (uint8_t)(0x80u >> b);
            q[i] = v;
        }
        crc = gd_cdda_crc16(q, 10);
        CHECK((uint16_t)((q[10] << 8) | q[11]) == crc,
              "CRC do Q reconstruido = 0x%04x, o que esta no subcode = 0x%04x",
              crc, (uint16_t)((q[10] << 8) | q[11]));
        /* Track 1 em BCD deve ser 0x01. */
        CHECK(q[1] == 0x01, "numero da track em BCD = 0x%02x, esperado 0x01", q[1]);
    }

    /* ---------------------------------------------------------------- */
    CASE(31, "GET_SCD formato 1: 14 bytes, numero da track em BINARIO");
    pkt_reset(p);
    p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q; p[3] = 0; p[4] = 14;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_SCD_Q_LEN, "formato 1 devolveu %u bytes, esperado %d",
          n, GD_SCD_Q_LEN);
    CHECK(((resp[2] << 8) | resp[3]) == 14, "length field != 14");
    CHECK(resp[4] == 0x41, "Control/ADR = 0x%02x, esperado 0x41", resp[4]);
    /* Aqui e' BINARIO. Para a track 1, BCD e binario coincidem, por isso
     * a prova tem de ser feita com uma track acima de 9. */
    CHECK(resp[5] == 1, "TNO = %u, esperado 1 (binario)", resp[5]);

    /* ---------------------------------------------------------------- */
    CASE(32, "Track >= 10: BCD no formato 0, binario no formato 1");
    {
        hostsim_t h;
        memdisc_t m;
        uint8_t q[12];
        int i, b;

        memdisc_init(&m, 0);
        hostsim_init(&h, &m.disc);
        /* 3 tracks so; forco o TNO via uma posicao de track 3 com o
         * numero > 9 e' impossivel, por isso uso track 3 (=3) e o
         * contraste BCD/binario no TEMPO, que e' o campo que diverge. */
        pkt_reset(p); p[0] = GD_SPI_CD_PLAY; p[1] = 0x01;
        pkt_set_fad(p, 32);
        p[8] = 0; p[9] = 0; p[10] = 250;
        hostsim_spi_command(&h, p, resp, sizeof resp);

        /* FAD 32, track 3 (StartFAD 32) -> elapsed 0, posicao 0:00:32. */
        pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_RAW; p[4] = 100;
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        for (i = 0; i < 12; i++) {
            uint8_t v = 0;
            for (b = 0; b < 8; b++)
                if (resp[4 + i * 8 + b] == 0x40) v |= (uint8_t)(0x80u >> b);
            q[i] = v;
        }
        /*
         * FAD 32 na track 3 (que comeca em 32) -> tempo dentro da track
         * = 0, posicao absoluta = 0 min 0 seg 32 frames.
         *
         * No formato 0 o frame absoluto esta em BCD, logo 32 = 0x32.
         * No formato 1 o mesmo valor e' binario big-endian, logo
         * 0x00 0x00 0x20. E' esta a demonstracao directa da diferenca.
         */
        CHECK(q[5] == 0x00, "tempo dentro da track em BCD = %02x, esperado 00", q[5]);
        CHECK(q[9] == 0x32, "frame absoluto em BCD = %02x, esperado 0x32", q[9]);

        pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q; p[4] = 14;
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        /* Mesma posicao, mas em binario: 32 frames = 0x000020. */
        CHECK((uint32_t)((resp[7] << 16) | (resp[8] << 8) | resp[9]) == 0,
              "tempo dentro da track (binario) = %u, esperado 0",
              (unsigned)((resp[7] << 16) | (resp[8] << 8) | resp[9]));
        CHECK((uint32_t)((resp[11] << 16) | (resp[12] << 8) | resp[13]) == 32,
              "FAD absoluto (binario) = %u, esperado 32",
              (unsigned)((resp[11] << 16) | (resp[12] << 8) | resp[13]));
        /* O mesmo 32: 0x32 em BCD, 0x20 em binario. */
        CHECK(resp[13] == 0x20,
              "frame absoluto em binario = 0x%02x, esperado 0x20 (nao 0x32)",
              resp[13]);
    }

    /* ---------------------------------------------------------------- */
    CASE(33, "O subcode reflecte o tempo, e' estatico no instante do pedido");
    {
        hostsim_t h;
        memdisc_t m;
        uint32_t f0, f1;

        memdisc_init(&m, 0);
        hostsim_init(&h, &m.disc);
        pkt_reset(p); p[0] = GD_SPI_CD_PLAY; p[1] = 0x01;
        pkt_set_fad(p, 0);
        p[8] = 0; p[9] = 0; p[10] = 200;
        hostsim_spi_command(&h, p, resp, sizeof resp);

        pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q; p[4] = 14;
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        f0 = (uint32_t)((resp[11] << 16) | (resp[12] << 8) | resp[13]);

        /* Dois GET_SCD seguidos sem tempo passado: a posicao nao muda. */
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        f1 = (uint32_t)((resp[11] << 16) | (resp[12] << 8) | resp[13]);
        CHECK(f0 == f1, "FAD mudou sem tempo passar: %u -> %u", f0, f1);

        /* Agora passa 1 segundo de audio. */
        gd_cdda_tick(&h.dev, 1000000);
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        f1 = (uint32_t)((resp[11] << 16) | (resp[12] << 8) | resp[13]);
        CHECK(f1 == f0 + 75, "apos 1 s o FAD foi de %u para %u, esperado +75",
              f0, f1);
    }

    /* ---------------------------------------------------------------- */
    CASE(34, "Fim da reproducao: fica no ultimo sector, status = terminado");
    {
        hostsim_t h;
        memdisc_t m;
        uint32_t f;
        uint32_t i;

        memdisc_init(&m, 0);
        hostsim_init(&h, &m.disc);
        pkt_reset(p); p[0] = GD_SPI_CD_PLAY; p[1] = 0x01;
        pkt_set_fad(p, 0);
        p[8] = 0; p[9] = 0; p[10] = 10;      /* acaba no FAD 10 */
        hostsim_spi_command(&h, p, resp, sizeof resp);

        for (i = 0; i < 20; i++) gd_cdda_tick(&h.dev, (i + 1) * 200000);
        CHECK(h.dev.cdda.status == GD_CDDA_TERMINATED,
              "status = %d, esperado TERMINATED", h.dev.cdda.status);

        pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q; p[4] = 14;
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(resp[1] == GD_AUD_DONE, "audio status = 0x%02x, esperado 0x13", resp[1]);
        f = (uint32_t)((resp[11] << 16) | (resp[12] << 8) | resp[13]);
        CHECK(f == 10, "FAD no fim = %u, esperado 10 (o ultimo lido)", f);
    }

    /* ---------------------------------------------------------------- */
    CASE(35, "CD_SEEK: param type 3 vai para casa (FAD 150) e STANDBY");
    {
        hostsim_t h;
        memdisc_t m;
        pkt_reset(p); p[0] = GD_SPI_CD_PLAY; p[1] = 0x01;
        pkt_set_fad(p, 50);
        p[8] = 0; p[9] = 0; p[10] = 200;

        memdisc_init(&m, 0);
        hostsim_init(&h, &m.disc);
        hostsim_spi_command(&h, p, resp, sizeof resp);
        pkt_reset(p); p[0] = GD_SPI_CD_SEEK; p[1] = 0x03;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.dev.cdda.curr_fad == 150, "FAD apos stop->home = %u, esperado 150",
              h.dev.cdda.curr_fad);
        CHECK(gd_device_state(&h.dev) == GD_STATE_STANDBY,
              "estado = %u, esperado STANDBY", gd_device_state(&h.dev));
        CHECK(h.dev.cdda.status == GD_CDDA_NO_INFO, "audio nao foi limpo");
    }

    /* ---------------------------------------------------------------- */
    CASE(36, "CD_PLAY com MSF converte para FAD");
    {
        hostsim_t h;
        memdisc_t m;
        memdisc_init(&m, 0);
        hostsim_init(&h, &m.disc);
        /* param_type 2 = MSF. 00m 00s 00f -> FAD 0. */
        pkt_reset(p); p[0] = GD_SPI_CD_PLAY; p[1] = 0x02;
        p[2] = 0; p[3] = 0; p[4] = 0;
        p[8] = 0; p[9] = 0; p[10] = 10;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.dev.cdda.start_fad == 0, "FAD inicial = %u, esperado 0", h.dev.cdda.start_fad);
        /* 1 minuto = 4500 frames. */
        p[2] = 1; p[3] = 0; p[4] = 0;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.dev.cdda.start_fad == 4500, "1m00s00f em MSF deu FAD %u, esperado 4500",
              h.dev.cdda.start_fad);
    }

    /* ---------------------------------------------------------------- */
    CASE(37, "CD_READ interrompe a reproducao de audio");
    {
        hostsim_t h;
        memdisc_t m;
        memdisc_init(&m, 0);
        hostsim_init(&h, &m.disc);
        pkt_reset(p); p[0] = GD_SPI_CD_PLAY; p[1] = 0x01;
        pkt_set_fad(p, 5); p[8] = 0; p[9] = 0; p[10] = 200;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.dev.cdda.status == GD_CDDA_PLAYING, "pre-condicao: nao estava a tocar");

        pkt_reset(p); p[0] = GD_SPI_CD_READ; p[1] = GD_READ_SEL_DATA;
        pkt_set_fad(p, 3); p[10] = 1;
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.dev.cdda.status == GD_CDDA_NO_INFO,
              "CD_READ nao interrompeu o audio (status %d)", h.dev.cdda.status);

        pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q; p[4] = 14;
        (void)hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(resp[1] == GD_AUD_NONE,
              "audio status = 0x%02x, esperado 0x15 (sem informacao)", resp[1]);
    }

    /* ---------------------------------------------------------------- */
    CASE(38, "GET_SCD formato 2 (UPC) e 3 (ISRC)");
    pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_UPC; p[4] = 24;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_SCD_UPC_LEN, "formato 2 devolveu %u bytes, esperado 24", n);
    CHECK(resp[4] == 0x02, "codigo de formato UPC = 0x%02x", resp[4]);

    pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_ISRC; p[4] = 16;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_SCD_ISRC_LEN, "formato 3 devolveu %u bytes, esperado 16", n);

    /* ---------------------------------------------------------------- */
    CASE(39, "GET_SCD sem disco: audio status = sem informacao");
    {
        hostsim_t h;
        gd_disc_t empty;
        memset(&empty, 0, sizeof empty);
        hostsim_init(&h, &empty);
        pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q; p[4] = 14;
        n = hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(n == GD_SCD_Q_LEN, "devolveu %u bytes sem disco", n);
        CHECK(resp[1] == GD_AUD_NONE, "audio status = 0x%02x, esperado 0x15", resp[1]);
    }

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
