#ifndef DISK_H
#define DISK_H

/* BIOS-backed LBA read for the FAT12 floppy.  The destination must be below
   physical address 0x10000 because the real-mode thunk uses ES=0. */
int bios_read_sector(unsigned short lba, void *buffer);

#endif
