[org 0x8000]
bits 16

FAT_START  equ 5
ROOT_START equ 23
DATA_START equ 37
KERNEL_LOCATION equ 0x1000
DESKTOP_LOCATION equ 0x20000
FAT_BUFFER       equ 0xA000
ROOT_BUFFER      equ 0xB200

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000

    mov [BOOT_DISK], dl
    mov [0x0500], dl        ; runtime BIOS thunk reads the original boot drive

    mov bx, FAT_BUFFER
    mov ax, FAT_START
    mov cx, 9

.read_fat:
    push ax
    push cx

    call read_sector

    pop cx
    pop ax

    inc ax
    add bx, 512

    loop .read_fat

    mov bx, ROOT_BUFFER
    mov ax, ROOT_START
    mov cx, 14

.read_root:
    push ax
    push cx

    call read_sector

    pop cx
    pop ax

    inc ax
    add bx, 512

    loop .read_root

    mov si, kernel_name
    mov di, ROOT_BUFFER

    call find_file

    jc kernel_not_found

    mov [KERNEL_CLUSTER], ax

    mov ax, [KERNEL_CLUSTER]
    mov bx, KERNEL_LOCATION

    call load_file

    cli

    lgdt [GDT_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:start_protected_mode

read_sector:
    push ax
    push bx
    push cx
    push dx
    push si

    xor dx, dx
    mov si, 36
    div si

    mov ch, al

    mov ax, dx
    xor dx, dx
    mov si, 18
    div si

    mov dh, al
    mov cl, dl
    inc cl

    mov dl, [BOOT_DISK]
    mov ah, 02h
    mov al, 01h

    int 13h
    jc disk_error

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

find_file:
    push bx
    push cx
    push dx
    push si
    push di

    mov cx, 224

.next_entry:
    cmp byte [di], 0x00
    je .not_found

    cmp byte [di], 0xE5
    je .skip

    mov al, [di + 11]

    test al, 08h
    jnz .skip

    cmp al, 0x0F
    je .skip

    push cx
    push si
    push di

    mov bx, si
    mov cx, 11

.compare:
    mov al, [bx]
    cmp al, [di]
    jne .different

    inc bx
    inc di
    loop .compare

    pop di
    pop si
    pop cx

    mov ax, [di + 26]

    pop di
    pop si
    pop dx
    pop cx
    pop bx

    clc
    ret

.different:
    pop di
    pop si
    pop cx

.skip:
    add di, 32
    loop .next_entry

.not_found:
    pop di
    pop si
    pop dx
    pop cx
    pop bx

    stc
    ret

load_file:
.next_cluster:
    cmp ax, 0xFF8
    jae .done

    push ax

    sub ax, 2
    add ax, DATA_START

    call read_sector

    add bx, 512

    pop ax

    push bx
    push dx

    mov bx, ax

    mov dx, ax
    add ax, ax
    add ax, dx
    shr ax, 1

    mov si, FAT_BUFFER
    add si, ax

    mov dx, [si]

    test bx, 1
    jz .even_cluster

    shr dx, 4
    jmp .got_next

.even_cluster:
    and dx, 0x0FFF

.got_next:
    mov ax, dx

    pop dx
    pop bx

    jmp .next_cluster

.done:
    ret

kernel_not_found:
    mov si, kernel_error
    call print_error
    jmp halt

disk_error:
    mov si, disk_error_message
    call print_error

halt:
    cli
    hlt
    jmp halt

print_error:
.next_char:
    lodsb

    cmp al, 0
    je .done

    mov ah, 0x0E
    mov bh, 0
    int 0x10

    jmp .next_char

.done:
    ret

BOOT_DISK       db 0

KERNEL_CLUSTER  dw 0

kernel_name:
    db "KERNEL  BIN"

kernel_error:
    db "KERNEL.BIN NOT FOUND", 0

disk_error_message:
    db "DISK READ ERROR", 0

CODE_SEG equ GDT_code - GDT_start
DATA_SEG equ GDT_data - GDT_start
CODE_SEG16 equ GDT_code16 - GDT_start
DATA_SEG16 equ GDT_data16 - GDT_start

GDT_start:

GDT_null:
    dd 0
    dd 0

GDT_code:
    dw 0xffff
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0

GDT_data:
    dw 0xffff
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0

GDT_code16:
    dw 0xffff
    dw 0
    db 0
    db 10011010b
    db 00001111b
    db 0

GDT_data16:
    dw 0xffff
    dw 0
    db 0
    db 10010010b
    db 00001111b
    db 0

GDT_end:

GDT_descriptor:
    dw GDT_end - GDT_start - 1
    dd GDT_start

[bits 32]

start_protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    jmp KERNEL_LOCATION
