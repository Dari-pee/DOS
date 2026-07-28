[org 0x7c00]
[bits 16]

; FAT12 boot sector for a 1.44 MB floppy disk.
jmp short start
nop
db 'DOS32   '                 ; OEM name
bytes_per_sector: dw 512
sectors_per_cluster: db 1
reserved_sectors: dw 1
fat_count: db 2
root_entries: dw 224
total_sectors: dw 2880
media_descriptor: db 0xf0
sectors_per_fat: dw 9
sectors_per_track: dw 18
head_count: dw 2
hidden_sectors: dd 0
large_sector_count: dd 0
drive_number: db 0
reserved: db 0
extended_boot_signature: db 0x29
volume_id: dd 0x32334544
volume_label: db 'DOS32      '
filesystem_type: db 'FAT12   '

KERNEL_SEGMENT equ 0x1000
ROOT_DIRECTORY_SECTOR equ 19
ROOT_DIRECTORY_SECTORS equ 14
FAT_SECTOR equ 1              ; LBA 1: the first sector after the boot sector
FAT_BUFFER equ 0xb000
ROOT_BUFFER equ 0x9000        ; kept intact for the kernel's `dir` command

start:
    mov [boot_disk], dl

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    mov si, loading_message
    call print_string

    ; Read the entire root directory, then locate KERNEL.BIN.
    mov ax, ROOT_DIRECTORY_SECTOR
    mov cx, ROOT_DIRECTORY_SECTORS
    mov bx, ROOT_BUFFER
    call read_sectors

    mov si, ROOT_BUFFER
    mov cx, 224
.find_kernel:
    cmp byte [si], 0
    je disk_error
    cmp byte [si], 0xe5
    je .next_entry

    push si
    push cx
    mov di, kernel_name
    mov cx, 11
    repe cmpsb
    pop cx
    pop si
    je .kernel_found

.next_entry:
    add si, 32
    loop .find_kernel
    jmp disk_error

.kernel_found:
    mov ax, [si + 26]
    mov [current_cluster], ax

    ; The first FAT is enough to follow the KERNEL.BIN cluster chain.
    mov ax, FAT_SECTOR
    mov cx, 9
    mov bx, FAT_BUFFER
    call read_sectors

    ; The linker places the kernel at physical address 0x1000.
    ; Keep ES at zero so BX addresses that exact location.
    xor ax, ax
    mov es, ax
    mov bx, KERNEL_SEGMENT

.load_cluster:
    mov ax, [current_cluster]
    cmp ax, 0x0ff8
    jae .kernel_loaded

    ; On this disk, data sector = cluster number + 31.
    add ax, 31
    call read_sector

    add bx, 512
    jnc .next_cluster
    mov ax, es
    add ax, 0x1000
    mov es, ax

.next_cluster:
    ; FAT12 entries are 12 bits: offset = n + n / 2.
    mov ax, [current_cluster]
    mov dx, ax
    shr dx, 1
    add ax, dx
    mov si, FAT_BUFFER
    add si, ax
    mov ax, [si]
    test word [current_cluster], 1
    jz .even_cluster
    shr ax, 4
    jmp .store_cluster

.even_cluster:
    and ax, 0x0fff

.store_cluster:
    mov [current_cluster], ax
    jmp .load_cluster

.kernel_loaded:
    mov si, starting_message
    call print_string
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:protected_mode

; Read CX sequential sectors beginning at LBA AX into ES:BX.
read_sectors:
    push cx
.sector_loop:
    call read_sector
    add bx, 512
    inc ax
    loop .sector_loop
    pop cx
    ret

; Read one LBA sector in AX into ES:BX using the BIOS floppy geometry.
read_sector:
    push ax
    push bx
    push cx
    push dx

    xor dx, dx
    div word [sectors_per_track]
    mov cl, dl
    inc cl
    xor dx, dx
    div word [head_count]
    mov dh, dl
    mov ch, al
    mov dl, [boot_disk]
    mov ah, 0x02
    mov al, 1
    int 0x13
    jc disk_error

    pop dx
    pop cx
    pop bx
    pop ax
    ret

disk_error:
    mov si, error_message
.print_error:
    call print_string
    cli
    hlt
    jmp $

; Print a null-terminated message through the BIOS teletype service.
print_string:
.print_error:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp .print_error
.done:
    ret

boot_disk: db 0
current_cluster: dw 0
kernel_name: db 'KERNEL  BIN'
loading_message: db 'Loading KERNEL.BIN...', 13, 10, 0
starting_message: db 'Starting kernel...', 13, 10, 0
error_message: db 'KERNEL.BIN missing', 0

gdt_start:
    dq 0
gdt_code:
    dw 0xffff, 0
    db 0, 0x9a, 0xcf, 0
gdt_data:
    dw 0xffff, 0
    db 0, 0x92, 0xcf, 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

[bits 32]
protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ebp, 0x90000
    mov esp, ebp
    jmp KERNEL_SEGMENT

times 510 - ($ - $$) db 0
dw 0xaa55
