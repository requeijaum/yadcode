/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_spi.c - Camada L3.
 *
 * Implementacao dos 16 comandos da Tabela 6.1 da spec, mais os dois
 * comandos nao documentados (0x70, 0x71) que o Dreamcast usa no boot.
 *
 * Todos os formatos de resposta vem literalmente da secao 8.2 da spec.
 */
#include <string.h>
#include "gd_spi.h"
#include "gd_cdda.h"

uint8_t  gd_spi_buf[GD_TOC_SIZE];
uint32_t gd_spi_buf_len;

static void buf_reset(void) { gd_spi_buf_len = 0; }
static void buf_u8(uint8_t v)  { if (gd_spi_buf_len < sizeof gd_spi_buf) gd_spi_buf[gd_spi_buf_len++] = v; }
static void buf_be16(uint16_t v) { buf_u8((uint8_t)(v >> 8)); buf_u8((uint8_t)v); }
static void buf_be24(uint32_t v) { buf_u8((uint8_t)(v >> 16)); buf_u8((uint8_t)(v >> 8)); buf_u8((uint8_t)v); }

/* ------------------------------------------------------------------ */
/* Fonte de dados: uma entrada pequena que serve a partir de gd_spi_buf */
/* ------------------------------------------------------------------ */

static int src_buf_open(gd_device_t *dev, uint16_t *bytecount)
{
    (void)dev;
    *bytecount = (uint16_t)gd_spi_buf_len;
    return 0;
}

static uint32_t src_buf_read(gd_device_t *dev, uint16_t *dst, uint32_t nwords)
{
    uint32_t want = nwords, i;
    for (i = 0; i < want; i++) {
        uint32_t off = dev->transferred + i * 2;
        if (off + 1 >= gd_spi_buf_len) { want = i; break; }
        dst[i] = (uint16_t)((gd_spi_buf[off] << 8) | gd_spi_buf[off + 1]);
    }
    return want;
}

static void src_buf_close(gd_device_t *dev) { (void)dev; }

static const gd_source_t src_buf = { src_buf_open, src_buf_read, src_buf_close };

/* ------------------------------------------------------------------ */
/* Fonte de dados para CD_READ: servida directamente do disco          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t fad;        /* proximo FAD a servir */
    uint32_t remaining;  /* sectores por servir */
    uint32_t sector_size;
} cdread_state_t;

static cdread_state_t cdread;

int gd_spi_disc_readable(const gd_device_t *dev)
{
    return dev->disc != NULL && dev->disc->present;
}

void gd_spi_clear_error(gd_device_t *dev)
{
    gd_taskfile_clear_error(dev);
}

uint32_t gd_spi_current_fad(const gd_device_t *dev)
{
    return dev->cur_fad;
}


/*
 * Numero da track a que um FAD pertence. O GD-ROM devolve 0xAA quando
 * o FAD nao esta dentro de nenhuma track - o host trata isso como
 * "posicao invalida" em vez de crashar.
 */
uint8_t gd_spi_track_of_fad(const gd_disc_t *d, uint32_t fad)
{
    if (!d || !d->track_of_fad) return 0xaa;
    /* O callback nao altera o disco; o cast e' para satisfazer o
     * prototipo, que existe para permitir implementacoes com cache. */
    return d->track_of_fad((gd_disc_t *)d, fad);
}

