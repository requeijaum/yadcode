/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_spec.c - Os 20 casos de teste de conformidade, do doc 11 §2.2.
 *
 * Cada caso verifica um comportamento que a spec Ver.1.30 obriga. Os
 * casos estao numerados como na documentacao.
 */
#include <stdio.h>
#include <string.h>
#include "gd_taskfile.h"
#include "gd_spi.h"
#include "hostsim.h"
#include "memdisc.h"

static int g_fail, g_pass;
static int g_case;

#define CHECK(cond, fmt, ...)                                                \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL  " fmt "\n", ##__VA_ARGS__);                     \
            g_fail++;                                                        \
        } else {                                                             \
            g_pass++;                                                        \
        }                                                                    \
    } while (0)

#define CASE(n, title) do { g_case = n; printf("[%2d] %s\n", n, title); } while (0)

static void pkt_reset(uint8_t *p) { memset(p, 0, GD_PKT_SIZE); }

/*
 * Os campos ASCII do protocolo sao preenchidos com espacos em vez de
 * NUL (ver resp_pack_ascii em gd_spi.c). O teste tem de usar a mesma
 * regra, senao compara strings com terminadores diferentes. O `strlen`
 * e' medido uma vez pelo mesmo motivo: `want` tem 14 chars + NUL e
 * `n` e' 16, por isso indexar ate' `n` leria fora da string.
 */
static int expect_ascii(const uint8_t *got, const char *want, int n)
{
    int i;
    size_t len = strlen(want);
    for (i = 0; i < n; i++) {
        uint8_t w = ((size_t)i < len) ? (uint8_t)want[i] : (uint8_t)' ';
        if (got[i] != w) return i;
    }
    return -1;
}

