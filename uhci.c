#include <stdint.h>
#include "io.h"
#include "pci.h"
#include "dma.h"
#include "delay.h"
#include "usb.h"
#include "uhci.h"
#include "kernel_stdio.h"

#define UHCI_CMD      0x00
#define UHCI_STS      0x02
#define UHCI_INTR     0x04
#define UHCI_FRNUM    0x06
#define UHCI_FRBASE   0x08
#define UHCI_SOFMOD   0x0C
#define UHCI_PORTSC   0x10

#define CMD_RS        0x0001
#define CMD_HCRESET   0x0002
#define CMD_GRESET    0x0004
#define CMD_CF        0x0040
#define CMD_MAXP      0x0080

#define STS_HALTED    0x0020

#define PORT_CONNECT  0x0001
#define PORT_CONNCHG  0x0002
#define PORT_ENABLE   0x0004
#define PORT_ENCHG    0x0008
#define PORT_LOWSPEED 0x0100
#define PORT_RESET    0x0200

#define TD_ACTIVE     (1u << 23)
#define TD_STALLED    (1u << 22)
#define TD_DBUFERR    (1u << 21)
#define TD_BABBLE     (1u << 20)
#define TD_CRCTIMEO   (1u << 18)
#define TD_BITSTUFF   (1u << 17)
#define TD_ERRMASK    (TD_STALLED | TD_DBUFERR | TD_BABBLE | TD_CRCTIMEO | TD_BITSTUFF)
#define TD_IOC        (1u << 24)
#define TD_LS         (1u << 26)
#define TD_SPD        (1u << 29)
#define TD_CERR3      (3u << 27)

#define PID_SETUP     0x2D
#define PID_IN        0x69
#define PID_OUT       0xE1

#define LINK_TERM     0x1u
#define LINK_QH       0x2u
#define LINK_DEPTH    0x4u

#define MAX_TDS       40
#define BUF_SIZE      256
#define MAX_UHCI      4

typedef struct {
    volatile uint32_t link;
    volatile uint32_t ctrl;
    volatile uint32_t token;
    volatile uint32_t buffer;
    uint32_t pad[4];
} __attribute__((aligned(32))) uhci_td_t;

typedef struct {
    volatile uint32_t link;
    volatile uint32_t element;
    uint32_t pad[2];
} __attribute__((aligned(16))) uhci_qh_t;

#define MAX_INT 4

typedef struct {
    uhci_qh_t *qh;
    uhci_td_t *td;
    uint8_t   *buf;
    uint8_t    addr, ep, low, mps;
    uint32_t   toggle;
} uhci_int_t;

typedef struct {
    uint16_t     io;
    uint32_t    *frames;
    uhci_qh_t   *qh_head;
    uhci_qh_t   *qh;
    uhci_td_t   *tds;
    uint8_t     *setup;
    uint8_t     *buf;
    uhci_int_t   ints[MAX_INT];
    int          nints;
    usb_hc_t     hc;
} uhci_t;

static uhci_t uhci_ctrl[MAX_UHCI];
static int    uhci_count;

static uint32_t td_token(uint32_t pid, uint32_t addr, uint32_t toggle, uint32_t len)
{
    return ((len - 1u) << 21) | (toggle << 19) | (addr << 8) | pid;
}


static int uhci_port_reset(usb_hc_t *hc, int port)
{
    uhci_t  *u   = hc->priv;
    uint16_t reg = (uint16_t)(u->io + UHCI_PORTSC + port * 2);

    if (!(inw(reg) & PORT_CONNECT))
        return -1;

    outw(reg, PORT_CONNCHG | PORT_ENCHG);

    outw(reg, PORT_RESET);
    delay_ms(50);
    outw(reg, 0);
    delay_ms(10);

    outw(reg, PORT_ENABLE | PORT_CONNCHG | PORT_ENCHG);
    uint16_t st = 0;
    for (int i = 0; i < 100; i++) {
        st = inw(reg);
        if (st & PORT_ENABLE)
            break;
        delay_ms(1);
    }
    if (!(st & PORT_CONNECT) || !(st & PORT_ENABLE))
        return -2;

    delay_ms(10);
    return (st & PORT_LOWSPEED) ? USB_SPEED_LOW : USB_SPEED_FULL;
}

