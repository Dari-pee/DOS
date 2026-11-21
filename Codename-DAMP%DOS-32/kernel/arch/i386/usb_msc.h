#ifndef ARCH_I386_USB_MSC_H
#define ARCH_I386_USB_MSC_H

#include <stdint.h>

int usb_msc_init(void);
int usb_msc_present(void);
int usb_msc_read(uint32_t lba, uint8_t count, void *buffer);
int usb_msc_write(uint32_t lba, uint8_t count, const void *buffer);

#endif
