/* SPDX-License-Identifier: Apache-2.0 */
/*
 * gd_taskfile.c - Camada L2.
 *
 * Implementa os pontos fixos do fluxo da spec, seca 7.1. A sequencia e
 * imposta pela spec e nao negociavel; o que o codigo faz e tornar essa
 * sequencia explicita e auditavel.
 *
 * Fluxo (spec seca 7.1, passos 1-9):
 *   1. Host espera BSY=0 && DRQ=0
 *   2. Host escreve Features / Sector Count / Byte Count / Drive Select
 *   3. Host escreve 0xA0 no Command register; drive poe BSY em <=400 ns
 *   4. Drive poe CoD=1, IO=0, BSY=0, DRQ=1  -> "packet can be received"
 *   5. Host escreve 6 palavras de 16 bits no registo Data
 *   6. Drive limpa DRQ, poe BSY, le Features + Byte Count
 *   7. Drive executa; para dados: Byte Count, IO=1, CoD=0, DRQ=1, BSY=0, INTRQ=1
 *   8. Host le Status (derruba INTRQ), le Byte Count, transfere
 *   9. Fim: IO=CoD=DRDY=1, BSY=DRQ=0, INTRQ=1
 */
#include <string.h>
#include "gd_taskfile.h"
#include "gd_spi.h"

uint8_t gd_device_state(const gd_device_t *dev)
{
    return dev->reg[GD_R_SECTORNUM] & GD_SN_STATUS_MASK;
}

void gd_device_set_state(gd_device_t *dev, uint8_t state)
{
    dev->reg[GD_R_SECTORNUM] =
        (uint8_t)((dev->reg[GD_R_SECTORNUM] & GD_SN_FORMAT_MASK) |
                  (state & GD_SN_STATUS_MASK));
}

static void set_bits(gd_device_t *dev, uint8_t reg, uint8_t mask, int on)
{
    if (on) dev->reg[reg] |= mask;
    else    dev->reg[reg] &= (uint8_t)~mask;
}

void gd_taskfile_set_status(gd_device_t *dev, uint8_t bit, int on)
{
    set_bits(dev, GD_R_STATUS, bit, on);
}

void gd_taskfile_set_error(gd_device_t *dev, uint8_t sensekey)
{
    dev->sensekey = sensekey;
    dev->reg[GD_R_ERROR] = (uint8_t)((sensekey << GD_ERR_SENSEKEY_SHIFT) |
                                     GD_ERR_ABRT);
    set_bits(dev, GD_R_STATUS, GD_ST_CHECK, 1);
}

void gd_taskfile_clear_error(gd_device_t *dev)
{
    dev->sensekey = GD_SK_NOSE;
    dev->asc = 0;
    dev->ascq = 0;
    dev->reg[GD_R_ERROR] = 0;
    set_bits(dev, GD_R_STATUS, GD_ST_CHECK, 0);
}

static void raise_irq(gd_device_t *dev)
{
    /* Device Control bit 1 (nIEN) = 1 coloca INTRQ em hi-Z. */
    if (dev->devcontrol & GD_DC_NIEN) {
        dev->irq = 0;
        if (dev->hal->assert_irq) dev->hal->assert_irq(dev, 0);
        return;
    }
    dev->irq = 1;
    if (dev->hal->assert_irq) dev->hal->assert_irq(dev, 1);
}

/* Recalcula Status e Interrupt Reason a partir do estado interno. */
void gd_taskfile_sync_status(gd_device_t *dev)
{
    uint8_t st = 0, ir = 0;

    switch (dev->phase) {
    case GD_PHASE_PACKET_RECV:
        /* CoD=1 (e' um comando), IO=0 (host->device), DRQ=1 */
        ir  = (uint8_t)(GD_IR_COD);
        st  = (uint8_t)(GD_ST_DRDY | GD_ST_DRQ);
        break;

    case GD_PHASE_DATA_IN:
        /* CoD=0 (sao dados), IO=1 (device->host), DRQ=1 */
        ir  = (uint8_t)(GD_IR_IO);
        st  = (uint8_t)(GD_ST_DRDY | GD_ST_DRQ);
        break;

    case GD_PHASE_IDLE:
    default:
        st  = (uint8_t)(GD_ST_DRDY);
        break;
    }

    dev->reg[GD_R_INTREASON] = ir;

    /*
     * CHECK tem de sobreviver a esta reconstrucao. Sem isto, um erro
     * sinalizado antes de terminar a transferencia era apagado pela
     * propria transicao que o deveria confirmar - e o host nunca via o
     * CHECK, so looping. O mesmo vale para DF e DSC.
     */
    st = (uint8_t)(st | (dev->reg[GD_R_STATUS] &
                         (GD_ST_CHECK | GD_ST_DF | GD_ST_DSC | GD_ST_CORR)));

    dev->reg[GD_R_STATUS] = st;
    dev->altstatus        = st;
}