static int uhci_control(usb_hc_t *hc, usb_dev_t *d, const usb_setup_t *s, void *data, int len)
{
    uhci_t    *u   = hc->priv;
    uhci_td_t *td  = u->tds;
    int        in  = (s->bmRequestType & 0x80) != 0;
    int        mps = d->mps0;
    uint32_t   base = TD_ACTIVE | TD_CERR3 | ((d->speed == USB_SPEED_LOW) ? TD_LS : 0);
    int        n = 0;

    if (len < 0 || len > BUF_SIZE)
        return -1;
    if (2 + (len + mps - 1) / mps > MAX_TDS)
        return -1;

    const uint8_t *sp = (const uint8_t *)s;
    for (int i = 0; i < 8; i++)
        u->setup[i] = sp[i];
    if (!in && len)
        for (int i = 0; i < len; i++)
            u->buf[i] = ((uint8_t *)data)[i];

    td[n].ctrl   = base;
    td[n].token  = td_token(PID_SETUP, d->addr, 0, 8);
    td[n].buffer = (uint32_t)u->setup;
    n++;

    uint32_t toggle = 1;
    for (int off = 0; off < len; off += mps) {
        int chunk = (len - off < mps) ? (len - off) : mps;
        td[n].ctrl   = base;
        td[n].token  = td_token(in ? PID_IN : PID_OUT, d->addr, toggle, (uint32_t)chunk);
        td[n].buffer = (uint32_t)(u->buf + off);
        toggle ^= 1;
        n++;
    }

    td[n].ctrl   = base | TD_IOC;
    td[n].token  = td_token(in ? PID_OUT : PID_IN, d->addr, 1, 0);
    td[n].buffer = 0;
    n++;

    for (int i = 0; i < n - 1; i++)
        td[i].link = (uint32_t)&td[i + 1] | LINK_DEPTH;
    td[n - 1].link = LINK_TERM;

    u->qh->element = (uint32_t)&td[0];

    int err = 0, timeout = 1;
    int bad = -1;
    for (int t = 0; t < 500; t++) {
        int active = 0;
        for (int i = 0; i < n; i++) {
            uint32_t c = td[i].ctrl;
            if (c & TD_ACTIVE) { active = 1; break; }
            if (c & TD_ERRMASK) { err = 1; bad = i; break; }
        }
        if (err) { timeout = 0; break; }
        if (!active) { timeout = 0; break; }
        delay_ms(1);
    }

    u->qh->element = LINK_TERM;

    if (timeout || err) {
        delay_ms(2);
        printf("uhci: control transfer failed (%s, td %d, ctrl %x)\n",
               timeout ? "timeout" : "error", bad, bad >= 0 ? td[bad].ctrl : 0);
        return -1;
    }

    if (in && len)
        for (int i = 0; i < len; i++)
            ((uint8_t *)data)[i] = u->buf[i];
    return len;
}

