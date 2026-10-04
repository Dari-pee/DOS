#include <stdint.h>
#include "usb.h"
#include "uhci.h"
#include "hid.h"
#include "storage.h"
#include "delay.h"
#include "kernel_stdio.h"

#define MAX_HC     8
#define MAX_DEV    32
#define MAX_DEPTH  4

static usb_hc_t  *hcs[MAX_HC];
static int        nhcs;
static usb_dev_t  devs[MAX_DEV];
static int        ndevs;
static int        next_addr;
static int        controllers_ready;
static int        verbose;

void usb_register_hc(usb_hc_t *hc)
{
    if (nhcs < MAX_HC)
        hcs[nhcs++] = hc;
}

int usb_ctrl(usb_dev_t *d, uint8_t type, uint8_t req, uint16_t value,
             uint16_t index, void *buf, int len)
{
    usb_setup_t s;
    s.bmRequestType = type;
    s.bRequest      = req;
    s.wValue        = value;
    s.wIndex        = index;
    s.wLength       = (uint16_t)len;
    return d->hc->control(d->hc, d, &s, buf, len);
}

static int usb_get_descriptor(usb_dev_t *d, uint8_t type, uint8_t index, void *buf, int len)
{
    return usb_ctrl(d, 0x80, 6, (uint16_t)((type << 8) | index), 0, buf, len);
}

static int usb_set_address(usb_dev_t *d, uint8_t addr)
{
    int r = usb_ctrl(d, 0x00, 5, addr, 0, 0, 0);
    if (r < 0)
        return r;
    delay_ms(10);
    d->addr = addr;
    return 0;
}

static const char *class_name(uint8_t c)
{
    switch (c) {
    case 0x01: return "Audio";
    case 0x03: return "HID";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0E: return "Video";
    default:   return "";
    }
}

static const char *speed_name(int s)
{
    return s == USB_SPEED_LOW ? "low-speed" : s == USB_SPEED_HIGH ? "high-speed" : "full-speed";
}

static void indent(int depth)
{
    for (int i = 0; i < depth * 2; i++)
        printf(" ");
}

static void make_path(char *dst, const char *parent, int port)
{
    int n = 0;
    while (parent[n]) {
        dst[n] = parent[n];
        n++;
    }
    if (n)
        dst[n++] = '.';
    if (port >= 10)
        dst[n++] = (char)('0' + port / 10);
    dst[n++] = (char)('0' + port % 10);
    dst[n] = 0;
}

static void print_config(const uint8_t *p, int total, int depth)
{
    int off = 0;
    while (off + 2 <= total) {
        uint8_t l = p[off], t = p[off + 1];
        if (l < 2)
            break;
        if (t == 4 && l >= 9) {
            indent(depth);
            printf("    interface %d: class %x.%x.%x %s\n", p[off + 2],
                   p[off + 5], p[off + 6], p[off + 7], class_name(p[off + 5]));
        } else if (t == 5 && l >= 7) {
            static const char *types[4] = { "control", "isoch", "bulk", "interrupt" };
            indent(depth);
            printf("      ep %x: %s %s, mps %d\n", p[off + 2], types[p[off + 3] & 3],
                   (p[off + 2] & 0x80) ? "IN" : "OUT", p[off + 4] | (p[off + 5] << 8));
        }
        off += l;
    }
}



typedef struct {
    uint8_t  num, cls, sub, proto;
    uint8_t  int_in, bulk_in, bulk_out;
    uint16_t int_mps, bulk_mps;
} iface_t;

