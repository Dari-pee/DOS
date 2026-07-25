org 0x0
bits 16

%define ENDL 0x0D, 0x0A

jmp start

start:
    cli

    mov ax, cs
    mov ds, ax
    mov es, ax

    mov [boot_drive], dl

    mov ss, ax
    mov sp, 0xFFFE

    sti

    ; Clear screen
    mov ah, 0x06
    mov al, 0
    mov bh, 0x1F
    mov ch, 0
    mov cl, 0
    mov dh, 24
    mov dl, 79
    int 0x10

    ; Cursor to top-left
    mov ah, 0x02
    mov bh, 0
    mov dh, 0
    mov dl, 0
    int 0x10

    mov si, msg_hello
    call puts

    jmp shell

get_string:
    xor cl, cl

.loop:
    mov ah, 0
    int 16h

    cmp al, 08h
    je .backspace

    cmp al, 0Dh
    je .done

    cmp cl, 63
    je .loop

    mov ah, 0Eh
    int 10h

    stosb
    inc cl
    jmp .loop

.backspace:
    cmp cl, 0
    je .loop

    dec di
    mov byte [di], 0
    dec cl

    mov ah, 0Eh

    mov al, 08h
    int 10h

    mov al, ' '
    int 10h

    mov al, 08h
    int 10h

    jmp .loop

.done:
    mov al, 0
    stosb

    mov ah, 0Eh
    mov al, 0Dh
    int 10h

    mov al, 0Ah
    int 10h

    ret

strcmp:
    push bx

.loop:
    mov al, [si]
    mov bl, [di]

    cmp al, bl
    jne .not_equal

    cmp al, 0
    je .equal

    inc si
    inc di
    jmp .loop

.not_equal:
    pop bx
    clc
    ret

.equal:
    pop bx
    stc
    ret

dispatch:

    mov si, buffer
    mov di, cmd_help
    call strcmp
    jc help

    mov si, buffer
    mov di, cmd_cls
    call strcmp
    jc cls

    mov si, buffer
    mov di, cmd_version
    call strcmp
    jc version

    mov si, buffer
    mov di, cmd_dir
    call strcmp
    jc dir

    mov si, msg_unknown
    call puts
    ret

shell:

.loop:
    mov si, prompt
    call puts

    mov di, buffer
    call get_string

    call dispatch

    jmp .loop

halt:
    cli

.loop:
    hlt
    jmp .loop

lba_to_chs:
    push ax
    push dx

    xor dx, dx
    div word [bdb_sectors_per_track]
    inc dx
    mov cx, dx

    xor dx, dx
    div word [bdb_heads]
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah

    pop ax
    mov dl, al
    pop ax
    ret


disk_read:
    push ax
    push bx
    push cx
    push dx
    push di

    push cx
    call lba_to_chs
    pop ax        ; AL = sector count

    mov ah, 02h
    mov di, 3

.retry:
    pusha
    stc
    int 13h
    jc .error

.success:
    popa
    clc
    jmp .exit

.error:
    popa
    call disk_reset

    dec di
    jnz .retry

    stc

.exit:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret


disk_reset:
    push ax
    push dx

    mov ah, 0
    mov dl, [boot_drive]
    int 13h

    pop dx
    pop ax
    ret

print_filename:
    push ax
    push bx
    push cx
    push si

    mov cx, 8

.name:
    lodsb
    cmp al, ' '
    je .skip_print_name

    mov ah, 0Eh
    int 10h

.skip_print_name:
    loop .name


    mov al, '.'
    mov ah, 0Eh
    int 10h


    mov cx, 3

.ext:
    lodsb
    cmp al, ' '
    je .skip_print_ext

    mov ah, 0Eh
    int 10h

.skip_print_ext:
    loop .ext


    mov al, 0Dh
    mov ah, 0Eh
    int 10h

    mov al, 0Ah
    int 10h


    pop si
    pop cx
    pop bx
    pop ax
    ret

puts:
    push si
    push ax
    push bx

.loop:
    lodsb
    or al, al
    jz .done

    mov ah, 0Eh
    mov bh, 0
    int 10h

    jmp .loop

.done:
    pop bx
    pop ax
    pop si
    ret

help:
    mov si, msg_help
    call puts
    ret

cls:
    mov ah, 06h
    mov al, 0
    mov bh, 1Fh
    mov ch, 0
    mov cl, 0
    mov dh, 24
    mov dl, 79
    int 10h

    mov ah, 02h
    mov bh, 0
    mov dh, 0
    mov dl, 0
    int 10h

    ret

version:
    mov si, msg_version
    call puts
    ret
    

dir:
    mov ax, ROOT_DIR_LBA
    mov bx, root_directory
    mov cx, ROOT_DIR_SECTORS
    mov dl, [boot_drive]
    call disk_read

    jc .error

    mov bx, root_directory
    mov cx, 224

.next_entry:

    cmp byte [bx], 0
    je .done

    cmp byte [bx], 0xE5
    je .skip

    test byte [bx+11], 08h
    jnz .skip

    test byte [bx+11], 10h
    jnz .skip

    mov si, bx
    call print_filename

.skip:
    add bx, 32
    loop .next_entry

.done:
    ret


.error:
    mov si, msg_dir_fail
    call puts
    ret

msg_hello    db "Kernel loaded successfully. Welcome!", ENDL, 0

msg_help db "Commands:", ENDL
         db " help", ENDL
         db " cls", ENDL
         db " version", ENDL
         db " dir", ENDL,0

msg_version  db "DariOS, DOS for short. Version 1", ENDL, 0
msg_dir_ok   db "Root directory loaded.", ENDL,0
msg_dir_fail db "Disk read failed.", ENDL,0

msg_unknown  db "Unknown command", ENDL, 0

prompt       db "> ",0

cmd_help     db "help",0
cmd_cls      db "cls",0
cmd_version  db "version",0
cmd_dir db "dir",0

buffer times 64 db 0

boot_drive             db 0

bdb_bytes_per_sector   dw 512
bdb_reserved_sectors   dw 1
bdb_fat_count          db 2
bdb_dir_entries_count  dw 224
bdb_sectors_per_fat    dw 9
bdb_sectors_per_track  dw 18
bdb_heads              dw 2

ROOT_DIR_SECTORS equ 14
ROOT_DIR_LBA     equ 19

root_directory         times 7168 db 0