static int src_cdread_open(gd_device_t *dev, uint16_t *bytecount)
{
    const uint8_t *p = dev->packet;   /* packet vivo em dev->packet */
    uint32_t n;

    if (!gd_spi_disc_readable(dev)) return -1;

    /*
     * CD_READ  (0x30): transfer length em Bytes 8..10, 24-bit.
     * CD_READ2 (0x31): transfer length em Bytes 6..7, 16-bit, e o next
     * address em 8..10. Sao formatos diferentes (Tabela 6.1) e
     * confundi-los faz o device devolver 0 sectores.
     */
    if (p[0] == GD_SPI_CD_READ2) {
        n = ((uint32_t)p[6] << 8) | p[7];
    } else {
        n = ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 8) | p[10];
    }

    /*
     * Data Select, Byte1[7:4] (spec seca 8.2) e Expected Data Type,
     * Byte1[3:1]. O GD-ROM so produz tres tamanhos:
     *   2048 - o normal
     *   2340 - header + subheader + data + EDC/ECC, sem o bloco de modo 2
     *   2352 - o sector cru completo
     */
    {
        uint8_t head = (p[1] & GD_READ_SEL_HEADER)    ? 1 : 0;
        uint8_t subh = (p[1] & GD_READ_SEL_SUBHEADER) ? 1 : 0;
        uint8_t data = (p[1] & GD_READ_SEL_DATA)      ? 1 : 0;
        uint8_t other= (p[1] & GD_READ_SEL_OTHER)     ? 1 : 0;
        uint8_t type = (p[1] & GD_READ_TYPE_MASK) >> GD_READ_TYPE_SHIFT;

        if (head && subh && data && type == 3 && !other) {
            cdread.sector_size = 2340;
        } else if (other || type == GD_READ_TYPE_CDDA_2352) {
            cdread.sector_size = 2352;
        } else {
            cdread.sector_size = 2048;
        }
    }

    cdread.fad = gd_spi_cdread_fad(p);
    cdread.remaining = n;

    *bytecount = (uint16_t)(n * cdread.sector_size);
    return 0;
}

static uint8_t  sec_cache[2352];
static uint32_t sec_fad = 0xffffffffu;

static uint32_t src_cdread_read(gd_device_t *dev, uint16_t *dst, uint32_t nwords)
{
    uint32_t bytes = nwords * 2;
    uint32_t into, avail, n, i;

    if (cdread.remaining == 0) return 0;

    into  = dev->transferred % cdread.sector_size;
    avail = cdread.sector_size - into;
    if (avail > bytes) avail = bytes;

    if (sec_fad != cdread.fad) {
        if (dev->disc->read_sectors(dev->disc, cdread.fad, 1,
                                    cdread.sector_size, sec_cache) != 0) {
            return 0;
        }
        sec_fad = cdread.fad;
    }

    /* dst entra sempre limpo: ver gd_taskfile_read_data(). */
    for (i = 0; i < avail; i++) {
        uint8_t b = sec_cache[into + i];
        if (i & 1) dst[i / 2] = (uint16_t)(dst[i / 2] | b);
        else       dst[i / 2] = (uint16_t)(dst[i / 2] | ((uint16_t)b << 8));
    }

    n = (avail + 1) / 2;
    if (avail == cdread.sector_size) {
        cdread.fad++;
        cdread.remaining--;
        sec_fad = 0xffffffffu;
    }
    return n;
}

static void src_cdread_close(gd_device_t *dev) { (void)dev; }

static const gd_source_t src_cdread = { src_cdread_open, src_cdread_read,
                                        src_cdread_close };

/* ------------------------------------------------------------------ */
/* Utilitarios                                                        */
/* ------------------------------------------------------------------ */

uint16_t gd_spi_alloc_len(const uint8_t *pkt, int wide)
{
    if (wide) {
        /* GET_TOC e GET_SCD: 2 bytes, MSB em Byte 3 (Tabela 6.1) */
        return (uint16_t)(((uint16_t)pkt[3] << 8) | pkt[4]);
    }
    return pkt[4];
}

uint32_t gd_spi_msf_to_fad(uint8_t m, uint8_t s, uint8_t f)
{
    return (uint32_t)((m * 60u * 75u) + (s * 75u) + f);
}

uint32_t gd_spi_cdread_fad(const uint8_t *pkt)
{
    if (pkt[1] & GD_READ_PARAM_MSF) {
        return gd_spi_msf_to_fad(pkt[2], pkt[3], pkt[4]);
    }
    return (uint32_t)(((uint32_t)pkt[2] << 16) |
                      ((uint32_t)pkt[3] << 8) | pkt[4]);
}

/*
 * Empacota `n` bytes ASCII, completando com espacos. O `strlen` e'
 * medido uma vez: ler `s[i]` para alem do NUL seria uma leitura fora
 * da string (os literais de 14 chars + NUL tem 15 bytes, e n = 16).
 */
