%include "config.inc"
[bits 16]
[org 0x7C00]

start:
    jmp 0x0000:.go              ; normalise CS:IP (some BIOSes use 07C0:0000)
.go:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl        ; BIOS passes the boot drive in DL
    call disk_init

    mov eax, STAGE2_LBA
    mov bx, STAGE2_ADDR
    mov cx, STAGE2_SECTORS
.load:
    call read_sector
    inc eax
    add bx, 512
    loop .load

    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR

disk_error:
    mov si, msg_err
.p: lodsb
    test al, al
    jz .h
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .p
.h: cli
    hlt
    jmp .h

msg_err: db "Disk error", 0

%include "diskread.inc"

times 510-($-$$) db 0
dw 0xAA55