void gd_taskfile_end_transfer(gd_device_t *dev)
{
    if (dev->src && dev->src->close) dev->src->close(dev);
    dev->src       = NULL;
    dev->remaining = 0;
    dev->phase     = GD_PHASE_IDLE;

    /* Seca 7.1 passo 9: IO=CoD=DRDY=1, BSY=DRQ=0, e INTRQ de novo. */
    gd_taskfile_sync_status(dev);
    dev->reg[GD_R_INTREASON] = (uint8_t)(GD_IR_COD | GD_IR_IO);
    raise_irq(dev);
}

void gd_taskfile_finish_nodata(gd_device_t *dev, uint8_t sensekey)
{
    dev->reg[GD_R_BYTECOUNTL] = 0;
    dev->reg[GD_R_BYTECOUNTH] = 0;

    if (sensekey != GD_SK_NOSE) {
        gd_taskfile_set_error(dev, sensekey);
    }
    gd_taskfile_end_transfer(dev);
}

void gd_taskfile_begin_response(gd_device_t *dev, const gd_source_t *src,
                                uint8_t sensekey)
{
    uint16_t bc = 0;

    if (!src) {
        gd_taskfile_finish_nodata(dev, sensekey);
        return;
    }

    dev->src = src;
    dev->transferred = 0;
    dev->remaining = 0;

    if (src->open && src->open(dev, &bc) != 0) {
        /* O produtor nao conseguiu preparar-se. */
        dev->src = NULL;
        gd_taskfile_finish_nodata(dev, GD_SK_MEDIUMERR);
        return;
    }

    /* Seca 7.1 passo 7: o Byte Count carreg-o host para saber o tamanho. */
    dev->reg[GD_R_BYTECOUNTL] = (uint8_t)(bc & 0xff);
    dev->reg[GD_R_BYTECOUNTH] = (uint8_t)(bc >> 8);
    dev->remaining = bc;

    if (bc == 0) {
        gd_taskfile_finish_nodata(dev, sensekey);
        return;
    }

    if (sensekey != GD_SK_NOSE) {
        gd_taskfile_set_error(dev, sensekey);
    }

    dev->phase = GD_PHASE_DATA_IN;
    gd_taskfile_sync_status(dev);
    raise_irq(dev);

    if (dev->hal->start_data_in) dev->hal->start_data_in(dev, bc);
}

/* ------------------------------------------------------------------ */
/* Registo Data: o unico de 16 bits                                    */
/* ------------------------------------------------------------------ */

uint16_t gd_taskfile_take_data(gd_device_t *dev)
{
    return dev->data;
}

/*
 * Puxa ate `nwords` palavras do produtor de dados. Usado pelo backend
 * durante a fase de dados de saida.
 *
 * Quando o Byte Count e' esgotado, fecha a fonte e sinaliza o fim da
 * transferencia (spec seca 7.1 passo 9).
 */
uint32_t gd_taskfile_read_data(gd_device_t *dev, uint16_t *dst, uint32_t nwords)
{
    uint32_t got;

    if (dev->phase != GD_PHASE_DATA_IN || !dev->src) return 0;
    if (dev->remaining == 0) return 0;

    /*
     * Nunca pedir mais do que resta. `remaining` ja' e' o total em falta,
     * logo o clamp e' sobre ele directamente - comparar contra
     * transferred + remaining truncava a transferencia a meio e deixava o
     * device permanentemente em DATA_IN.
     */
    while (nwords && nwords * 2 > dev->remaining) nwords--;

    for (uint32_t i = 0; i < nwords; i++) dst[i] = 0;

    got = dev->src->read(dev, dst, nwords);
    if (got == 0) {
        /*
         * O produtor ficou sem dados com o Byte Count por satisfazer.
         * Nao ha recovery para isto na spec: encrava-se a transacao e
         * sinaliza-se o fim, para o host nao ficar a_wait por dados que
         * nunca virao. Better to fail loudly than to hang.
         */
        gd_taskfile_end_transfer(dev);
        return 0;
    }

    dev->transferred += got * 2;
    dev->remaining    -= got * 2;

    if (dev->remaining == 0) {
        gd_taskfile_end_transfer(dev);
    }
    return got;
}

