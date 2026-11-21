#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include "usb.h"

typedef struct {
    char     vendor[9];
    char     product[17];
    uint32_t blocks;
    uint32_t block_size;
} storage_info_t;

void storage_reset(void);
int  storage_count(void);

int  storage_attach(usb_dev_t *d, uint8_t iface, uint8_t ep_in, uint8_t ep_out, int mps);

int  storage_info(int idx, storage_info_t *out);
int  storage_read(int idx, uint32_t lba, uint32_t count, void *buf);
int  storage_write(int idx, uint32_t lba, uint32_t count, const void *buf);

#endif
