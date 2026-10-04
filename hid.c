#include <stdint.h>
#include "usb.h"
#include "hid.h"
#include "mouse.h"

#define MAX_HID 4
#define KQ_SIZE 32

typedef struct {
    usb_hc_t *hc;
    void     *handle;
    uint8_t   proto;
    uint8_t   prev[8];
} hid_dev_t;

static hid_dev_t hids[MAX_HID];
static int       nhids;
static int       hid_caps;
static char      kq[KQ_SIZE];
static int       kq_head, kq_tail;
static const char key_normal[] =
    "abcdefghijklmnopqrstuvwxyz" "1234567890" "\n\x1b\b\t " "-=[]\\" "#" ";'`,./";
static const char key_shift[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "!@#$%^&*()" "\n\x1b\b\t " "_+{}|" "~" ":\"~<>?";

void hid_reset(void)
{
    nhids = 0;
    kq_head = kq_tail = 0;
}

int hid_count(void)
{
    return nhids;
}

int hid_attach(usb_dev_t *d, uint8_t iface, uint8_t proto, uint8_t ep, int mps)
{
    if (nhids >= MAX_HID || !d->hc->int_open)
        return -1;

    usb_ctrl(d, 0x21, 0x0B, 0, iface, 0, 0);
    usb_ctrl(d, 0x21, 0x0A, 0, iface, 0, 0);

    void *h = d->hc->int_open(d->hc, d, ep, mps);
    if (!h)
        return -2;

    hid_dev_t *hd = &hids[nhids++];
    hd->hc     = d->hc;
    hd->handle = h;
    hd->proto  = proto;
    for (int i = 0; i < 8; i++)
        hd->prev[i] = 0;
    return 0;
}

static void push_key(char c)
{
    int next = (kq_head + 1) % KQ_SIZE;
    if (next != kq_tail) {
        kq[kq_head] = c;
        kq_head = next;
    }
}

static void handle_keyboard(hid_dev_t *hd, const uint8_t *r, int n)
{
    if (n < 8)
        return;

    int shift = (r[0] & 0x22) != 0;

    for (int i = 2; i < 8; i++) {
        uint8_t k = r[i];
        if (k < 4)
            continue;

        int already_down = 0;
        for (int j = 2; j < 8; j++)
            if (hd->prev[j] == k)
                already_down = 1;
        if (already_down)
            continue;

        if (k == 0x39) {
            hid_caps = !hid_caps;
            continue;
        }

        if ((unsigned)(k - 4) < sizeof(key_normal) - 1) {
            int letter = (k <= 0x1D);
            int upper  = letter ? (shift != hid_caps) : shift;
            push_key(upper ? key_shift[k - 4] : key_normal[k - 4]);
        }
    }

    for (int i = 0; i < 8; i++)
        hd->prev[i] = r[i];
}

static int handle_mouse(const uint8_t *r, int n)
{
    if (n < 3)
        return 0;

    int dx  = (int8_t)r[1];
    int dy  = (int8_t)r[2];
    int btn = r[0] & 1;
    int changed = (dx != 0) || (dy != 0) || (btn != mouse_left_button);

    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0)   mouse_x = 0;
    if (mouse_x > 319) mouse_x = 319;
    if (mouse_y < 0)   mouse_y = 0;
    if (mouse_y > 199) mouse_y = 199;
    mouse_left_button = btn;
    return changed;
}

int hid_poll(void)
{
    int moved = 0;
    uint8_t rep[8];

    for (int i = 0; i < nhids; i++) {
        hid_dev_t *hd = &hids[i];
        int n = hd->hc->int_poll(hd->hc, hd->handle, rep, 8);
        if (n <= 0)
            continue;
        if (hd->proto == 1)
            handle_keyboard(hd, rep, n);
        else if (handle_mouse(rep, n))
            moved = 1;
    }
    return moved;
}

int hid_getchar(void)
{
    hid_poll();
    if (kq_head == kq_tail)
        return 0;
    char c = kq[kq_tail];
    kq_tail = (kq_tail + 1) % KQ_SIZE;
    return (unsigned char)c;
}