static int uhci_bulk(usb_hc_t *hc, usb_dev_t *d, uint8_t ep, int mps, int in,
                     void *data, int len, uint8_t *toggle)
{
    uhci_t    *u    = hc->priv;
    uhci_td_t *td   = u->tds;
    uint32_t   base = TD_ACTIVE | TD_CERR3 | (in ? TD_SPD : 0) |
                      ((d->speed == USB_SPEED_LOW) ? TD_LS : 0);
    uint8_t   *p    = data;
    int        total = 0;

    if (mps < 8 || mps > 64 || len <= 0)
        return len == 0 ? 0 : -1;

    while (len > 0) {
        int      n = 0, batch = 0;
        uint32_t tg = *toggle;

        while (len - batch > 0 && n < MAX_TDS) {
            int chunk = (len - batch < mps) ? (len - batch) : mps;
            td[n].ctrl   = base;
            td[n].token  = ((uint32_t)(chunk - 1) << 21) | (tg << 19) |
                           ((uint32_t)(ep & 0x0F) << 15) | ((uint32_t)d->addr << 8) |
                           (in ? PID_IN : PID_OUT);
            td[n].buffer = (uint32_t)(p + batch);
            tg ^= 1;
            batch += chunk;
            n++;
        }
        for (int i = 0; i < n - 1; i++)
            td[i].link = (uint32_t)&td[i + 1] | LINK_DEPTH;
        td[n - 1].link = LINK_TERM;
        td[n - 1].ctrl |= TD_IOC;

        u->qh->element = (uint32_t)&td[0];

        int done = 0, err = 0, packets = 0, short_pkt = 0;
        for (int t = 0; t < 5000 && !done; t++) {
            packets = 0;
            for (int i = 0; i < n; i++) {
                uint32_t c = td[i].ctrl;
                if (c & TD_ACTIVE)
                    break;
                if (c & TD_ERRMASK) {
                    err = (c & TD_STALLED) ? -2 : -1;
                    done = 1;
                    break;
                }
                packets++;
                if (in && ((c + 1) & 0x7FF) < (((td[i].token >> 21) + 1) & 0x7FF)) {
                    short_pkt = 1;
                    done = 1;
                    break;
                }
            }
            if (!done && packets == n)
                done = 1;
            if (!done)
                delay_ms(1);
        }

        u->qh->element = LINK_TERM;
        if (!done) {
            err = -1;
            delay_ms(2);
        }

        int got = 0;
        for (int i = 0; i < packets; i++)
            got += in ? (int)((td[i].ctrl + 1) & 0x7FF)
                      : (int)(((td[i].token >> 21) + 1) & 0x7FF);
        *toggle ^= (uint8_t)(packets & 1);
        total += got;
        p     += got;
        len   -= got;

        if (err)
            return err;
        if (short_pkt)
            break;
    }
    return total;
}

static void uhci_int_arm(uhci_int_t *it)
{
    it->td->link   = LINK_TERM;
    it->td->token  = (((uint32_t)it->mps - 1u) << 21) | (it->toggle << 19) |
                     ((uint32_t)it->ep << 15) | ((uint32_t)it->addr << 8) | PID_IN;
    it->td->buffer = (uint32_t)it->buf;
    it->td->ctrl   = TD_ACTIVE | TD_CERR3 | (it->low ? TD_LS : 0);
    it->qh->element = (uint32_t)it->td;
}

static void *uhci_int_open(usb_hc_t *hc, usb_dev_t *d, uint8_t ep, int mps)
{
    uhci_t *u = hc->priv;

    if (u->nints >= MAX_INT || mps < 1 || mps > 64)
        return 0;

    uhci_int_t *it = &u->ints[u->nints++];
    it->addr   = d->addr;
    it->ep     = ep & 0x0F;
    it->low    = (d->speed == USB_SPEED_LOW);
    it->mps    = (uint8_t)mps;
    it->toggle = 0;

    uhci_int_arm(it);
    it->qh->link     = u->qh_head->link;
    u->qh_head->link = (uint32_t)it->qh | LINK_QH;
    return it;
}

static int uhci_int_poll(usb_hc_t *hc, void *handle, uint8_t *out, int maxlen)
{
    uhci_int_t *it = handle;
    uint32_t c = it->td->ctrl;
    int n;
    (void)hc;

    if (c & TD_ACTIVE)
        return 0;

    if (c & TD_ERRMASK) {
        n = -1;
    } else {
        n = (int)((c + 1) & 0x7FF);
        if (n > maxlen)
            n = maxlen;
        for (int i = 0; i < n; i++)
            out[i] = it->buf[i];
        it->toggle ^= 1;
    }

    uhci_int_arm(it);
    return n;
}

static void uhci_int_close_all(usb_hc_t *hc)
{
    uhci_t *u = hc->priv;
    u->qh_head->link = (uint32_t)u->qh | LINK_QH;
    u->nints = 0;
}