static int parse_ifaces(const uint8_t *cfg, int total, iface_t *out, int max)
{
    int off = 0, n = 0;

    while (off + 2 <= total) {
        uint8_t l = cfg[off], t = cfg[off + 1];
        if (l < 2)
            break;

        if (t == 4 && l >= 9) {
            if (n >= max)
                break;
            iface_t *f = &out[n++];
            f->num   = cfg[off + 2];
            f->cls   = cfg[off + 5];
            f->sub   = cfg[off + 6];
            f->proto = cfg[off + 7];
            f->int_in = f->bulk_in = f->bulk_out = 0;
            f->int_mps = f->bulk_mps = 0;
        } else if (t == 5 && l >= 7 && n > 0) {
            iface_t *f   = &out[n - 1];
            uint8_t  ep  = cfg[off + 2];
            int      typ = cfg[off + 3] & 3;
            uint16_t mps = (uint16_t)((cfg[off + 4] | (cfg[off + 5] << 8)) & 0x7FF);

            if (typ == 3 && (ep & 0x80)) {
                if (!f->int_in) { f->int_in = ep; f->int_mps = mps; }
            } else if (typ == 2) {
                if (ep & 0x80) {
                    if (!f->bulk_in) { f->bulk_in = ep; f->bulk_mps = mps; }
                } else if (!f->bulk_out) {
                    f->bulk_out = ep;
                }
            }
        }
        off += l;
    }
    return n;
}

static void enumerate_device(usb_hc_t *hc, int speed, const char *path, int depth);

static int hub_status(usb_dev_t *hub, int port, uint16_t *status, uint16_t *change)
{
    uint8_t st[4];
    if (usb_ctrl(hub, 0xA3, 0, 0, (uint16_t)port, st, 4) < 0)
        return -1;
    *status = (uint16_t)(st[0] | (st[1] << 8));
    *change = (uint16_t)(st[2] | (st[3] << 8));
    return 0;
}

static void hub_port(usb_dev_t *hub, int port, const char *path, int depth)
{
    uint16_t status, change;

    if (hub_status(hub, port, &status, &change) < 0 || !(status & 0x0001))
        return;

    usb_ctrl(hub, 0x23, 1, 16, (uint16_t)port, 0, 0);

    usb_ctrl(hub, 0x23, 3, 4, (uint16_t)port, 0, 0);
    for (int i = 0; i < 50; i++) {
        delay_ms(10);
        if (hub_status(hub, port, &status, &change) < 0)
            return;
        if (change & 0x0010)
            break;
    }
    usb_ctrl(hub, 0x23, 1, 20, (uint16_t)port, 0, 0);

    if (hub_status(hub, port, &status, &change) < 0 || !(status & 0x0002)) {
        printf("  hub port %d: device did not enable\n", port);
        return;
    }
    delay_ms(10);

    int speed = (status & 0x0200) ? USB_SPEED_LOW :
                (status & 0x0400) ? USB_SPEED_HIGH : USB_SPEED_FULL;

    char child[16];
    make_path(child, path, port);
    enumerate_device(hub->hc, speed, child, depth + 1);
}

static void hub_probe(usb_dev_t *hub, const char *path, int depth)
{
    uint8_t hd[8];

    if (usb_ctrl(hub, 0xA0, 6, 0x2900, 0, hd, 7) < 0) {
        printf("  hub: could not read hub descriptor\n");
        return;
    }
    int nports = hd[2];
    if (nports > 8)
        nports = 8;
    int power_ms = hd[5] * 2;
    if (power_ms < 100)
        power_ms = 100;

    if (verbose) {
        indent(depth);
        printf("    hub with %d ports\n", nports);
    }

    for (int p = 1; p <= nports; p++)
        usb_ctrl(hub, 0x23, 3, 8, (uint16_t)p, 0, 0);
    delay_ms(power_ms);

    for (int p = 1; p <= nports; p++)
        hub_port(hub, p, path, depth);
}

static void attach_interfaces(usb_dev_t *d, const uint8_t *cfg, int total,
                              const char *path, int depth)
{
    iface_t ifs[6];
    int n = parse_ifaces(cfg, total, ifs, 6);
    int configured = 0, hub_done = 0;

    for (int i = 0; i < n; i++) {
        iface_t *f = &ifs[i];
        int hid = (f->cls == 3 && f->sub == 1 && (f->proto == 1 || f->proto == 2) && f->int_in);
        int msc = (f->cls == 8 && f->sub == 6 && f->proto == 0x50 && f->bulk_in && f->bulk_out);
        int hub = (f->cls == 9 && !hub_done);

        if (!hid && !msc && !hub)
            continue;

        if (!configured) {
            if (usb_ctrl(d, 0x00, 9, cfg[5], 0, 0, 0) < 0)
                return;
            configured = 1;
        }

        if (hid) {
            hid_attach(d, f->num, f->proto, f->int_in, f->int_mps);
        } else if (msc) {
            int r = storage_attach(d, f->num, f->bulk_in, f->bulk_out, f->bulk_mps);
            if (r < 0)
                printf("  storage: setup failed (code %d)\n", r);
        } else {
            hub_done = 1;
            hub_probe(d, path, depth);
        }
    }
}

