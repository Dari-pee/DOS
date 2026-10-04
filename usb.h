#ifndef USB_H
#define USB_H

#include <stdint.h>

#define USB_SPEED_LOW  0
#define USB_SPEED_FULL 1
#define USB_SPEED_HIGH 2

typedef struct usb_setup {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

struct usb_hc;
struct usb_dev;

typedef struct usb_hc {
    const char *name;
    int         nports;
    int (*port_reset)(struct usb_hc *hc, int port);
    int (*control)(struct usb_hc *hc, struct usb_dev *dev,
                   const usb_setup_t *setup, void *buf, int len);
    void *(*int_open)(struct usb_hc *hc, struct usb_dev *dev, uint8_t ep, int mps);
    int   (*int_poll)(struct usb_hc *hc, void *handle, uint8_t *out, int maxlen);
    void  (*int_close_all)(struct usb_hc *hc);
    int   (*bulk)(struct usb_hc *hc, struct usb_dev *dev, uint8_t ep, int mps,
                  int in, void *buf, int len, uint8_t *toggle);
    void *priv;
} usb_hc_t;

typedef struct usb_dev {
    usb_hc_t *hc;
    uint8_t   addr;
    uint8_t   speed;
    uint8_t   port;
    uint8_t   mps0;
    uint16_t  vid, pid;
    uint8_t   dev_class, dev_subclass, dev_proto;
} usb_dev_t;

void usb_register_hc(usb_hc_t *hc);
int  usb_ctrl(usb_dev_t *d, uint8_t type, uint8_t req, uint16_t value,
              uint16_t index, void *buf, int len);

void usb_init(void);
void usb_scan(void);

#endif