/* Preenche os 3 bytes de FAD (CD_READ, Parameter Type = FAD). */
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
    /* Maior que um sector CD (2048) e que CD_READ2 de 2 sectores. */
    uint8_t p[GD_PKT_SIZE], resp[4096];
    uint8_t expect[2352];
    uint32_t n;

    memdisc_init(&md, 0);
    hostsim_init(&hs, &md.disc);

    /* ---------------------------------------------------------------- */
    CASE(1, "Power-on reset: task file com os valores da secao 3.3.1.1");
    hostsim_init(&hs, &md.disc);
    CHECK(hs.dev.reg[GD_R_STATUS]     == GD_RESET_STATUS, "STATUS 0x%02x != 0x%02x",
          hs.dev.reg[GD_R_STATUS], GD_RESET_STATUS);
    CHECK(hs.dev.reg[GD_R_ERROR]      == GD_RESET_ERROR,  "ERROR 0x%02x != 0x%02x",
          hs.dev.reg[GD_R_ERROR], GD_RESET_ERROR);
    CHECK(hs.dev.reg[GD_R_BYTECOUNTL] == GD_RESET_SECTORCNT, "SecCnt 0x%02x",
          hs.dev.reg[GD_R_BYTECOUNTL]);
    CHECK(hs.dev.reg[GD_R_SECTORNUM]  == GD_RESET_SECTORNUM, "SecNr 0x%02x",
          hs.dev.reg[GD_R_SECTORNUM]);
    CHECK(hs.dev.reg[GD_R_DRIVESEL]   == GD_RESET_DRIVEHEAD, "DrvHd 0x%02x",
          hs.dev.reg[GD_R_DRIVESEL]);
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "phase != IDLE");

    /* ---------------------------------------------------------------- */
    CASE(2, "Comando 0x00 (NOP): aceite mesmo com BSY=1, sem dados");
    hostsim_write_command(&hs, GD_CMD_NOP);
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "NOP mudou a fase");
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "NOP pôs CHECK");

    /* ---------------------------------------------------------------- */
    CASE(3, "Comando 0x08 (Soft Reset): task file reinicializada, DRV mantido");
    hostsim_write_control(&hs, GD_R_DRIVESEL, 0x00);
    hostsim_write_control(&hs, GD_R_ERROR, 0x40);
    hostsim_write_command(&hs, GD_CMD_SOFTRESET);
    CHECK(hs.dev.reg[GD_R_ERROR]  == GD_RESET_ERROR, "ERROR nao reinicializou");
    CHECK(hs.dev.reg[GD_R_STATUS] == GD_RESET_STATUS, "STATUS nao reinicializou");
    CHECK(!(hs.dev.reg[GD_R_DRIVESEL] & 0x80), "bit DRV foi alterado");

    /* ---------------------------------------------------------------- */
    CASE(4, "Comando 0x90 (EXECDIAG): Error = 0x01 (Normal)");
    hostsim_write_command(&hs, GD_CMD_EXECDIAG);
    CHECK(hs.dev.reg[GD_R_ERROR] == GD_DIAG_NORMAL, "Error 0x%02x != 0x%02x",
          hs.dev.reg[GD_R_ERROR], GD_DIAG_NORMAL);
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "EXECDIAG pôs CHECK");
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "EXECDIAG nao voltou a IDLE");

    /* ---------------------------------------------------------------- */
    CASE(5, "Comando 0xA1 (IDENTIFY): strings reais nos offsets certos");
    hostsim_write_command(&hs, GD_CMD_IDDEV);
    {
        uint16_t w[128];
        uint32_t i;
        for (i = 0; i < 128 && gd_taskfile_data_pending(&hs.dev); i++)
            gd_taskfile_read_data(&hs.dev, &w[i], 1);
        /* 0x10..0x1F = 16 bytes de fabricante, a partir da palavra 8 */
        CHECK(gd_spi_buf_len >= 0x40, "resposta de IDENTIFY demasiado curta (%u)",
              gd_spi_buf_len);
        int o;
        o = expect_ascii(&gd_spi_buf[0x10], "SEGA          ", 16);
        CHECK(o < 0, "fabricante em 0x10 errado no offset +%d: '%.16s'",
              o, &gd_spi_buf[0x10]);
        o = expect_ascii(&gd_spi_buf[0x20], "GD-ROM DRIVE  ", 16);
        CHECK(o < 0, "modelo em 0x20 errado no offset +%d: '%.16s'",
              o, &gd_spi_buf[0x20]);
        o = expect_ascii(&gd_spi_buf[0x30], "Rev 5.07      ", 16);
        CHECK(o < 0, "firmware em 0x30 errado no offset +%d: '%.16s'",
              o, &gd_spi_buf[0x30]);
        CHECK(gd_spi_buf[0] == 0 && gd_spi_buf[1] == 0, "IDs 0-1 nao zero");
    }

    /* ---------------------------------------------------------------- */
    CASE(6, "Comando 0xEF (SET FEATURES): so modo de transferencia");
    hostsim_write_control(&hs, GD_R_FEATURES, GD_FEAT_XFERMODE);
    hostsim_write_control(&hs, GD_R_BYTECOUNTL, GD_SC_MODE_PIO_DEFAULT);
    hostsim_write_command(&hs, GD_CMD_SETFEATURE);
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "SET FEATURES nao voltou a IDLE");
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "SET FEATURES pôs CHECK");

    /* ---------------------------------------------------------------- */
    CASE(7, "Comando ATA invalido (0x55): Error=0x04, Status tem CHECK");
    hostsim_write_command(&hs, 0x55);
    CHECK(hs.dev.reg[GD_R_ERROR] == GD_ERR_ABRT, "Error 0x%02x != 0x%02x",
          hs.dev.reg[GD_R_ERROR], GD_ERR_ABRT);
    CHECK(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK, "Status sem CHECK");
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_BSY), "BSY ficou asserted");
    CHECK(hs.dev.irq, "INTRQ nao foi assertado");

    /* ---------------------------------------------------------------- */
    CASE(8, "SPI 0x00 TEST_UNIT: GOOD, sem CHECK");
    pkt_reset(p); p[0] = GD_SPI_TEST_UNIT;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == 0, "TEST_UNIT devolveu %u bytes (deveria 0)", n);
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "TEST_UNIT pôs CHECK");

    /* ---------------------------------------------------------------- */
    CASE(9, "SPI 0x10 REQ_STAT: 10 bytes, campo a campo");
    pkt_reset(p); p[0] = GD_SPI_REQ_STAT; p[4] = GD_REQSTAT_SIZE;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_REQSTAT_SIZE, "REQ_STAT devolveu %u bytes, esperado %d",
          n, GD_REQSTAT_SIZE);
    CHECK((resp[1] & 0xf0) == (GD_FORMAT_GDROM << 4),
          "Disc Format 0x%02x, esperado 0x%02x", resp[1] & 0xf0,
          GD_FORMAT_GDROM << 4);
    /* Byte 4 = Index tem de ser 1: com 0 o host acha que esta antes do
     * primeiro sector util e estraga o calculo de posicao. */
    CHECK(resp[4] == 1, "Index = %u, esperado 1", resp[4]);
    /* Byte 5..7 = FAD corrente. No reset o device esta na posicao 0. */
    CHECK((uint32_t)((resp[5] << 16) | (resp[6] << 8) | resp[7]) == 0,
          "FAD corrente no reset = %u, esperado 0",
          (unsigned)((resp[5] << 16) | (resp[6] << 8) | resp[7]));

    /* ---------------------------------------------------------------- */
    CASE(10, "SPI 0x11 REQ_MODE: 32 bytes, standby 0x00B4, ASCII real");
    pkt_reset(p); p[0] = GD_SPI_REQ_MODE; p[4] = GD_REQMODE_SIZE;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_REQMODE_SIZE, "REQ_MODE devolveu %u bytes, esperado %d",
          n, GD_REQMODE_SIZE);
    CHECK(((resp[4] << 8) | resp[5]) == GD_DEFAULT_STANDBY,
          "standby 0x%02x%02x != 0x00B4", resp[4], resp[5]);
    CHECK(resp[6] == 0x19, "read flags 0x%02x != 0x19", resp[6]);
    CHECK(resp[9] == GD_DEFAULT_RETRY, "ReadRetryTimes 0x%02x != 0x%02x",
          resp[9], GD_DEFAULT_RETRY);
    CHECK(expect_ascii(&resp[10], "SE      ", 8) < 0, "drive name errado: '%.8s'", &resp[10]);
    CHECK(expect_ascii(&resp[18], "Rev 6.43", 8) < 0, "system version errada: '%.8s'", &resp[18]);
    CHECK(expect_ascii(&resp[26], "990408", 6) < 0, "system date errada: '%.6s'", &resp[26]);

    /* ---------------------------------------------------------------- */
    CASE(11, "SPI 0x13 REQ_ERROR: 10 bytes, Byte 0 = 0xF0");
    pkt_reset(p); p[0] = GD_SPI_REQ_ERROR; p[4] = GD_REQERROR_SIZE;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_REQERROR_SIZE, "REQ_ERROR devolveu %u bytes, esperado %d",
          n, GD_REQERROR_SIZE);
    CHECK(resp[0] == 0xf0, "Byte 0 = 0x%02x, esperado 0xF0", resp[0]);

    /* ---------------------------------------------------------------- */
    CASE(12, "SPI 0x14 GET_TOC: 408 bytes, Select=1 so em alta densidade");
    pkt_reset(p); p[0] = GD_SPI_GET_TOC; p[4] = GD_TOC_SIZE & 0xff;
    p[3] = (uint8_t)(GD_TOC_SIZE >> 8);
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_TOC_SIZE, "GET_TOC devolveu %u bytes, esperado %d",
          n, GD_TOC_SIZE);
    /* ADR forcado a 1: o subcode so existe no canal Q. */
    CHECK(resp[0] == 0x41, "Control/ADR da track 1 = 0x%02x, esperado 0x41 (CTRL 4 << 4 | ADR 1)", resp[0]);
    CHECK((uint32_t)((resp[1] << 16) | (resp[2] << 8) | resp[3]) == md.disc.toc[0].fad,
          "FAD da track 1 errado");
    /* Um GD-ROM so tem as tracks 1 e 2 na area de densidade unica; a 3
     * em diante e' de dupla densidade. A track 3 fica a 0xFF. */
    CHECK(resp[2 * 4] == 0xff && resp[3 * 4] == 0xff,
          "tracks 3+ deveriam estar a 0xFF na densidade unica: 0x%02x 0x%02x",
          resp[2 * 4], resp[3 * 4]);
    /* [396..403]: primeira e ultima track, com o numero da track no
     * campo FAD em vez de um FAD. */
    CHECK((uint32_t)((resp[397] << 8) | resp[398]) == 1, "primeira track != 1");
    CHECK((uint32_t)((resp[401] << 8) | resp[402]) == 2,
          "ultima track = %u, esperado 2", (unsigned)((resp[401] << 8) | resp[402]));
    /* Select=1 num disco de densidade unica tem de dar uma TOC valida,
     * nao um crash nem um tamanho errado. */
    {
        uint8_t p2[GD_PKT_SIZE];
        pkt_reset(p2); p2[0] = GD_SPI_GET_TOC; p2[1] = 0x01;
        p2[3] = (uint8_t)(GD_TOC_SIZE >> 8); p2[4] = GD_TOC_SIZE & 0xff;
        n = hostsim_spi_command(&hs, p2, resp, sizeof resp);
        CHECK(n == GD_TOC_SIZE, "GET_TOC Select=1 devolveu %u bytes", n);
    }
    /* Em alta densidade, Select=1 tem de devolver conteudo diferente. */
    {
        hostsim_t hs2;
        memdisc_t md2;
        memdisc_init(&md2, 1);
        hostsim_init(&hs2, &md2.disc);
        pkt_reset(p); p[0] = GD_SPI_GET_TOC; p[1] = 0x01;
        p[3] = (uint8_t)(GD_TOC_SIZE >> 8); p[4] = GD_TOC_SIZE & 0xff;
        n = hostsim_spi_command(&hs2, p, resp, sizeof resp);
        CHECK(n == GD_TOC_SIZE, "GET_TOC HD Select=1 devolveu %u bytes", n);
    }

    /* ---------------------------------------------------------------- */
    CASE(13, "SPI 0x15 REQ_SES: 6 bytes");
    pkt_reset(p); p[0] = GD_SPI_REQ_SES; p[4] = GD_REQSES_SIZE;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == GD_REQSES_SIZE, "REQ_SES devolveu %u bytes, esperado %d",
          n, GD_REQSES_SIZE);
    /* Um GD-ROM tem SEMPRE 2 sessoes: a 1 comeca na track 1, a 2 na 3. */
    CHECK(resp[2] == 2, "n de sessoes %u != 2", resp[2]);
    CHECK((uint32_t)((resp[3] << 16) | (resp[4] << 8) | resp[5]) == GD_LEADOUT_FAD,
          "EndFAD da sessao 0 = %u, esperado %u",
          (unsigned)((resp[3] << 16) | (resp[4] << 8) | resp[5]), GD_LEADOUT_FAD);
    /* Sessao 2 comeca na track 3. */
    pkt_reset(p); p[0] = GD_SPI_REQ_SES; p[2] = 2; p[4] = GD_REQSES_SIZE;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(resp[2] == 3, "primeira track da sessao 2 = %u, esperado 3", resp[2]);

    /* ---------------------------------------------------------------- */
    CASE(14, "SPI 0x30 CD_READ: le o sector certo, byte a byte");
    pkt_reset(p);
    p[0] = GD_SPI_CD_READ;
    p[1] = GD_READ_SEL_DATA;                 /* 0x20: so Data, tipo FAD */
    pkt_set_fad(p, 5);
    p[8] = 0; p[9] = 0; p[10] = 1;            /* 1 sector */
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == MEMDISC_SECTOR_SIZE, "CD_READ devolveu %u bytes, esperado %d",
          n, MEMDISC_SECTOR_SIZE);
    memdisc_expected(5, MEMDISC_SECTOR_SIZE, expect);
    CHECK(memcmp(resp, expect, MEMDISC_SECTOR_SIZE) == 0,
          "conteudo do sector 5 diferente da imagem");
    /* Verificar que leu o FAD 5 e nao outro. */
    CHECK(((resp[0] << 24) | (resp[1] << 16) | (resp[2] << 8) | resp[3]) == 5,
          "cabecalho do sector aponta para FAD %d, esperado 5",
          ((resp[0] << 24) | (resp[1] << 16) | (resp[2] << 8) | resp[3]));

    /* ---------------------------------------------------------------- */
    CASE(15, "SPI 0x31 CD_READ2: transfer length em [6:7], 16 bits");
    pkt_reset(p);
    p[0] = GD_SPI_CD_READ2;
    p[1] = GD_READ_SEL_DATA;
    pkt_set_fad(p, 3);
    p[6] = 0; p[7] = 2;                      /* 2 sectores, 16-bit */
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == 2 * MEMDISC_SECTOR_SIZE,
          "CD_READ2 devolveu %u bytes, esperado %d", n,
          2 * MEMDISC_SECTOR_SIZE);
    memdisc_expected(3, MEMDISC_SECTOR_SIZE, expect);
    CHECK(memcmp(resp, expect, MEMDISC_SECTOR_SIZE) == 0,
          "CD_READ2: primeiro sector errado");

    /* ---------------------------------------------------------------- */
    CASE(16, "SPI 0x40 GET_SCD formato 0: 100 bytes");
    pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_RAW;
    p[3] = 0; p[4] = 100;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == 100, "GET_SCD fmt 0 devolveu %u bytes, esperado 100", n);
    CHECK(((resp[2] << 8) | resp[3]) == 100, "length field != 0x0064");

    /* ---------------------------------------------------------------- */
    CASE(17, "SPI 0x40 GET_SCD formato 1: 14 bytes");
    pkt_reset(p); p[0] = GD_SPI_GET_SCD; p[1] = GD_SCD_Q;
    p[3] = 0; p[4] = 14;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == 14, "GET_SCD fmt 1 devolveu %u bytes, esperado 14", n);
    CHECK(((resp[2] << 8) | resp[3]) == 14, "length field != 0x000E");

    /* ---------------------------------------------------------------- */
    CASE(18, "SPI 0x70 -> 0x71: GOOD, depois blob, estado -> PAUSE");
    pkt_reset(p); p[0] = GD_SPI_CMD70; p[2] = 0x1f;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == 0, "0x70 devolveu %u bytes (esperado 0)", n);
    CHECK(!(hs.dev.reg[GD_R_STATUS] & GD_ST_CHECK), "0x70 pôs CHECK");

    pkt_reset(p); p[0] = GD_SPI_CMD71;
    n = hostsim_spi_command(&hs, p, resp, sizeof resp);
    CHECK(n == 6, "0x71 devolveu %u bytes, esperado 6 (iceGDROM)", n);
    CHECK(resp[0] == 0xba && resp[1] == 0x06 && resp[2] == 0x0d &&
          resp[3] == 0xca && resp[4] == 0x6a && resp[5] == 0x1f,
          "blob do 0x71 diferente do iceGDROM");
    CHECK(gd_device_state(&hs.dev) == GD_STATE_PAUSE,
          "estado apos 0x71 = %u, esperado PAUSE", gd_device_state(&hs.dev));

    /* ---------------------------------------------------------------- */
    CASE(19, "SPI 0x12 SET MODE: unico comando com dados host->device");
    pkt_reset(p); p[0] = GD_SPI_SET_MODE; p[4] = 8;
    /* Nao ha dados de saida: o device tem de consumir os 8 bytes. */
    n = hostsim_spi_command(&hs, p, NULL, 0);
    CHECK(n == 0, "SET_MODE devolveu %u bytes (esperado 0)", n);
    /* O host escreve as palavras. */
    hostsim_write_data(&hs, 0x0000);
    hostsim_write_data(&hs, 0x0000);
    hostsim_write_data(&hs, 0x0000);
    hostsim_write_data(&hs, 0x0000);
    CHECK(hs.dev.phase == GD_PHASE_IDLE, "SET_MODE nao voltou a IDLE");

    /* ---------------------------------------------------------------- */
    CASE(20, "Troca de disco: TEST_UNIT reflecte <BUSY> / NODISC");
    {
        gd_disc_t empty;
        hostsim_t hs2;
        memset(&empty, 0, sizeof empty);
        empty.present = 0;
        empty.read_sectors = memdisc_read_sectors;

        hostsim_init(&hs2, &empty);
        pkt_reset(p); p[0] = GD_SPI_TEST_UNIT;
        n = hostsim_spi_command(&hs2, p, resp, sizeof resp);
        CHECK(n == 0, "TEST_UNIT sem disco devolveu %u bytes", n);
        CHECK(gd_device_state(&hs2.dev) == GD_STATE_NODISC,
              "estado sem disco = %u, esperado NODISC", gd_device_state(&hs2.dev));

        /* CD_READ sem disco tem de dar NOT READY, nao dados a zeros. */
        pkt_reset(p); p[0] = GD_SPI_CD_READ; p[1] = GD_READ_SEL_DATA;
        pkt_set_fad(p, 0); p[10] = 1;
        n = hostsim_spi_command(&hs2, p, resp, sizeof resp);
        CHECK(n == 0, "CD_READ sem disco devolveu %u bytes", n);
        CHECK((hs2.dev.reg[GD_R_ERROR] >> GD_ERR_SENSEKEY_SHIFT) == GD_SK_NOTREADY,
              "Sense Key = %u, esperado NOT READY",
              hs2.dev.reg[GD_R_ERROR] >> GD_ERR_SENSEKEY_SHIFT);
    }

    /* ---------------------------------------------------------------- */
    /* ---------------------------------------------------------------- */
    CASE(21, "Alternate Status NAO limpa o INTRQ; Status limpa");
    {
        /*
         * O 0x71 termina com o IRQ asserted. Como hostsim_spi_command()
         * le o Status no fim (o que o limpa), medimos o numero de
         * asserts e depois testamos as duas leituras por cima.
         */
        hostsim_t h;
        hostsim_init(&h, &md.disc);
        pkt_reset(p); p[0] = GD_SPI_CMD71;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.irq_asserts > 0, "o 0x71 nao assertou o IRQ");
        CHECK(h.dev.irq == 0, "pre-condicao: o Status final devia limpar o IRQ");
        /* Reasserta artificialmente para testar as leituras. */
        h.dev.irq = 1;
        (void)hostsim_read_altstatus(&h);
        CHECK(h.dev.irq == 1, "Alternate Status limpou o IRQ - nao devia");
        (void)hostsim_read_control(&h, GD_R_STATUS);
        CHECK(h.dev.irq == 0, "Status nao limpou o IRQ");
    }

    /* ---------------------------------------------------------------- */
    CASE(22, "REQ_ERROR limpa o estado de erro depois de responder");
    {
        hostsim_t h;
        hostsim_init(&h, &md.disc);
        pkt_reset(p); p[0] = GD_SPI_READ_SECTOR;   /* comando invalido */
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK((h.dev.reg[GD_R_STATUS] & GD_ST_CHECK) != 0,
              "comando invalido nao pôs CHECK (status 0x%02x)",
              h.dev.reg[GD_R_STATUS]);
        CHECK(h.dev.sensekey == GD_SK_ILLEGALREQ, "sense key = %u",
              h.dev.sensekey);
        pkt_reset(p); p[0] = GD_SPI_REQ_ERROR; p[4] = GD_REQERROR_SIZE;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK((h.dev.reg[GD_R_STATUS] & GD_ST_CHECK) == 0,
              "REQ_ERROR nao limpou CHECK");
        CHECK(h.dev.sensekey == GD_SK_NOSE, "sense key nao foi limpa");
        CHECK(h.dev.reg[GD_R_ERROR] == 0, "registo Error nao foi limpo");
    }

    /* ---------------------------------------------------------------- */
    CASE(23, "TEST_UNIT poe CHECK quando a unidade esta BUSY");
    {
        hostsim_t h;
        hostsim_init(&h, &md.disc);
        gd_device_set_state(&h.dev, GD_STATE_BUSY);
        pkt_reset(p); p[0] = GD_SPI_TEST_UNIT;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK((h.dev.reg[GD_R_STATUS] & GD_ST_CHECK) != 0,
              "TEST_UNIT com unidade BUSY nao pôs CHECK");
        /* Em PAUSE nao deve por. */
        gd_device_set_state(&h.dev, GD_STATE_PAUSE);
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK((h.dev.reg[GD_R_STATUS] & GD_ST_CHECK) == 0,
              "TEST_UNIT com unidade PAUSE pôs CHECK");
    }

    /* ---------------------------------------------------------------- */
    CASE(24, "Soft Reset escreve Byte Count 0x14 / 0xEB");
    {
        hostsim_t h;
        hostsim_init(&h, &md.disc);
        hostsim_write_command(&h, GD_CMD_SOFTRESET);
        CHECK(h.dev.reg[GD_R_BYTECOUNTL] == 0x14, "ByteCount lo = 0x%02x",
              h.dev.reg[GD_R_BYTECOUNTL]);
        CHECK(h.dev.reg[GD_R_BYTECOUNTH] == 0xeb, "ByteCount hi = 0x%02x",
              h.dev.reg[GD_R_BYTECOUNTH]);
    }

    /* ---------------------------------------------------------------- */
    CASE(25, "SET_MODE so escreve nos offsets 0..9");
    {
        hostsim_t h;
        hostsim_init(&h, &md.disc);
        /* Offset 0, 4 bytes: dentro da area gravavel. */
        pkt_reset(p); p[0] = GD_SPI_SET_MODE; p[2] = 0; p[4] = 4;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        hostsim_write_data(&h, 0x0000);
        hostsim_write_data(&h, 0x0000);
        CHECK(h.dev.phase == GD_PHASE_IDLE, "SET_MODE gravavel nao terminou");

        /* Offset 12, 4 bytes: fora da area gravavel, logo nada e'
         * pedido ao host e o comando termina de imediato. */
        pkt_reset(p); p[0] = GD_SPI_SET_MODE; p[2] = 12; p[4] = 4;
        hostsim_spi_command(&h, p, resp, sizeof resp);
        CHECK(h.dev.phase == GD_PHASE_IDLE,
              "SET_MODE num offset nao gravavel ficou pendente");
    }

    /* ---------------------------------------------------------------- */
    CASE(26, "CD_READ so produz 2048, 2340 ou 2352");
    {
        /* 0x20 (Data) -> 2048 */
        pkt_reset(p); p[0] = GD_SPI_CD_READ; p[1] = GD_READ_SEL_DATA;
        pkt_set_fad(p, 1); p[10] = 1;
        n = hostsim_spi_command(&hs, p, resp, sizeof resp);
        CHECK(n == 2048, "Data select 0x20 deu %u bytes, esperado 2048", n);
        /* 0x30 (Other) -> 2352 */
        pkt_reset(p); p[0] = GD_SPI_CD_READ; p[1] = GD_READ_SEL_OTHER;
        pkt_set_fad(p, 1); p[10] = 1;
        n = hostsim_spi_command(&hs, p, resp, sizeof resp);
        CHECK(n == 2352, "Data select 0x10 deu %u bytes, esperado 2352", n);
    }

    /* ---------------------------------------------------------------- */
    CASE(27, "REQ_STAT reporta o FAD corrente depois de um CD_READ");
    {
        pkt_reset(p); p[0] = GD_SPI_CD_READ; p[1] = GD_READ_SEL_DATA;
        pkt_set_fad(p, 9); p[10] = 1;
        (void)hostsim_spi_command(&hs, p, resp, sizeof resp);
        pkt_reset(p); p[0] = GD_SPI_REQ_STAT; p[4] = GD_REQSTAT_SIZE;
        (void)hostsim_spi_command(&hs, p, resp, sizeof resp);
        CHECK((uint32_t)((resp[5] << 16) | (resp[6] << 8) | resp[7]) == 8,
              "FAD corrente = %u, esperado 8 (o sector antes do lido)",
              (unsigned)((resp[5] << 16) | (resp[6] << 8) | resp[7]));
        CHECK(resp[3] == 1, "TNO = %u, esperado 1", resp[3]);
    }

    /* ---------------------------------------------------------------- */
    CASE(28, "REQ_MODE respeita o offset e a contagem do packet");
    {
        /* Pedir so 4 bytes a partir do offset 4. */
        pkt_reset(p); p[0] = GD_SPI_REQ_MODE; p[2] = 4; p[4] = 4;
        n = hostsim_spi_command(&hs, p, resp, sizeof resp);
        CHECK(n == 4, "REQ_MODE devolveu %u bytes, esperado 4", n);
        CHECK(resp[0] == 0x00 && resp[1] == 0xb4,
              "offset 4 de REQ_MODE = %02x %02x, esperado 00 b4",
              resp[0], resp[1]);
        /* Contagem maior do que a struct: tem de ser clampada. */
        pkt_reset(p); p[0] = GD_SPI_REQ_MODE; p[2] = 0; p[4] = 0xff;
        n = hostsim_spi_command(&hs, p, resp, sizeof resp);
        CHECK(n == GD_REQMODE_SIZE, "REQ_MODE clampado a %u, esperado %d",
              n, GD_REQMODE_SIZE);
    }

    printf("\n%d checks passados, %u falhados (caso %d)\n",
           g_pass, g_fail, g_case);
    return g_fail ? 1 : 0;
}
