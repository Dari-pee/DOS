#ifndef HID_H
#define HID_H

#include "usb.h"

void hid_reset(void);
int  hid_count(void);
int  hid_attach(usb_dev_t *d, uint8_t iface, uint8_t proto, uint8_t ep, int mps);
int  hid_poll(void);
int  hid_getchar(void);

#endif