static int uhci_start(uhci_t *u, uint8_t bus, uint8_t dev, uint8_t fn)
{
    uint32_t cmd = pci_read32(bus, dev, fn, 0x04) & 0xFFFF;
    pci_write32(bus, dev, fn, 0x04, cmd | 0x05);

    uint32_t bar4 = pci_read32(bus, dev, fn, 0x20);
    if (!(bar4 & 1))
        return -1;
    u->io = (uint16_t)(bar4 & 0xFFFC);
    uint32_t leg = pci_read32(bus, dev, fn, 0xC0);
    pci_write32(bus, dev, fn, 0xC0, (leg & 0xFFFF0000u) | 0x8F00);
    outw(u->io + UHCI_CMD, 0);
    for (int i = 0; i < 100 && !(inw(u->io + UHCI_STS) & STS_HALTED); i++)
        delay_ms(1);

    outw(u->io + UHCI_CMD, CMD_HCRESET);
    int i;
    for (i = 0; i < 100 && (inw(u->io + UHCI_CMD) & CMD_HCRESET); i++)
        delay_ms(1);
    if (i == 100)
        return -2;

    outw(u->io + UHCI_CMD, CMD_GRESET);
    delay_ms(50);
    outw(u->io + UHCI_CMD, 0);
    delay_ms(10);

    u->frames = dma_alloc(4096, 4096);
    u->qh     = dma_alloc(sizeof(uhci_qh_t), 16);
    u->tds    = dma_alloc(sizeof(uhci_td_t) * MAX_TDS, 32);
    u->setup  = dma_alloc(8, 16);
    u->buf    = dma_alloc(BUF_SIZE, 16);
    u->qh_head = dma_alloc(sizeof(uhci_qh_t), 16);
    uhci_qh_t *iq = dma_alloc(sizeof(uhci_qh_t) * MAX_INT, 16);
    uhci_td_t *it = dma_alloc(sizeof(uhci_td_t) * MAX_INT, 32);
    uint8_t   *ib = dma_alloc(64 * MAX_INT, 16);
    if (!u->frames || !u->qh || !u->tds || !u->setup || !u->buf ||
        !u->qh_head || !iq || !it || !ib)
        return -3;

    for (int k = 0; k < MAX_INT; k++) {
        u->ints[k].qh  = &iq[k];
        u->ints[k].td  = &it[k];
        u->ints[k].buf = ib + 64 * k;
    }
    u->nints = 0;

    u->qh->link    = LINK_TERM;
    u->qh->element = LINK_TERM;
    u->qh_head->link    = (uint32_t)u->qh | LINK_QH;
    u->qh_head->element = LINK_TERM;
    for (int f = 0; f < 1024; f++)
        u->frames[f] = (uint32_t)u->qh_head | LINK_QH;

    outw(u->io + UHCI_INTR, 0);
    outw(u->io + UHCI_FRNUM, 0);
    outl(u->io + UHCI_FRBASE, (uint32_t)u->frames);
    outb(u->io + UHCI_SOFMOD, 0x40);
    outw(u->io + UHCI_STS, 0xFFFF);
    outw(u->io + UHCI_CMD, CMD_RS | CMD_CF | CMD_MAXP);

    for (i = 0; i < 100 && (inw(u->io + UHCI_STS) & STS_HALTED); i++)
        delay_ms(1);
    if (i == 100)
        return -4;

    int ports = 0;
    while (ports < 8) {
        uint16_t v = inw(u->io + UHCI_PORTSC + ports * 2);
        if (v == 0xFFFF || !(v & 0x80))
            break;
        ports++;
    }

    u->hc.name       = "UHCI";
    u->hc.nports     = ports;
    u->hc.port_reset = uhci_port_reset;
    u->hc.control    = uhci_control;
    u->hc.bulk       = uhci_bulk;
    u->hc.int_open   = uhci_int_open;
    u->hc.int_poll   = uhci_int_poll;
    u->hc.int_close_all = uhci_int_close_all;
    u->hc.priv       = u;
    return 0;
}

int uhci_init_all(void)
{
    uint8_t bus, dev, fn;

    for (int idx = 0; idx < MAX_UHCI; idx++) {
        if (pci_find_class(0x0C, 0x03, 0x00, idx, &bus, &dev, &fn) < 0)
            break;

        uhci_t *u = &uhci_ctrl[uhci_count];
        int r = uhci_start(u, bus, dev, fn);
        if (r < 0) {
            printf("UHCI at %x:%x.%x failed to start (code %d)\n", bus, dev, fn, r);
            continue;
        }
        usb_register_hc(&u->hc);
        uhci_count++;
    }
    return uhci_count;
}
