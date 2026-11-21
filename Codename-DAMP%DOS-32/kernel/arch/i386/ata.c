#include <stdint.h>

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t data);
extern void insw(uint16_t port, void *addr, uint32_t count);
extern void outsw(uint16_t port, const void *addr, uint32_t count);

#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_COMM_STAT    0x1F7

#define ATA_CMD_READ_PIO         0x20
#define ATA_CMD_WRITE_PIO        0x30
#define ATA_CMD_CACHE_FLUSH      0xE7

#define ATA_SR_BSY               0x80
#define ATA_SR_DRQ               0x08
#define ATA_SR_ERR               0x01
#define ATA_SR_DF                0x20
#define ATA_TIMEOUT              1000000u

static int ata_wait(uint8_t required) {
    for (uint32_t timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        uint8_t status = inb(ATA_PRIMARY_COMM_STAT);
        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return -1;
        if (!(status & ATA_SR_BSY) && (status & required) == required)
            return 0;
    }
    return -1;
}

static void ata_delay(void) {
    for (int i = 0; i < 4; i++)
        (void)inb(0x3F6);
}

int ata_read_sectors(uint8_t slave, uint32_t lba, uint8_t count, void *buffer) {
    if (slave > 1 || !count || !buffer || lba >= 0x10000000u ||
        (uint32_t)count > 0x10000000u - lba)
        return -1;
    if (ata_wait(0) != 0)
        return -1;
    outb(ATA_PRIMARY_DRIVE_HEAD, (slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ata_delay();
    if (ata_wait(0) != 0)
        return -1;
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMM_STAT, ATA_CMD_READ_PIO);

    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ata_wait(ATA_SR_DRQ) != 0)
            return -1;
        insw(ATA_PRIMARY_DATA, ptr, 256);
        ptr += 256;
    }
    return 0;
}

int ata_write_sectors(uint8_t slave, uint32_t lba, uint8_t count, const void *buffer) {
    if (slave > 1 || !count || !buffer || lba >= 0x10000000u ||
        (uint32_t)count > 0x10000000u - lba)
        return -1;
    if (ata_wait(0) != 0)
        return -1;
    outb(ATA_PRIMARY_DRIVE_HEAD, (slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ata_delay();
    if (ata_wait(0) != 0)
        return -1;
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMM_STAT, ATA_CMD_WRITE_PIO);

    const uint16_t *ptr = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ata_wait(ATA_SR_DRQ) != 0)
            return -1;
        outsw(ATA_PRIMARY_DATA, ptr, 256);
        ptr += 256;
    }
    outb(ATA_PRIMARY_COMM_STAT, ATA_CMD_CACHE_FLUSH);
    return ata_wait(0);
}
