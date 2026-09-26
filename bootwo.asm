[org 0x8000]
bits 16

; whilst implementing the desktop, i had an issue where no matter what i did the kernel.bin wouldnt load
; after i took some steps back (removed desktop.asm, fat12) the desktop.asm was the issue since
; ts fuckass file was writing to the wrong memory location
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; i will forever HATE desktops. CLI is king
; desktop = ass
; gui = ass
; command line interface = king
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; fuck desktops, they are a pain in the ass to implement and i will never implement one again
; ggs bru


BPB_RESERVED_SECTORS equ 0x7C00 + 0x0E
BPB_NUM_FATS          equ 0x7C00 + 0x10
BPB_ROOT_ENTRIES      equ 0x7C00 + 0x11
BPB_SECTORS_PER_FAT   equ 0x7C00 + 0x16
BPB_SECTORS_PER_TRACK equ 0x7C00 + 0x18
BPB_HEADS             equ 0x7C00 + 0x1A
KERNEL_LOCATION equ 0x1000
DESKTOP_LOCATION equ 0x20000
FAT_BUFFER       equ 0xA000

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000

    mov [BOOT_DISK], dl
    mov [0x0500], dl

    call parse_bpb
    call check_extensions

    mov bx, FAT_BUFFER
    mov ax, [FAT_START]
    mov cx, [SECTORS_PER_FAT]

.read_fat:
    push ax
    push cx

    call read_sector

    pop cx
    pop ax

    inc ax
    add bx, 512

    loop .read_fat

    mov bx, [ROOT_BUFFER_ADDR]
    mov ax, [ROOT_START]
    mov cx, [ROOT_DIR_SECTORS]

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
    mov di, [ROOT_BUFFER_ADDR]

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

parse_bpb:
    mov si, BPB_RESERVED_SECTORS
    mov ax, [si]
    mov [FAT_START], ax

    mov si, BPB_NUM_FATS
    xor ah, ah
    mov al, [si]
    mov [NUM_FATS], ax

    mov si, BPB_SECTORS_PER_FAT
    mov ax, [si]
    mov [SECTORS_PER_FAT], ax

    mov ax, [NUM_FATS]
    mul word [SECTORS_PER_FAT]
    add ax, [FAT_START]
    mov [ROOT_START], ax

    mov si, BPB_ROOT_ENTRIES
    mov ax, [si]
    mov [ROOT_ENTRIES], ax

    mov ax, [ROOT_ENTRIES]
    mov dx, ax
    and dx, 0x000F
    shr ax, 4
    cmp dx, 0
    je .no_round
    inc ax
.no_round:
    mov [ROOT_DIR_SECTORS], ax

    mov ax, [ROOT_START]
    add ax, [ROOT_DIR_SECTORS]
    mov [DATA_START], ax

    mov ax, [SECTORS_PER_FAT]
    mov cl, 9
    shl ax, cl
    add ax, FAT_BUFFER
    mov [ROOT_BUFFER_ADDR], ax

    mov si, BPB_SECTORS_PER_TRACK
    mov ax, [si]
    mov [SECTORS_PER_TRACK], ax

    mov si, BPB_HEADS
    mov ax, [si]
    mov [HEADS], ax

    ret

check_extensions:
    mov byte [EXT_SUPPORTED], 0

    mov dl, [BOOT_DISK]
    mov ah, 0x41
    mov bx, 0x55AA
    int 13h
    jc .done
    cmp bx, 0xAA55
    jne .done

    mov byte [EXT_SUPPORTED], 1

.done:
    ret

read_sector:
    push ax
    push bx
    push cx
    push dx
    push si

    cmp byte [EXT_SUPPORTED], 0
    je .use_chs

    mov [dap_lba_lo], ax
    mov word [dap_lba_lo+2], 0
    mov [dap_offset], bx
    mov word [dap_count], 1
    mov word [dap_segment], 0

    mov dl, [BOOT_DISK]
    mov si, dap
    mov ah, 42h

    int 13h
    jc disk_error
    jmp .done

.use_chs:
    xor dx, dx
    div word [SECTORS_PER_TRACK]
    mov cl, dl
    inc cl
    xor dx, dx
    div word [HEADS]
    mov ch, al
    mov dh, dl
    mov dl, [BOOT_DISK]
    mov ax, 0x0201

    int 13h
    jc disk_error

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    dd 0

find_file:
    push bx
    push cx
    push dx
    push si
    push di

    mov cx, [ROOT_ENTRIES]

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
    add ax, [DATA_START]

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
FAT_START         dw 0
ROOT_START        dw 0
DATA_START        dw 0
NUM_FATS          dw 0
SECTORS_PER_FAT   dw 0
ROOT_ENTRIES      dw 0
ROOT_DIR_SECTORS  dw 0
ROOT_BUFFER_ADDR  dw 0
SECTORS_PER_TRACK dw 0
HEADS             dw 0
EXT_SUPPORTED     db 0

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
