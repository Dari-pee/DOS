#include <stdint.h>
#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA    0x0CFC

static uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    return (1u << 31) | ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) | ((uint32_t)function << 8) |
           (offset & 0xFCu);
}

static void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read32(bus, slot, function, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read32(bus, slot, function, offset);
    return (uint8_t)(value >> ((offset & 3u) * 8u));
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t function,
                 uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t function,
                 uint8_t offset, uint16_t value)
{
    uint32_t address = pci_addr(bus, slot, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t old = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t mask = 0xFFFFu << shift;
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, (old & ~mask) | ((uint32_t)value << shift));
}

static int pci_present(uint8_t bus, uint8_t slot, uint8_t function)
{
    return (uint16_t)pci_read32(bus, slot, function, 0) != 0xFFFFu;
}

static void pci_visit(uint8_t bus, uint8_t slot, uint8_t function,
                      pci_visit_fn visit, void *context)
{
    if (!pci_present(bus, slot, function))
        return;

    uint32_t identity = pci_read32(bus, slot, function, 0);
    uint32_t class_info = pci_read32(bus, slot, function, 8);
    struct pci_device device;
    device.bus = bus;
    device.slot = slot;
    device.function = function;
    device.revision = (uint8_t)class_info;
    device.prog_if = (uint8_t)(class_info >> 8);
    device.subclass = (uint8_t)(class_info >> 16);
    device.class_code = (uint8_t)(class_info >> 24);
    device.vendor_id = (uint16_t)identity;
    device.device_id = (uint16_t)(identity >> 16);
    visit(&device, context);
}

void pci_scan(pci_visit_fn visit, void *context)
{
    if (!visit)
        return;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            if (!pci_present((uint8_t)bus, slot, 0))
                continue;
            pci_visit((uint8_t)bus, slot, 0, visit, context);

            uint8_t header_type = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            if (header_type & 0x80) {
                for (uint8_t function = 1; function < 8; function++)
                    pci_visit((uint8_t)bus, slot, function, visit, context);
            }
        }
    }
}