static void resp_pack_ascii(const char *s, int n)
{
    int i;
    size_t len = strlen(s);
    for (i = 0; i < n; i++) {
        buf_u8((size_t)i < len ? (uint8_t)s[i] : ' ');
    }
}

/* ------------------------------------------------------------------ */
/* Comandos individuais                                                */
/* ------------------------------------------------------------------ */

/* Seca 8.2 - REQ_STAT: 10 bytes */
static void do_req_stat(gd_device_t *dev, const uint8_t *pkt)
{
    (void)pkt;
    gd_disc_t *d = dev->disc;
    uint8_t state = gd_device_state(dev);
    uint32_t cur_fad = gd_spi_current_fad(dev);
    uint8_t tno = gd_spi_track_of_fad(d, cur_fad);
    uint8_t ctrl = 0x04, adr = 0x01;
    uint8_t repeats = dev->cdda.repeats;

    if (d && d->track_control) d->track_control(d, tno, &ctrl, &adr);

    buf_reset();
    buf_u8((uint8_t)(state & 0x0f));
    /* Byte 1: Disc Format no nibble alto, repeat count no nibble baixo. */
    buf_u8((uint8_t)(((d ? d->disc_format : 0) << 4) | (repeats & 0x0f)));
    buf_u8((uint8_t)(((ctrl & 0x0f) << 4) | (adr & 0x0f)));  /* Control/ADR */
    buf_u8(tno);                     /* TNO: numero da track      */
    buf_u8(0x01);                    /* Index: 1, nunca 0         */
    buf_be24(cur_fad);               /* posicao actual            */
    buf_u8(0x00);                    /* MaxReadErrorRetryTimes    */
    buf_u8(0x00);

    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
}

/*
 * Seca 8.2 - REQ_MODE: 32 bytes. RISCO ALTO: lido por praticamente todos
 * os jogos, e o driver do kernel Linux le exactamente este prefixo.
 *
 * Byte 2 do packet e' o offset e Byte 4 a contagem.
 *
 * A posicao 6 (read flags) e' 0x19. Os campos 10..31 sao imutaveis em
 * runtime: SET_MODE so escreve em 0..9 (ver do_set_mode).
 */
static void do_req_mode(gd_device_t *dev, const uint8_t *pkt)
{
    (void)dev;
    {
        static const uint8_t hw_info[GD_REQMODE_SIZE] = {
            0x00, 0x00,             /* [0..1] reservados                 */
            0x00,                   /* [2]    CD-ROM speed: 0 = MAX       */
            0x00,                   /* [3]    reservado                   */
            0x00,                   /* [4]    standby hi                   */
            0xb4,                   /* [5]    standby lo = 180 s           */
            0x19,                   /* [6]    read flags                   */
            0x00, 0x00,             /* [7..8] reservados                  */
            0x08,                   /* [9]    read retry                   */
            'S', 'E', ' ', ' ', ' ', ' ', ' ', ' ',   /* [10..17] drive   */
            'R', 'e', 'v', ' ', '6', '.', '4', '3',   /* [18..25] version */
            '9', '9', '0', '4', '0', '8'               /* [26..31] date    */
        };
        uint8_t off = pkt[2];
        uint8_t cnt = pkt[4];
        uint8_t i;

        /* Byte 2 = offset, Byte 4 = contagem. Sem clamp no inicio. */
        if (off >= GD_REQMODE_SIZE) {
            gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
            return;
        }
        if (cnt > GD_REQMODE_SIZE - off) cnt = (uint8_t)(GD_REQMODE_SIZE - off);

        buf_reset();
        for (i = 0; i < cnt; i++) buf_u8(hw_info[off + i]);
    }
    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
}

