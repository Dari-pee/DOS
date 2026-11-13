#ifndef PCI_H
#define PCI_H

#include <stdint.h>

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);
void     pci_scan(void);
int      pci_find_class(uint8_t cls, uint8_t sub, uint8_t pif, int index,
                        uint8_t *bus, uint8_t *dev, uint8_t *fn);

#endif
