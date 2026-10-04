#include <stdint.h>
#include "usbcmd.h"
#include "storage.h"
#include "kernel_stdio.h"

static uint8_t sect[4096];

static int starts(const char *t, const char *prefix)
{
    while (*prefix)
        if (*t++ != *prefix++)
            return 0;
    return 1;
}

static const char *skip_spaces(const char *p)
{
    while (*p == ' ')
        p++;
    return p;
}

static int parse_u32(const char **p, uint32_t *out)
{
    const char *s = skip_spaces(*p);
    uint32_t v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
        digits++;
    }
    *p = s;
    *out = v;
    return digits > 0;
}

static void print_hex_byte(uint8_t b)
{
    static const char hex[] = "0123456789abcdef";
    printf("%c%c", hex[b >> 4], hex[b & 15]);
}

static void hexdump(const uint8_t *d, int len)
{
    for (int row = 0; row < len; row += 16) {
        printf("%x: ", row);
        if (row < 16) printf("  ");
        else if (row < 256) printf(" ");
        for (int i = 0; i < 16; i++) {
            print_hex_byte(d[row + i]);
            printf(" ");
        }
        printf(" ");
        for (int i = 0; i < 16; i++) {
            uint8_t c = d[row + i];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}

static int need_disk(void)
{
    if (storage_count() == 0) {
        printf("No USB disk. (Plug one in and run 'usb' first.)\n");
        return 0;
    }
    return 1;
}

int usbcmd_try(const char *cmd)
{
    storage_info_t info;
    uint32_t a, b;
    const char *p;

    if (!starts(cmd, "usbdisk") && !starts(cmd, "usbread") &&
        !starts(cmd, "usbwrite") && !starts(cmd, "usbsum"))
        return 0;

    if (starts(cmd, "usbdisk")) {
        if (storage_count() == 0) {
            printf("No USB disks.\n");
            return 1;
        }
        for (int i = 0; i < storage_count(); i++) {
            storage_info(i, &info);
            printf("disk %d: %s %s, %u blocks of %u bytes", i, info.vendor, info.product,
                   info.blocks, info.block_size);
            if (info.block_size == 512)
                printf(" (%u KB)", info.blocks / 2);
            printf("\n");
        }
        return 1;
    }

    if (starts(cmd, "usbread")) {
        p = cmd + 7;
        if (!parse_u32(&p, &a)) {
            printf("Usage: usbread <sector>\n");
            return 1;
        }
        if (!need_disk()) return 1;
        storage_info(0, &info);
        if (storage_read(0, a, 1, sect) < 0) {
            printf("Read failed (sector %u).\n", a);
            return 1;
        }
        printf("Sector %u (first 128 of %u bytes):\n", a, info.block_size);
        hexdump(sect, 128);
        return 1;
    }

    if (starts(cmd, "usbwrite")) {
        p = cmd + 8;
        if (!parse_u32(&p, &a) || *skip_spaces(p) == 0) {
            printf("Usage: usbwrite <sector> <text>\n");
            return 1;
        }
        p = skip_spaces(p);
        if (!need_disk()) return 1;
        storage_info(0, &info);
        for (uint32_t i = 0; i < info.block_size; i++)
            sect[i] = 0;
        uint32_t n = 0;
        while (p[n] && n < info.block_size - 1) {
            sect[n] = (uint8_t)p[n];
            n++;
        }
        if (storage_write(0, a, 1, sect) < 0) {
            printf("Write failed (sector %u).\n", a);
            return 1;
        }
        printf("Wrote %u bytes to sector %u.\n", n, a);
        return 1;
    }

    p = cmd + 6;
    if (!parse_u32(&p, &a) || !parse_u32(&p, &b) || b == 0) {
        printf("Usage: usbsum <sector> <count>\n");
        return 1;
    }
    if (!need_disk()) return 1;
    storage_info(0, &info);
    uint32_t per = sizeof(sect) / info.block_size;
    uint32_t h = 2166136261u, done = 0;
    while (done < b) {
        uint32_t n = (b - done > per) ? per : (b - done);
        if (storage_read(0, a + done, n, sect) < 0) {
            printf("Read failed at sector %u.\n", a + done);
            return 1;
        }
        for (uint32_t i = 0; i < n * info.block_size; i++) {
            h ^= sect[i];
            h *= 16777619u;
        }
        done += n;
    }
    printf("%u blocks from sector %u: hash %x\n", b, a, h);
    return 1;
}