/* Seca 8.2 - REQ_ERROR: 10 bytes. Byte 0 = 0xF0. */
static void do_req_error(gd_device_t *dev, const uint8_t *pkt)
{
    (void)pkt;
    buf_reset();
    buf_u8(0xf0);
    buf_u8(0x00);
    buf_u8((uint8_t)(dev->reg[GD_R_ERROR] >> GD_ERR_SENSEKEY_SHIFT));
    buf_u8(0x00);
    buf_be16(0x0000);              /* command specific info */
    buf_be16(0x0000);
    buf_u8(0x00);                  /* ASC  */
    buf_u8(0x00);                  /* ASCQ */
    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);

    /*
     * A spec diz: "When another command is issued, error information is
     * cleared". O device real vai mais longe - ler o erro limpa-o
     * imediatamente, e nao no comando seguinte. Sem isto o CHECK fica
     * pendiente e o host entra em loop a re-tentar.
     */
    dev->reg[GD_R_ERROR]  = 0;
    gd_taskfile_set_status(dev, GD_ST_CHECK, 0);
    gd_spi_clear_error(dev);
}

/*
 * Seca 8.2 - GET_TOC: 408 bytes. RISCO ALTO.
 *
 * Byte 1 bit 0 = Select: 0 -> densidade unica, 1 -> dupla densidade.
 * A contagem e um 16-bit nos Bytes 3 e 4 (MSB no Byte 3), com clamp a
 * 408.
 *
 * A construcao da TOC vive em L5, nao aqui: ADR forcado a 1, primeira e
 * ultima track com o numero da track no campo FAD, e a regra de que um
 * GD-ROM so tem as tracks 1 e 2 na area de densidade unica. A camada SPI
 * limita-se a escolher a area e a pedir os bytes.
 */
static void do_get_toc(gd_device_t *dev, const uint8_t *pkt)
{
    gd_disc_t *d = dev->disc;
    int area = pkt[1] & 0x01;
    uint16_t cnt = gd_spi_alloc_len(pkt, GD_ALLOCLEN_WORD);

    buf_reset();

    if (!gd_spi_disc_readable(dev) || !d->fill_toc) {
        if (dev->hal->memzero) dev->hal->memzero(gd_spi_buf, GD_TOC_SIZE);
        gd_spi_buf_len = GD_TOC_SIZE;
        gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
        return;
    }

    d->fill_toc(d, area, gd_spi_buf);
    gd_spi_buf_len = GD_TOC_SIZE;

    if (cnt > GD_TOC_SIZE) cnt = GD_TOC_SIZE;
    gd_spi_buf_len = cnt;
    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
}

/*
 * Seca 8.2 - REQ_SES: 6 bytes. Byte 2 do packet e' o numero da sessao,
 * Byte 4 a contagem.
 *
 * Byte 0 e' o estado da unidade, e a camada L5 preenche o resto. Para
 * sessao 0 devolve o total de sessoes e o EndFAD do disco; para uma
 * sessao concreta, o numero da sua primeira track e o seu StartFAD.
 */
static void do_req_ses(gd_device_t *dev, const uint8_t *pkt)
{
    gd_disc_t *d = dev->disc;
    uint8_t sess = pkt[2];
    uint8_t info[6];
    uint8_t i;

    if (gd_spi_disc_readable(dev) && d->fill_session_info) {
        d->fill_session_info(d, sess, info);
    } else {
        memset(info, 0, sizeof info);
    }
    info[0] = (uint8_t)(gd_device_state(dev) & 0x0f);

    buf_reset();
    for (i = 0; i < 6; i++) buf_u8(info[i]);
    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
}

/* Seca 8.2 - GET_SCD formato 0: 100 bytes (subcode P..W, 96 bytes) */
/* Seca 8.2 - GET_SCD formato 1: 14 bytes (Q-subcode) */
/*
 * Seca 8.2 - GET_SCD. A geracao do subcode e' L4 (gd_cdda.c); aqui
 * limita-se a pedir o formato certo e a limitar pela Allocation Length
 * do packet, que e um 16-bit nos Bytes 3 e 4.
 */
