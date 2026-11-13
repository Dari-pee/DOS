#include <stdint.h>
#include "io.h"
#include "pci.h"
#include "kernel_stdio.h"

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    outl(0xCF8, 0x80000000u
                | ((uint32_t)bus << 16)
                | ((uint32_t)dev << 11)
                | ((uint32_t)fn  << 8)
                | (off & 0xFC));
    return inl(0xCFC);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v)
{
    outl(0xCF8, 0x80000000u
                | ((uint32_t)bus << 16)
                | ((uint32_t)dev << 11)
                | ((uint32_t)fn  << 8)
                | (off & 0xFC));
    outl(0xCFC, v);
}

void pci_scan(void)
{
    int found = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);

                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0)
                        break;
                    continue;
                }

                uint32_t cls        = pci_read32(bus, dev, fn, 0x08);
                uint8_t  class_code = cls >> 24;
                uint8_t  subclass   = cls >> 16;
                uint8_t  prog_if    = cls >> 8;

                printf("%x:%x.%x  %x:%x  class %x.%x.%x", bus, dev, fn,
                       id & 0xFFFF, id >> 16, class_code, subclass, prog_if);

                if (class_code == 0x0C && subclass == 0x03) {
                    const char *kind = "USB (unknown)";
                    if (prog_if == 0x00) kind = "UHCI";
                    else if (prog_if == 0x10) kind = "OHCI";
                    else if (prog_if == 0x20) kind = "EHCI";
                    else if (prog_if == 0x30) kind = "xHCI";
                    printf("  <-- %s", kind);
                    found++;
                }
                printf("\n");

                if (fn == 0 && !((pci_read32(bus, dev, 0, 0x0C) >> 16) & 0x80))
                    break;
            }
        }
    }

    printf("USB controllers found: %d\n", found);
}

int pci_find_class(uint8_t cls, uint8_t sub, uint8_t pif, int index,
                   uint8_t *bus, uint8_t *dev, uint8_t *fn)
{
    int seen = 0;

    for (int b = 0; b < 256; b++) {
        for (int d = 0; d < 32; d++) {
            for (int f = 0; f < 8; f++) {
                uint32_t id = pci_read32(b, d, f, 0x00);

                if ((id & 0xFFFF) == 0xFFFF) {
                    if (f == 0)
                        break;
                    continue;
                }

                uint32_t c = pci_read32(b, d, f, 0x08);
                if ((uint8_t)(c >> 24) == cls && (uint8_t)(c >> 16) == sub &&
                    (uint8_t)(c >> 8) == pif) {
                    if (seen == index) {
                        *bus = (uint8_t)b; *dev = (uint8_t)d; *fn = (uint8_t)f;
                        return 0;
                    }
                    seen++;
                }

                if (f == 0 && !((pci_read32(b, d, 0, 0x0C) >> 16) & 0x80))
                    break;
            }
        }
    }
    return -1;
}
