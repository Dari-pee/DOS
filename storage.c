#include <stdint.h>
#include "usb.h"
#include "storage.h"
#include "delay.h"

#define MAX_STOR 4

typedef struct {
    usb_dev_t *dev;
    uint8_t    iface, ep_in, ep_out;
    int        mps;
    uint8_t    tog_in, tog_out;
    uint32_t   tag;
    storage_info_t info;
} ustor_t;

typedef struct {
    uint32_t sig;
    uint32_t tag;
    uint32_t data_len;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_len;
    uint8_t  cb[16];
} __attribute__((packed)) cbw_t;

typedef struct {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;
} __attribute__((packed)) csw_t;

static ustor_t stors[MAX_STOR];
static int     nstor;

void storage_reset(void) { nstor = 0; }
int  storage_count(void) { return nstor; }

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int bulk_in(ustor_t *s, void *buf, int len)
{
    return s->dev->hc->bulk(s->dev->hc, s->dev, s->ep_in, s->mps, 1, buf, len, &s->tog_in);
}

static int bulk_out(ustor_t *s, void *buf, int len)
{
    return s->dev->hc->bulk(s->dev->hc, s->dev, s->ep_out, s->mps, 0, buf, len, &s->tog_out);
}

static void clear_halt(ustor_t *s, uint8_t ep)
{
    usb_ctrl(s->dev, 0x02, 1, 0, ep, 0, 0);
    if (ep & 0x80) s->tog_in = 0; else s->tog_out = 0;
}

static void bot_recover(ustor_t *s)
{
    usb_ctrl(s->dev, 0x21, 0xFF, 0, s->iface, 0, 0);
    delay_ms(20);
    clear_halt(s, s->ep_in);
    clear_halt(s, s->ep_out);
}

static int bot_cmd(ustor_t *s, const uint8_t *cdb, int cdb_len, void *data, uint32_t data_len, int in)
{
    cbw_t cbw;
    csw_t csw;
    int   r;

    cbw.sig      = 0x43425355;
    cbw.tag      = ++s->tag;
    cbw.data_len = data_len;
    cbw.flags    = in ? 0x80 : 0x00;
    cbw.lun      = 0;
    cbw.cb_len   = (uint8_t)cdb_len;
    for (int i = 0; i < 16; i++)
        cbw.cb[i] = (i < cdb_len) ? cdb[i] : 0;

    if (bulk_out(s, &cbw, 31) != 31) {
        bot_recover(s);
        return -1;
    }

    if (data_len) {
        r = in ? bulk_in(s, data, (int)data_len) : bulk_out(s, data, (int)data_len);
        if (r == -2)
            clear_halt(s, in ? s->ep_in : s->ep_out);
        else if (r < 0) {
            bot_recover(s);
            return -1;
        }
    }

    r = bulk_in(s, &csw, 13);
    if (r == -2) {
        clear_halt(s, s->ep_in);
        r = bulk_in(s, &csw, 13);
    }
    if (r != 13 || csw.sig != 0x53425355 || csw.tag != s->tag) {
        bot_recover(s);
        return -1;
    }
    if (csw.status == 2) {
        bot_recover(s);
        return -1;
    }
    return csw.status == 0 ? 0 : -1;
}

static void zero_cdb(uint8_t *cdb)
{
    for (int i = 0; i < 16; i++)
        cdb[i] = 0;
}

int storage_attach(usb_dev_t *d, uint8_t iface, uint8_t ep_in, uint8_t ep_out, int mps)
{
    if (nstor >= MAX_STOR)
        return -1;

    ustor_t *s = &stors[nstor];
    s->dev = d;
    s->iface = iface;
    s->ep_in = ep_in;
    s->ep_out = ep_out;
    s->mps = mps;
    s->tog_in = s->tog_out = 0;
    s->tag = 0;

    uint8_t cdb[16];
    uint8_t buf[36];
    /* idk whatt this bottom comment is claude added it for me */
    /* INQUIRY: who are you? */
    zero_cdb(cdb);
    cdb[0] = 0x12;
    cdb[4] = 36;
    if (bot_cmd(s, cdb, 6, buf, 36, 1) < 0)
        return -2;
    for (int i = 0; i < 8; i++)  s->info.vendor[i]  = (char)buf[8 + i];
    for (int i = 0; i < 16; i++) s->info.product[i] = (char)buf[16 + i];
    s->info.vendor[8] = 0;
    s->info.product[16] = 0;
    for (int i = 7; i >= 0 && s->info.vendor[i] == ' '; i--)  s->info.vendor[i] = 0;
    for (int i = 15; i >= 0 && s->info.product[i] == ' '; i--) s->info.product[i] = 0;
    int ready = 0;
    for (int tries = 0; tries < 20 && !ready; tries++) {
        zero_cdb(cdb);
        if (bot_cmd(s, cdb, 6, 0, 0, 1) == 0) {
            ready = 1;
        } else {
            uint8_t sense[18];
            zero_cdb(cdb);
            cdb[0] = 0x03;
            cdb[4] = 18;
            bot_cmd(s, cdb, 6, sense, 18, 1);
            delay_ms(100);
        }
    }
    if (!ready)
        return -3;
    zero_cdb(cdb);
    cdb[0] = 0x25;
    if (bot_cmd(s, cdb, 10, buf, 8, 1) < 0)
        return -4;
    s->info.blocks     = be32(buf) + 1;
    s->info.block_size = be32(buf + 4);
    if (s->info.block_size == 0 || s->info.block_size > 4096)
        return -5;

    nstor++;
    return 0;
}

int storage_info(int idx, storage_info_t *out)
{
    if (idx < 0 || idx >= nstor)
        return -1;
    *out = stors[idx].info;
    return 0;
}

static int storage_rw(int idx, uint32_t lba, uint32_t count, void *buf, int write)
{
    if (idx < 0 || idx >= nstor)
        return -1;

    ustor_t *s = &stors[idx];
    if (count == 0 || lba + count > s->info.blocks || lba + count < lba)
        return -1;

    uint8_t *p = buf;
    while (count) {
        uint32_t n = (count > 8) ? 8 : count;
        uint8_t  cdb[16];
        zero_cdb(cdb);
        cdb[0] = write ? 0x2A : 0x28;
        cdb[2] = (uint8_t)(lba >> 24);
        cdb[3] = (uint8_t)(lba >> 16);
        cdb[4] = (uint8_t)(lba >> 8);
        cdb[5] = (uint8_t)lba;
        cdb[7] = (uint8_t)(n >> 8);
        cdb[8] = (uint8_t)n;
        if (bot_cmd(s, cdb, 10, p, n * s->info.block_size, !write) < 0)
            return -1;
        lba   += n;
        count -= n;
        p     += n * s->info.block_size;
    }
    return 0;
}

int storage_read(int idx, uint32_t lba, uint32_t count, void *buf)
{
    return storage_rw(idx, lba, count, buf, 0);
}

int storage_write(int idx, uint32_t lba, uint32_t count, const void *buf)
{
    return storage_rw(idx, lba, count, (void *)buf, 1);
}