void gd_taskfile_put_data(gd_device_t *dev, uint16_t w)
{
    dev->data = w;

    if (dev->phase == GD_PHASE_PACKET_RECV) {
        /* Seca 7.1 passo 5: 6 palavras = 12 bytes, big-endian dentro da
         * palavra (o host escreve o byte mais significativo primeiro). */
        dev->packet[dev->packet_words * 2 + 0] = (uint8_t)(w >> 8);
        dev->packet[dev->packet_words * 2 + 1] = (uint8_t)(w & 0xff);
        dev->packet_words++;

        if (dev->packet_words >= GD_PKT_WORDS) {
            dev->packet_words = 0;
            dev->phase = GD_PHASE_IDLE;
            /* Seca 7.1 passo 6: limpa DRQ, poe BSY, executa. */
            gd_spi_execute(dev, dev->packet);
        }
    } else if (dev->sink) {
        /*
         * Seca 7.2 - PIO data <- host. SET_MODE e' o unico comando com
         * dados host->device, e e' known-length: o Allocation Length do
         * packet diz quantos bytes o host vai escrever.
         */
        uint32_t i;
        for (i = 0; i < 2; i++) {
            if (dev->sink_got >= dev->sink_len) break;
            dev->sink[dev->sink_got++] = i ? (uint8_t)(w & 0xff)
                                           : (uint8_t)(w >> 8);
        }
        if (dev->sink_got >= dev->sink_len) {
            dev->sink = NULL;
            dev->sink_got = 0;
            dev->sink_len = 0;
            if (dev->sink_done) dev->sink_done(dev);
            gd_taskfile_finish_nodata(dev, GD_SK_NOSE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Registos de controlo                                                 */
/* ------------------------------------------------------------------ */

void gd_taskfile_write_reg(gd_device_t *dev, uint8_t reg, uint8_t value)
{
    switch (reg) {
    case GD_R_ERROR:
    case GD_R_FEATURES:
    case GD_R_SECTORNUM:
    case GD_R_DRIVESEL:
        dev->reg[reg] = value;
        break;

    case GD_R_BYTECOUNTL:
        dev->reg[reg] = value;
        break;
    case GD_R_BYTECOUNTH:
        dev->reg[reg] = value;
        break;

    case GD_R_STATUS:
    case GD_R_INTREASON:
        /* Read-only no device. Escreve-la e' ignorada. */
        break;

    default:
        break;
    }
}

void gd_taskfile_clear_irq(gd_device_t *dev)
{
    if (dev->irq) {
        dev->irq = 0;
        if (dev->hal->assert_irq) dev->hal->assert_irq(dev, 0);
    }
}

uint8_t gd_taskfile_read_altstatus(gd_device_t *dev)
{
    /* Nao limpa o INTRQ. Ver a nota em gd_taskfile.h. */
    return dev->altstatus;
}

uint8_t gd_taskfile_read_reg(gd_device_t *dev, uint8_t reg)
{
    switch (reg) {
    case GD_R_STATUS:
        /* So a leitura de Status limpa o INTRQ. */
        gd_taskfile_clear_irq(dev);
        return dev->reg[GD_R_STATUS];

    case GD_R_INTREASON:
        return dev->reg[GD_R_INTREASON];

    default:
        return dev->reg[reg];
    }
}

/* ------------------------------------------------------------------ */
/* Comandos ATA (spec seca 3, Tabela 3.3)                              */
/* ------------------------------------------------------------------ */

static void taskfile_init(gd_device_t *dev)
{
    /* Seca 3.3.1.1 - valores de task file apos power-on / hard reset. */
    dev->reg[GD_R_STATUS]     = GD_RESET_STATUS;
    dev->reg[GD_R_ERROR]      = GD_RESET_ERROR;
    dev->reg[GD_R_BYTECOUNTL] = GD_RESET_SECTORCNT;
    dev->reg[GD_R_BYTECOUNTH] = 0;
    dev->reg[GD_R_SECTORNUM]  = GD_RESET_SECTORNUM;
    dev->reg[GD_R_FEATURES]   = 0;
    dev->reg[GD_R_DRIVESEL]   = GD_RESET_DRIVEHEAD;
    dev->reg[GD_R_INTREASON]  = 0;
    dev->altstatus            = GD_RESET_STATUS;
    dev->phase                = GD_PHASE_IDLE;
    dev->packet_words         = 0;
    dev->src                  = NULL;
    dev->remaining            = 0;
    dev->transferred          = 0;
    dev->sink                 = NULL;
    dev->sink_len             = 0;
    dev->sink_got             = 0;
    dev->sensekey             = GD_SK_NOSE;
    dev->asc                  = 0;
    dev->ascq                 = 0;
    dev->cur_fad              = 0;
    dev->irq                  = 0;
}

void gd_taskfile_reset(gd_device_t *dev)
{
    taskfile_init(dev);
    gd_cdda_reset(dev);
    if (dev->hal->assert_irq) dev->hal->assert_irq(dev, 0);
}

void gd_taskfile_command(gd_device_t *dev, uint8_t cmd)
{
    dev->pending_command = cmd;

    switch (cmd) {
    case GD_CMD_NOP:
        /*
         * Seca 5: NOP e' o terminador de comando e e aceite mesmo com
         * BSY=1. Nao produz dados e nao muda de estado.
         */
        return;

    case GD_CMD_SOFTRESET:
        /*
         * Seca 3.3.1.2: pode ser emitido mesmo com BSY=1; o unico status
         * reportado e' BUSY; a task file reinicializa-se como no power-on
         * EXCEPTO o bit DRV, que permanece inalterado.
         *
         * Byte Count = 0x14 / 0xEB. A spec nao o diz; o GD-ROM real
         * escreve-o e o comment no Flycast e "DC Checker expects these
         * values". Sem isto o boot falha num verificador.
         */
        {
            uint8_t drv = dev->reg[GD_R_DRIVESEL] & 0x80;
            taskfile_init(dev);
            dev->reg[GD_R_DRIVESEL] = drv;
            dev->reg[GD_R_BYTECOUNTL] = 0x14;
            dev->reg[GD_R_BYTECOUNTH] = 0xeb;
        }
        return;

    case GD_CMD_EXECDIAG:
        /*
         * Seca 3.3.1, Tabela 3.4. O registo Error devolve o codigo de
         * diagnostico; 01h = Normal. O GD-ROM e' sempre device 0, logo
         * o codigo de byte alto e' zero.
         */
        dev->reg[GD_R_ERROR] = GD_DIAG_NORMAL;
        dev->reg[GD_R_BYTECOUNTL] = 0;
        dev->reg[GD_R_BYTECOUNTH] = 0;
        dev->phase = GD_PHASE_IDLE;
        set_bits(dev, GD_R_STATUS, GD_ST_CHECK, 0);
        gd_taskfile_sync_status(dev);
        raise_irq(dev);
        return;

    case GD_CMD_PACKET:
        /*
         * Seca 7.1 passo 3-4: BSY dentro de 400 ns, depois CoD=1,
         * IO=0, BSY=0, DRQ=1.
         */
        dev->packet_words = 0;
        dev->reg[GD_R_ERROR] = 0;
        set_bits(dev, GD_R_STATUS, GD_ST_CHECK, 0);
        dev->phase = GD_PHASE_PACKET_RECV;
        gd_taskfile_sync_status(dev);
        return;

    case GD_CMD_IDDEV:
        /* Sempre PIO (spec seca 3). Treatado pela camada SPI. */
        gd_spi_identify(dev);
        return;

    case GD_CMD_SETFEATURE:
        /* So modo de transferencia (spec seca 3). */
        dev->phase = GD_PHASE_IDLE;
        gd_taskfile_sync_status(dev);
        raise_irq(dev);
        return;

    default:
        /*
         * Seca 5, comando desconhecido: abort no Error, error no Status,
         * clear de busy e INTRQ. E' literalmente o que a spec descreve.
         */
        dev->reg[GD_R_ERROR] = (uint8_t)GD_ERR_ABRT;
        dev->reg[GD_R_BYTECOUNTL] = 0;
        dev->reg[GD_R_BYTECOUNTH] = 0;
        dev->phase = GD_PHASE_IDLE;
        set_bits(dev, GD_R_STATUS, GD_ST_CHECK, 1);
        gd_taskfile_sync_status(dev);
        set_bits(dev, GD_R_STATUS, GD_ST_CHECK, 1);
        raise_irq(dev);
        return;
    }
}
