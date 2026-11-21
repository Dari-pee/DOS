#include <stdint.h>
#include <stddef.h>
#include "pci.h"
#include "timer.h"
#include "usb_msc.h"

/* Minimal EHCI + USB Mass Storage Bulk-Only Transport (BOT), LUN 0. */
#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI  0x20
#define PCI_COMMAND      0x04
#define PCI_BAR0         0x10

#define EHCI_CAPLENGTH   0x00
#define EHCI_HCSPARAMS   0x04
#define EHCI_HCCPARAMS   0x08
#define EHCI_USBCMD      0x00
#define EHCI_USBSTS      0x04
#define EHCI_ASYNCLIST   0x18
#define EHCI_CONFIGFLAG  0x40
#define EHCI_PORTSC      0x44
#define CMD_RUN          (1u << 0)
#define CMD_RESET        (1u << 1)
#define CMD_ASYNC        (1u << 5)
#define STS_HALTED       (1u << 12)
#define PORT_CONNECT     (1u << 0)
#define PORT_ENABLE      (1u << 2)
#define PORT_RESET       (1u << 8)
#define PORT_POWER       (1u << 12)

#define QTD_TERMINATE    1u
#define QTD_ACTIVE       (1u << 7)
#define QTD_HALTED       (1u << 6)
#define QTD_DATABUFERR   (1u << 5)
#define QTD_BABBLE       (1u << 4)
#define QTD_XACTERR      (1u << 3)
#define QTD_PID_OUT      0u
#define QTD_PID_IN       1u
#define QTD_PID_SETUP    2u
#define QTD_CERR3        (3u << 10)
#define QTD_IOC          (1u << 15)
#define QH_LINK_TYPE     2u
#define EHCI_TIMEOUT_MS  1000u

typedef struct __attribute__((packed, aligned(32))) {
    volatile uint32_t next;
    volatile uint32_t alternate;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t ext_buffer[5];
} ehci_qtd_t;

typedef struct __attribute__((packed, aligned(32))) {
    volatile uint32_t horizontal;
    volatile uint32_t endpoint;
    volatile uint32_t endpoint_caps;
    volatile uint32_t current;
    volatile uint32_t next;
    volatile uint32_t alternate;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t ext_buffer[5];
} ehci_qh_t;

typedef struct __attribute__((packed)) {
    uint8_t request_type, request;
    uint16_t value, index, length;
} usb_setup_t;

typedef struct __attribute__((packed)) {
    uint32_t signature, tag, data_length;
    uint8_t flags, lun, command_length, command[16];
} bot_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t signature, tag, residue;
    uint8_t status;
} bot_csw_t;

static volatile uint8_t *mmio;
static volatile uint8_t *opregs;
static uint8_t port_count;
static uint8_t device_address;
static uint8_t ep_in, ep_out;
static uint16_t bulk_mps;
static uint8_t in_toggle, out_toggle;
static uint32_t bot_tag;
static uint32_t sector_count;
static uint32_t sector_size;
static uint8_t active;

static ehci_qh_t async_qh __attribute__((aligned(32)));
static ehci_qh_t transfer_qh __attribute__((aligned(32)));
static ehci_qtd_t transfer_tds[3] __attribute__((aligned(32)));
static uint8_t io_buffer[4096] __attribute__((aligned(4096)));
static bot_cbw_t cbw __attribute__((aligned(32)));
static bot_csw_t csw __attribute__((aligned(32)));
static uint8_t setup_buffer[8] __attribute__((aligned(32)));

static uint32_t mmio_read32(uint32_t offset)
{
    return *(volatile uint32_t *)(opregs + offset);
}

static void mmio_write32(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(opregs + offset) = value;
}

static uint32_t cap_read32(uint32_t offset)
{
    return *(volatile uint32_t *)(mmio + offset);
}

static void delay_tick(void)
{
    timer_sleep_ms(1);
}