static void enumerate_device(usb_hc_t *hc, int speed, const char *path, int depth)
{
    if (ndevs >= MAX_DEV || depth > MAX_DEPTH)
        return;

    usb_dev_t *d = &devs[ndevs++];
    d->hc    = hc;
    d->addr  = 0;
    d->speed = (uint8_t)speed;
    d->port  = 0;
    d->mps0  = 8;

    uint8_t desc[18];

    if (usb_get_descriptor(d, 1, 0, desc, 8) < 0) {
        printf("  port %s: device did not answer\n", path);
        ndevs--;
        return;
    }
    d->mps0 = desc[7];
    if (d->mps0 != 8 && d->mps0 != 16 && d->mps0 != 32 && d->mps0 != 64) {
        printf("  port %s: bad ep0 packet size %d\n", path, d->mps0);
        ndevs--;
        return;
    }

    if (usb_set_address(d, (uint8_t)next_addr) < 0) {
        printf("  port %s: SET_ADDRESS failed\n", path);
        ndevs--;
        return;
    }
    next_addr++;

    if (usb_get_descriptor(d, 1, 0, desc, 18) < 0) {
        printf("  port %s: could not read device descriptor\n", path);
        ndevs--;
        return;
    }
    d->vid          = (uint16_t)(desc[8] | (desc[9] << 8));
    d->pid          = (uint16_t)(desc[10] | (desc[11] << 8));
    d->dev_class    = desc[4];
    d->dev_subclass = desc[5];
    d->dev_proto    = desc[6];

    if (verbose) {
        indent(depth);
        printf("  port %s: %s device, addr %d, %x:%x, ep0 mps %d, class %x.%x.%x\n",
               path, speed_name(speed), d->addr, d->vid, d->pid, d->mps0,
               d->dev_class, d->dev_subclass, d->dev_proto);
    }

    uint8_t cfg[256];
    if (usb_get_descriptor(d, 2, 0, cfg, 9) < 0)
        return;
    int total = cfg[2] | (cfg[3] << 8);
    if (total > 256) total = 256;
    if (total < 9)   total = 9;
    if (usb_get_descriptor(d, 2, 0, cfg, total) < 0)
        return;

    if (verbose)
        print_config(cfg, total, depth);
    attach_interfaces(d, cfg, total, path, depth);
}

static void scan(int v)
{
    verbose = v;

    if (!controllers_ready) {
        uhci_init_all();
        controllers_ready = 1;
    }

    if (nhcs == 0) {
        printf("USB: no UHCI controller found\n");
        return;
    }

    ndevs = 0;
    next_addr = 1;

    for (int i = 0; i < nhcs; i++)
        if (hcs[i]->int_close_all)
            hcs[i]->int_close_all(hcs[i]);
    hid_reset();
    storage_reset();

    for (int i = 0; i < nhcs; i++) {
        usb_hc_t *hc = hcs[i];
        if (verbose)
            printf("%s controller %d: %d ports\n", hc->name, i, hc->nports);
        for (int p = 0; p < hc->nports; p++) {
            int speed = hc->port_reset(hc, p);
            if (speed < 0)
                continue;
            char path[16];
            make_path(path, "", p);
            enumerate_device(hc, speed, path, 0);
        }
    }

    printf("USB: %d device(s), %d HID input, %d disk(s)\n", ndevs, hid_count(), storage_count());
}

void usb_scan(void) { scan(1); }
void usb_init(void) { scan(0); }