static void do_get_scd(gd_device_t *dev, const uint8_t *pkt)
{
    uint8_t format = pkt[1] & 0x0f;
    uint16_t alloc = gd_spi_alloc_len(pkt, GD_ALLOCLEN_WORD);
    uint32_t n;

    buf_reset();
    n = gd_cdda_get_subcode(dev, format, gd_spi_buf, sizeof gd_spi_buf);
    if (n == 0) {
        /* Formato desconhecido: resposta vazia, sem CHECK. */
        gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
        return;
    }
    if (alloc && alloc < n) n = alloc;
    gd_spi_buf_len = n;
    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
}

/* Seca 7.4 - comandos sem dados */
static void do_test_unit(gd_device_t *dev, const uint8_t *pkt)
{
    (void)pkt;
    if (!gd_spi_disc_readable(dev)) {
        gd_device_set_state(dev, GD_STATE_NODISC);
        gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
        return;
    }
    /*
     * Se a unidade estiver BUSY - tipicamente a trocar de disco - o
     * TEST_UNIT tem de reportar CHECK para o host voltar a tentar. A
     * spec diz que TEST_UNIT nao reporta CHECK, mas um disco real
     * reporta, e jogos dependem disso para a troca de disco funcionar.
     */
    if (gd_device_state(dev) == GD_STATE_BUSY) {
        gd_taskfile_set_status(dev, GD_ST_CHECK, 1);
    }
    gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
}

static void do_cd_open(gd_device_t *dev, const uint8_t *pkt)
{
    (void)pkt;
    gd_device_set_state(dev, GD_STATE_OPEN);
    gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
}

static void do_cd_play(gd_device_t *dev, const uint8_t *pkt)
{
    if (gd_cdda_play(dev, pkt) != 0) {
        gd_taskfile_finish_nodata(dev, GD_SK_NOTREADY);
        return;
    }
    gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
}

static void do_cd_seek(gd_device_t *dev, const uint8_t *pkt)
{
    if (gd_cdda_seek(dev, pkt) != 0) {
        gd_taskfile_finish_nodata(dev, GD_SK_NOTREADY);
        return;
    }
    gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
}

static void do_cd_scan(gd_device_t *dev, const uint8_t *pkt)
{
    if (gd_cdda_scan(dev, pkt) != 0) {
        gd_taskfile_finish_nodata(dev, GD_SK_NOTREADY);
        return;
    }
    gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
}

static void do_cd_read(gd_device_t *dev, const uint8_t *pkt)
{
    if (!gd_spi_disc_readable(dev)) {
        gd_device_set_state(dev, GD_STATE_NODISC);
        gd_taskfile_finish_nodata(dev, GD_SK_NOTREADY);
        return;
    }
    /*
     * A posicao corrente e' o sector imediatamente ANTES do lido. O
     * REQ_STAT tem de a reportar, senao o host perde a conta de onde
     * esta no disco e volta a pedir os mesmos sectores.
     */
    /* Ler dados interrompe qualquer reproducao de audio em curso. */
    dev->cdda.status = GD_CDDA_NO_INFO;
    dev->cur_fad = gd_spi_cdread_fad(pkt) - 1;
    gd_taskfile_begin_response(dev, &src_cdread, GD_SK_NOSE);
}

/* Seca 7.2 - SET_MODE: o unico comando com dados host->device */
static void do_set_mode(gd_device_t *dev, const uint8_t *pkt)
{
    static uint8_t sink[GD_REQMODE_SIZE];
    uint8_t off = pkt[2];
    uint8_t len = pkt[4];
    uint8_t writable = GD_HARDINFO_WRITABLE;   /* offsets 0..9 */

    /*
     * So os primeiros 10 bytes sao gravaveis em runtime: speed, standby,
     * read flags e read retry. Os bytes 10..31 (nome do drive, versao do
     * sistema, data) sao imutaveis - o device real ignora escritas la.
     */
    if (off >= writable) {
        gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
        return;
    }
    if (len > writable - off) len = (uint8_t)(writable - off);
    if (len > sizeof sink) len = (uint8_t)sizeof sink;

    /* A resposta de SET_MODE nao tem dados: e' known-length. */
    dev->sink     = sink;
    dev->sink_len = len;
    dev->sink_got = 0;
    dev->phase    = GD_PHASE_DATA_IN;
    gd_taskfile_sync_status(dev);
    if (dev->hal->assert_irq) dev->hal->assert_irq(dev, 1);
    dev->irq = 1;
}

