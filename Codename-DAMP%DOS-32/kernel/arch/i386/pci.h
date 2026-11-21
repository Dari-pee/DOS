#ifndef ARCH_I386_PCI_H
#define ARCH_I386_PCI_H

#include <stdint.h>

#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI  0x20
#define PCI_PROGIF_XHCI  0x30

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t revision;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint16_t vendor_id;
    uint16_t device_id;
};

typedef void (*pci_visit_fn)(const struct pci_device *device, void *context);

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t function,
                 uint8_t offset, uint32_t value);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t function,
                 uint8_t offset, uint16_t value);
void pci_scan(pci_visit_fn visit, void *context);

#endif
