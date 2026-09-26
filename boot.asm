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
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [bsDriveNumber], dl
    sti
    mov dl, [bsDriveNumber]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .use_chs
    cmp bx, 0xAA55
    jne .use_chs

    mov word [dap_count],  4
    mov word [dap_offset], 0x8000
    mov word [dap_segment], 0
    mov dword [dap_lba_lo], 1
    mov dword [dap_lba_hi], 0

    mov dl, [bsDriveNumber]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error

    jmp 0x0800:0000

.use_chs:
    mov ax, 1
    xor dx, dx
    div word [bpbSectorsPerTrack]
    mov cl, dl
    inc cl
    xor dx, dx
    div word [bpbHeads]
    mov ch, al
    mov dh, dl
    mov dl, [bsDriveNumber]

    mov bx, 0x8000
    mov ah, 0x02
    mov al, 4
    int 0x13
    jc disk_error

    jmp 0x0800:0000

disk_error:
    cli
    hlt
dap:
    db 0x10
    db 0
dap_count:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_lba_lo:
    dd 0
dap_lba_hi:
    dd 0

times 510-($-$$) db 0
dw 0xAA55
