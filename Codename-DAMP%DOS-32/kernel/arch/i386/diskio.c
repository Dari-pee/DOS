#include "ff.h"
#include "diskio.h"
#include "usb_msc.h"

extern int ata_read_sectors(uint8_t slave, uint32_t lba, uint8_t count, void *buffer);
extern int ata_write_sectors(uint8_t slave, uint32_t lba, uint8_t count, const void *buffer);

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv < 2) return 0;
    if (pdrv == 2 && usb_msc_present()) return 0;
    return STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv < 2) return 0;
    if (pdrv == 2 && usb_msc_present()) return 0;
    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv == 2 && buff && count && count <= 255 &&
        (LBA_t)(uint32_t)sector == sector)
        return usb_msc_read((uint32_t)sector, (uint8_t)count, buff) == 0 ? RES_OK : RES_ERROR;
    if (pdrv >= 2 || !buff || !count || count > 255 || (LBA_t)(uint32_t)sector != sector)
        return RES_PARERR;
    return ata_read_sectors(pdrv, (uint32_t)sector, (uint8_t)count, buff) == 0
        ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv == 2 && buff && count && count <= 255 &&
        (LBA_t)(uint32_t)sector == sector)
        return usb_msc_write((uint32_t)sector, (uint8_t)count, buff) == 0 ? RES_OK : RES_ERROR;
    if (pdrv >= 2 || !buff || !count || count > 255 || (LBA_t)(uint32_t)sector != sector)
        return RES_PARERR;
    return ata_write_sectors(pdrv, (uint32_t)sector, (uint8_t)count, buff) == 0
        ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
    }
    return RES_PARERR;
}
