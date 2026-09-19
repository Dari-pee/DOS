[org 0x7C00]
bits 16

jmp short start
nop

bdOEMLabel           db "MSWIN4.1"
bpbBytesPerSector    dw 512
bpbSectorsPerCluster db 1
bpbReservedSectors   dw 5
bpbNumberOfFATs      db 2
bpbRootEntries       dw 224
bpbTotalSectors      dw 2880
bpbMedia             db 0xF0
bpbSectorsPerFAT     dw 9
bpbSectorsPerTrack   dw 18
bpbHeads             dw 2
bpbHiddenSectors     dd 0
bpbLargeSectors      dd 0

bsDriveNumber        db 0
bsReserved1          db 0
bsBootSignature       db 0x29
bsVolumeID           dd 0x12345678
bsVolumeLabel        db "DARI     OS"
bsFileSystemType     db "FAT12   "

start:
    ; INT 13h uses the current stack, so establish one before the first BIOS call.
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [bsDriveNumber], dl
    sti

    mov bx, 0x8000

    mov ah, 0x02
    mov al, 4
    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [bsDriveNumber]

    int 0x13
    jc disk_error

    jmp 0x0800:0000

disk_error:
    cli
    hlt

times 510-($-$$) db 0
dw 0xAA55