static int wait_mask(uint32_t offset, uint32_t mask, uint32_t expected)
{
    for (uint32_t i = 0; i < EHCI_TIMEOUT_MS; i++) {
        if ((mmio_read32(offset) & mask) == expected)
            return 0;
        delay_tick();
    }
    return -1;
}

static int pci_find_ehci(uint8_t *bus_out, uint8_t *slot_out, uint8_t *fn_out)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read32((uint8_t)bus, slot, 0, 0);
            if ((uint16_t)id == 0xFFFFu)
                continue;
            uint8_t funcs = (pci_read8((uint8_t)bus, slot, 0, 0x0E) & 0x80) ? 8 : 1;
            for (uint8_t fn = 0; fn < funcs; fn++) {
                uint32_t c = pci_read32((uint8_t)bus, slot, fn, 8);
                if ((uint8_t)(c >> 24) == PCI_CLASS_SERIAL &&
                    (uint8_t)(c >> 16) == PCI_SUBCLASS_USB &&
                    (uint8_t)(c >> 8) == PCI_PROGIF_EHCI) {
                    *bus_out = (uint8_t)bus;
                    *slot_out = slot;
                    *fn_out = fn;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static int bios_handoff(uint32_t hccparams)
{
    uint8_t eecp = (uint8_t)((hccparams >> 8) & 0xFF);
    for (int hops = 0; eecp >= 0x40 && hops < 32; hops++) {
        uint32_t header = cap_read32(eecp);
        uint8_t id = (uint8_t)header;
        uint8_t next = (uint8_t)(header >> 8);
        if (id == 1) {
            volatile uint32_t *legacy = (volatile uint32_t *)(mmio + eecp);
            legacy[1] |= (1u << 24); /* OS owns the controller */
            for (uint32_t t = 0; t < EHCI_TIMEOUT_MS; t++) {
                if (!(legacy[1] & (1u << 16)))
                    return 0;
                delay_tick();
            }
            return -1;
        }
        if (next == 0)
            break;
        eecp = next;
    }
    return 0;
}

static int controller_start(void)
{
    uint8_t bus, slot, fn;
    if (pci_find_ehci(&bus, &slot, &fn) < 0)
        return -1;

    uint32_t bar = pci_read32(bus, slot, fn, PCI_BAR0);
    if (!bar || (bar & 1u) || ((bar >> 1) & 3u) == 2u)
        return -2; /* I/O or 64-bit BAR is not supported by this 32-bit driver. */
    uint32_t base = bar & 0xFFFFFFF0u;
    if (base < 0x100000u)
        return -3;

    uint16_t command = pci_read16(bus, slot, fn, PCI_COMMAND);
    pci_write16(bus, slot, fn, PCI_COMMAND, command | 0x0006u); /* memory + bus master */

    mmio = (volatile uint8_t *)base;
    uint8_t caplen = mmio[EHCI_CAPLENGTH];
    if (caplen < 0x10 || caplen > 0x40)
        return -4;
    opregs = mmio + caplen;
    uint32_t hcs = cap_read32(EHCI_HCSPARAMS);
    uint32_t hcc = cap_read32(EHCI_HCCPARAMS);
    port_count = (uint8_t)(hcs & 0x0Fu);
    if (!port_count || port_count > 15 || bios_handoff(hcc) < 0)
        return -5;

    uint32_t cmd = mmio_read32(EHCI_USBCMD);
    mmio_write32(EHCI_USBCMD, cmd & ~CMD_RUN);
    if (wait_mask(EHCI_USBSTS, STS_HALTED, STS_HALTED) < 0)
        return -6;
    mmio_write32(EHCI_USBCMD, mmio_read32(EHCI_USBCMD) | CMD_RESET);
    if (wait_mask(EHCI_USBCMD, CMD_RESET, 0) < 0)
        return -7;

    async_qh.horizontal = ((uint32_t)(uintptr_t)&transfer_qh & ~0x1Fu) | QH_LINK_TYPE;
    transfer_qh.horizontal = ((uint32_t)(uintptr_t)&async_qh & ~0x1Fu) | QH_LINK_TYPE;
    async_qh.endpoint = (1u << 15) | (2u << 12) | (64u << 16); /* head, high-speed, MPS */
    async_qh.next = QTD_TERMINATE;
    async_qh.alternate = QTD_TERMINATE;
    async_qh.token = 0;
    mmio_write32(EHCI_ASYNCLIST, (uint32_t)(uintptr_t)&async_qh);
    mmio_write32(EHCI_CONFIGFLAG, 1);
    mmio_write32(EHCI_USBCMD, CMD_RUN | CMD_ASYNC);
    if (wait_mask(EHCI_USBSTS, STS_HALTED, 0) < 0)
        return -8;
    return 0;
}

static void qtd_set_buffer(ehci_qtd_t *td, const void *ptr, uint32_t len)
{
    uintptr_t p = (uintptr_t)ptr;
    for (int i = 0; i < 5; i++)
        td->buffer[i] = 0;
    if (!ptr || !len)
        return;
    td->buffer[0] = (uint32_t)p;
    uintptr_t page = p & ~(uintptr_t)0xFFF;
    for (int i = 1; i < 5; i++) {
        page += 0x1000;
        td->buffer[i] = (uint32_t)page;
    }
}

static void qtd_prepare(ehci_qtd_t *td, uint32_t next, uint32_t pid,
                        uint32_t toggle, void *data, uint32_t len, int ioc)
{
    td->next = next;
    td->alternate = QTD_TERMINATE;
    td->token = (pid << 8) | QTD_CERR3 | ((len & 0x7FFFu) << 16) |
                (toggle ? (1u << 31) : 0) | QTD_ACTIVE | (ioc ? QTD_IOC : 0);
    qtd_set_buffer(td, data, len);
}

static int wait_transfer(ehci_qh_t *qh)
{
    for (uint32_t i = 0; i < EHCI_TIMEOUT_MS; i++) {
        uint32_t token = qh->token;
        if (!(token & QTD_ACTIVE))
            return (token & (QTD_HALTED | QTD_DATABUFERR | QTD_BABBLE | QTD_XACTERR)) ? -1 : 0;
        delay_tick();
    }
    return -1;
}

static int run_transfer(ehci_qh_t *qh, ehci_qtd_t *first)
{
    qh->current = 0;
    qh->next = (uint32_t)(uintptr_t)first;
    qh->alternate = QTD_TERMINATE;
    qh->token = 0;
    return wait_transfer(qh);
}

static int control_transfer(uint8_t addr, uint8_t max_packet, usb_setup_t *setup,
                            void *data, uint16_t length)
{
    for (int i = 0; i < 8; i++)
        setup_buffer[i] = ((uint8_t *)setup)[i];
    transfer_qh.endpoint = (addr & 0x7Fu) | (2u << 12) |
                          ((uint32_t)max_packet << 16);
    transfer_qh.endpoint_caps = 0;

    int data_in = (setup->request_type & 0x80u) != 0;
    qtd_prepare(&transfer_tds[0], (uint32_t)(uintptr_t)&transfer_tds[1],
                QTD_PID_SETUP, 0, setup_buffer, 8, 0);
    if (length) {
        qtd_prepare(&transfer_tds[1], (uint32_t)(uintptr_t)&transfer_tds[2],
                    data_in ? QTD_PID_IN : QTD_PID_OUT, 1, data, length, 0);
        qtd_prepare(&transfer_tds[2], QTD_TERMINATE,
                    data_in ? QTD_PID_OUT : QTD_PID_IN, 1, 0, 0, 1);
    } else {
        qtd_prepare(&transfer_tds[1], QTD_TERMINATE, QTD_PID_IN, 1, 0, 0, 1);
    }
    if (run_transfer(&transfer_qh, &transfer_tds[0]) < 0)
        return -1;
    return length;
}

static int bulk_transfer(uint8_t endpoint, uint16_t mps, int in,
                         void *data, uint16_t length, uint8_t *toggle)
{
    transfer_qh.endpoint = (device_address & 0x7Fu) |
        ((uint32_t)(endpoint & 0x0Fu) << 8) | (1u << 14) |
        (2u << 12) | ((uint32_t)mps << 16);
    transfer_qh.endpoint_caps = 0;
    qtd_prepare(&transfer_tds[0], QTD_TERMINATE, in ? QTD_PID_IN : QTD_PID_OUT,
                *toggle, data, length, 1);
    if (run_transfer(&transfer_qh, &transfer_tds[0]) < 0)
        return -1;
    *toggle ^= (uint8_t)((length + mps - 1u) / mps);
    return length;
}

static int ctrl(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                void *data, uint16_t length)
{
    usb_setup_t setup = { type, request, value, index, length };
    return control_transfer(device_address, 64, &setup, data, length);
}

static int enumerate_storage(void)
{
    uint8_t device_desc[18] __attribute__((aligned(16)));
    uint8_t config[512] __attribute__((aligned(16)));
    device_address = 0;
    usb_setup_t get8 = {0x80, 6, 0x0100, 0, 8};
    if (control_transfer(0, 8, &get8, device_desc, 8) < 0)
        return -1;
    uint8_t max0 = device_desc[7];
    if (max0 != 8 && max0 != 16 && max0 != 32 && max0 != 64)
        return -2;
    usb_setup_t setaddr = {0, 5, 1, 0, 0};
    if (control_transfer(0, max0, &setaddr, 0, 0) < 0)
        return -3;
    timer_sleep_ms(10);
    device_address = 1;

    usb_setup_t getdev = {0x80, 6, 0x0100, 0, 18};
    if (ctrl(0x80, 6, 0x0100, 0, device_desc, 18) < 0)
        return -4;
    usb_setup_t getcfg = {0x80, 6, 0x0200, 0, 9};
    if (ctrl(0x80, 6, 0x0200, 0, config, 9) < 0)
        return -5;
    uint16_t total = (uint16_t)(config[2] | (config[3] << 8));
    if (total < 9 || total > sizeof(config))
        return -6;
    if (ctrl(0x80, 6, 0x0200, 0, config, total) < 0)
        return -7;

    uint8_t iface = 0xFF;
    ep_in = ep_out = 0;
    bulk_mps = 0;
    int collecting_msc = 0;
    for (uint16_t off = 0; off + 2 <= total;) {
        uint8_t len = config[off], type = config[off + 1];
        if (len < 2 || off + len > total)
            return -8;
        if (type == 4 && len >= 9) {
            collecting_msc = config[off + 5] == 8 && config[off + 6] == 6 &&
                             config[off + 7] == 0x50;
            if (collecting_msc)
                iface = config[off + 2];
        } else if (type == 5 && len >= 7 && collecting_msc &&
                   (config[off + 3] & 3u) == 2u) {
            uint8_t address = config[off + 2];
            uint16_t mps = (uint16_t)(config[off + 4] | (config[off + 5] << 8)) & 0x7FFu;
            if (address & 0x80u) ep_in = address;
            else ep_out = address;
            if (mps > bulk_mps) bulk_mps = mps;
        }
        off += len;
    }
    if (iface == 0xFF || !ep_in || !ep_out || !bulk_mps)
        return -9;

    if (ctrl(0, 9, config[5], 0, 0, 0) < 0)
        return -10;
    if (ctrl(0x01, 11, 0, iface, 0, 0) < 0) /* SET_INTERFACE alt 0 */
        return -11;
    in_toggle = out_toggle = 0;
    return 0;
}

static void be32_store(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t be32_load(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int bot_cmd(uint8_t *cdb, uint8_t cdb_len, void *data, uint32_t len, int in)
{
    cbw.signature = 0x43425355u;
    cbw.tag = ++bot_tag;
    cbw.data_length = len;
    cbw.flags = in ? 0x80 : 0;
    cbw.lun = 0;
    cbw.command_length = cdb_len;
    for (int i = 0; i < 16; i++) cbw.command[i] = i < cdb_len ? cdb[i] : 0;
    if (bulk_transfer(ep_out, bulk_mps, 0, &cbw, sizeof(cbw), &out_toggle) != sizeof(cbw))
        return -1;
    if (len && bulk_transfer(in ? ep_in : ep_out, bulk_mps, in, data,
                             (uint16_t)len, in ? &in_toggle : &out_toggle) != (int)len)
        return -2;
    for (int i = 0; i < sizeof(csw); i++) ((uint8_t *)&csw)[i] = 0;
    if (bulk_transfer(ep_in, bulk_mps, 1, &csw, sizeof(csw), &in_toggle) != sizeof(csw))
        return -3;
    if (csw.signature != 0x53425355u || csw.tag != cbw.tag || csw.status != 0)
        return -4;
    return 0;
}

static int storage_initialize(void)
{
    uint8_t cdb[16] = {0};
    uint8_t inquiry[36];
    cdb[0] = 0x12; cdb[4] = sizeof(inquiry);
    if (bot_cmd(cdb, 6, inquiry, sizeof(inquiry), 1) < 0)
        return -1;
    cdb[0] = 0; /* TEST UNIT READY */
    for (int tries = 0; tries < 10; tries++) {
        if (bot_cmd(cdb, 6, 0, 0, 0) == 0)
            break;
        timer_sleep_ms(100);
        if (tries == 9)
            return -2;
    }
    cdb[0] = 0x25; /* READ CAPACITY(10) */
    if (bot_cmd(cdb, 10, io_buffer, 8, 1) < 0)
        return -3;
    uint32_t last_lba = be32_load(io_buffer);
    sector_size = be32_load(io_buffer + 4);
    if (last_lba == 0xFFFFFFFFu || sector_size != 512)
        return -4;
    sector_count = last_lba + 1;
    return 0;
}

int usb_msc_init(void)
{
    active = 0;
    if (controller_start() < 0)
        return -1;
    for (uint8_t p = 0; p < port_count; p++) {
        uint32_t off = EHCI_PORTSC + 4u * p;
        uint32_t v = mmio_read32(off);
        if (!(v & PORT_CONNECT))
            continue;
        if (v & PORT_POWER)
            mmio_write32(off, (v & ~(PORT_CONNECT | PORT_ENABLE | PORT_RESET)) | PORT_POWER);
        else
            mmio_write32(off, (v & ~(PORT_CONNECT | PORT_ENABLE | PORT_RESET)) | PORT_POWER);
        timer_sleep_ms(20);
        v = mmio_read32(off);
        mmio_write32(off, (v & ~PORT_RESET) | PORT_RESET | PORT_POWER);
        timer_sleep_ms(50);
        v = mmio_read32(off);
        mmio_write32(off, (v & ~PORT_RESET) | PORT_POWER);
        timer_sleep_ms(10);
        v = mmio_read32(off);
        if (!(v & PORT_ENABLE))
            continue; /* Full/low-speed devices belong to EHCI companion controllers. */
        if (enumerate_storage() == 0 && storage_initialize() == 0) {
            active = 1;
            return 0;
        }
    }
    return -2;
}

int usb_msc_present(void) { return active; }

static int rw(uint32_t lba, uint8_t count, void *buffer, int write)
{
    if (!active || !buffer || !count || lba >= sector_count || count > sector_count - lba)
        return -1;
    uint8_t *bytes = (uint8_t *)buffer;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t cdb[16] = {0};
        cdb[0] = write ? 0x2A : 0x28;
        be32_store(cdb + 2, lba + i);
        cdb[8] = 1;
        if (bot_cmd(cdb, 10, bytes + (uint32_t)i * 512u, 512, !write) < 0)
            return -1;
    }
    return 0;
}

int usb_msc_read(uint32_t lba, uint8_t count, void *buffer)
{
    return rw(lba, count, buffer, 0);
}

int usb_msc_write(uint32_t lba, uint8_t count, const void *buffer)
{
    return rw(lba, count, (void *)buffer, 1);
}