void gd_spi_identify(gd_device_t *dev)
{
    int i;
    buf_reset();
    /* Bytes 0-3: IDs. */
    buf_be16(0x0000);
    buf_be16(0x0000);
    /* Bytes 0x04-0x0F: reservados, a zeros. */
    for (i = 0; i < 12; i++) buf_u8(0x00);
    /* 0x10-0x1F: fabricante, 16 ASCII (spec seca 3) */
    resp_pack_ascii("SEGA          ", 16);
    /* 0x20-0x2F: modelo, 16 ASCII */
    resp_pack_ascii("GD-ROM DRIVE  ", 16);
    /* 0x30-0x3F: firmware, 16 ASCII */
    resp_pack_ascii("Rev 5.07      ", 16);
    gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
}

/* ------------------------------------------------------------------ */
/* Despacho                                                           */
/* ------------------------------------------------------------------ */

void gd_spi_execute(gd_device_t *dev, const uint8_t *pkt)
{
    /*
     * gd_spi_buf e' APENAS o buffer de resposta. O packet vive em
     * dev->packet e passa-se aos handlers como argumento. Confundir os
     * dois foi o primeiro bug que os testes apanharam.
     */
    gd_spi_buf_len = 0;

    switch (pkt[0]) {
    case GD_SPI_TEST_UNIT: do_test_unit(dev, pkt);  break;
    case GD_SPI_REQ_STAT:  do_req_stat(dev, pkt);   break;
    case GD_SPI_REQ_MODE:  do_req_mode(dev, pkt);   break;
    case GD_SPI_SET_MODE:  do_set_mode(dev, pkt);   break;
    case GD_SPI_REQ_ERROR: do_req_error(dev, pkt);  break;
    case GD_SPI_GET_TOC:   do_get_toc(dev, pkt);    break;
    case GD_SPI_REQ_SES:   do_req_ses(dev, pkt);    break;
    case GD_SPI_CD_OPEN:   do_cd_open(dev, pkt);    break;
    case GD_SPI_CD_PLAY:   do_cd_play(dev, pkt);    break;
    case GD_SPI_CD_SEEK:   do_cd_seek(dev, pkt);    break;
    case GD_SPI_CD_SCAN:   do_cd_scan(dev, pkt);    break;
    case GD_SPI_CD_READ:   do_cd_read(dev, pkt);    break;
    case GD_SPI_CD_READ2:  do_cd_read(dev, pkt);    break;
    case GD_SPI_GET_SCD:   do_get_scd(dev, pkt);    break;

    case GD_SPI_CMD70:
        /*
         * "Prepare disk" / security check. Nao documentado, obrigatorio.
         * O Dreamdrive responde GOOD e cai em TEST_UNIT.
         */
        do_test_unit(dev, pkt);
        break;

    case GD_SPI_CMD71:
        /*
         * Resposta enlatada. iceGDROM responde 6 bytes:
         *   BA 06 0D CA 6A 1F
         * MAME guarda ~200 bytes (GDROM_Cmd71_Reply[]). O conteudo exato
         * e' sensivel ao firmware, mas responder e' obrigatorio para o
         * boot avancar. Apos este comando o estado passa a PAUSE se o
         * disco for bootavel, STANDBY caso contrario.
         */
        buf_reset();
        buf_u8(0xba); buf_u8(0x06); buf_u8(0x0d);
        buf_u8(0xca); buf_u8(0x6a); buf_u8(0x1f);
        gd_device_set_state(dev,
            gd_spi_disc_readable(dev) ? GD_STATE_PAUSE : GD_STATE_STANDBY);
        gd_taskfile_begin_response(dev, &src_buf, GD_SK_NOSE);
        break;

    default:
        /*
         * Seca 5: comando SPI desconhecido. O iceGDROM responde como
         * comando ATA desconhecido: abort + status de erro.
         */
        gd_taskfile_finish_nodata(dev, GD_SK_ILLEGALREQ);
        break;
    }
}
