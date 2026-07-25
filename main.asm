org 0x0
bits 16

%define ENDL 0x0D, 0x0A
%define ROOT_DIR_LBA 19
%define ROOT_DIR_SECTORS 14
%define DATA_LBA 33
%define PROGRAM_LOAD_SEGMENT 0x3000
%define PROGRAM_LOAD_OFFSET 0x0000

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

    call clear_screen
    mov si, msg_hello
    call puts

shell:
.loop:
    mov si, prompt
    call puts
    mov di, buffer
    call get_string
    call dispatch
    jmp .loop

; Built-ins are checked first. Anything else is treated as a FAT12 .BIN program.
dispatch:
    cmp byte [buffer], 0
    je .done
    mov si, buffer
    mov di, cmd_help
    call strcmp
    jc help
    mov si, buffer
    mov di, cmd_cls
    call strcmp
    jc clear_screen
    mov si, buffer
    mov di, cmd_version
    call strcmp
    jc version
    mov si, buffer
    mov di, cmd_dir
    call strcmp
    jc dir

    call run_program
.done:
    ret

; Convert a typed command into an uppercase FAT 8.3 name.
; HELLO and hello.bin both become "HELLO   BIN". Spaces start arguments.
; Carry set means the name was invalid (empty, too long, or more than one dot).
make_fat_name:
    mov di, fat_name
    mov cx, 11
    mov al, ' '
    rep stosb

    mov si, buffer
    xor bx, bx                 ; characters in basename
    xor dx, dx                 ; DL = extension length, DH = saw dot
.name:
    lodsb
    or al, al
    jz .finish_name
    cmp al, ' '
    je .finish_name
    cmp al, '.'
    je .dot
    call uppercase
    cmp bx, 8
    jae .bad
    mov [fat_name + bx], al
    inc bx
    jmp .name
.dot:
    cmp dh, 0
    jne .bad
    cmp bx, 0
    je .bad
    mov dh, 1
.extension:
    lodsb
    or al, al
    jz .finish_extension
    cmp al, ' '
    je .finish_extension
    cmp al, '.'
    je .bad
    call uppercase
    cmp dl, 3
    jae .bad
    push ax
    xor ax, ax
    mov al, dl
    mov di, fat_name + 8
    add di, ax
    pop ax
    mov [di], al
    inc dl
    jmp .extension
.finish_name:
    cmp bx, 0
    je .bad
    cmp dh, 0
    jne .ok
    mov byte [fat_name + 8], 'B'
    mov byte [fat_name + 9], 'I'
    mov byte [fat_name + 10], 'N'
.ok:
    clc
    ret
.finish_extension:
    cmp dl, 0
    je .bad
    clc
    ret
.bad:
    stc
    ret

uppercase:
    cmp al, 'a'
    jb .done
    cmp al, 'z'
    ja .done
    sub al, 'a' - 'A'
.done:
    ret

; Locate, load, and call a raw 16-bit .BIN file. The program must return with RETF.
run_program:
    call make_fat_name
    jc .bad_name
    call load_root_directory
    jc .disk_error
    call find_program
    jc .not_found
    mov [current_cluster], ax
    call load_fat
    jc .disk_error
    call load_program
    jc .disk_error

    push ds
    mov ax, PROGRAM_LOAD_SEGMENT
    mov ds, ax
    mov es, ax
    call far PROGRAM_LOAD_SEGMENT:PROGRAM_LOAD_OFFSET
    pop ds
    mov ax, ds
    mov es, ax
    ret
.bad_name:
    mov si, msg_bad_name
    jmp puts
.not_found:
    mov si, msg_not_found
    jmp puts
.disk_error:
    mov si, msg_dir_fail
    jmp puts

load_root_directory:
    push bx
    push cx
    push dx
    mov ax, ds
    mov es, ax
    mov ax, ROOT_DIR_LBA
    mov bx, root_directory
    mov cx, ROOT_DIR_SECTORS
    mov dl, [boot_drive]
    call disk_read
    pop dx
    pop cx
    pop bx
    ret

load_fat:
    push bx
    push cx
    push dx
    mov ax, ds
    mov es, ax
    mov ax, [bdb_reserved_sectors]
    mov bx, fat_buffer
    xor cx, cx
    mov cl, [bdb_sectors_per_fat]
    mov dl, [boot_drive]
    call disk_read
    pop dx
    pop cx
    pop bx
    ret

; Returns AX = first cluster and carry clear when fat_name exists in root dir.
find_program:
    mov di, root_directory
    mov cx, [bdb_dir_entries_count]
.next:
    cmp byte [di], 0
    je .not_found
    cmp byte [di], 0xE5
    je .skip
    mov al, [di + 11]
    test al, 0x18              ; volume label or directory
    jnz .skip
    cmp al, 0x0F               ; long filename entry
    je .skip
    push cx
    push di
    mov si, fat_name
    mov cx, 11
    repe cmpsb
    pop di
    pop cx
    je .found
.skip:
    add di, 32
    loop .next
.not_found:
    stc
    ret
.found:
    mov ax, [di + 26]
    clc
    ret

; Read the FAT12 cluster chain to PROGRAM_LOAD_SEGMENT:0000.
load_program:
    mov ax, PROGRAM_LOAD_SEGMENT
    mov es, ax
    xor bx, bx
.cluster:
    mov ax, [current_cluster]
    sub ax, 2
    add ax, DATA_LBA
    mov cx, 1
    mov dl, [boot_drive]
    call disk_read
    jc .error
    add bx, [bdb_bytes_per_sector]

    mov ax, [current_cluster]
    mov cx, 3
    mul cx
    mov cx, 2
    div cx
    mov si, fat_buffer
    add si, ax
    mov ax, [si]
    or dx, dx
    jz .even_cluster
    shr ax, 4
    jmp .next_cluster
.even_cluster:
    and ax, 0x0FFF
.next_cluster:
    cmp ax, 0x0FF8
    jae .done
    cmp ax, 2
    jb .error
    mov [current_cluster], ax
    jmp .cluster
.done:
    clc
    ret
.error:
    stc
    ret

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
    or al, al
    jz .equal
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

clear_screen:
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

help:
    mov si, msg_help
    jmp puts
version:
    mov si, msg_version
    jmp puts

dir:
    call load_root_directory
    jc .error
    mov bx, root_directory
    mov cx, [bdb_dir_entries_count]
.next:
    cmp byte [bx], 0
    je .done
    cmp byte [bx], 0xE5
    je .skip
    mov al, [bx + 11]
    test al, 0x18
    jnz .skip
    cmp al, 0x0F
    je .skip
    mov si, bx
    call print_filename
.skip:
    add bx, 32
    loop .next
.done:
    ret
.error:
    mov si, msg_dir_fail
    jmp puts

print_filename:
    push ax
    push cx
    push si
    mov cx, 8
.name:
    lodsb
    cmp al, ' '
    je .name_skip
    mov ah, 0Eh
    int 10h
.name_skip:
    loop .name
    mov al, '.'
    mov ah, 0Eh
    int 10h
    mov cx, 3
.extension:
    lodsb
    cmp al, ' '
    je .extension_skip
    mov ah, 0Eh
    int 10h
.extension_skip:
    loop .extension
    mov si, endl_string
    call puts
    pop si
    pop cx
    pop ax
    ret

puts:
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
    ret

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
    pop ax
    mov ah, 02h
    mov di, 3
.retry:
    pusha
    stc
    int 13h
    jnc .done
    popa
    call disk_reset
    dec di
    jnz .retry
    stc
    jmp .exit
.done:
    popa
    clc
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

msg_hello     db 'Kernel loaded successfully. Welcome!', ENDL, 0
msg_help      db 'Commands: help, cls, version, dir', ENDL
              db 'Type a .BIN filename to run it (for example: hello).', ENDL, 0
msg_version   db 'DariOS, DOS for short. Version 1', ENDL, 0
msg_unknown   db 'Unknown command', ENDL, 0
msg_not_found db 'Program not found.', ENDL, 0
msg_bad_name  db 'Invalid 8.3 program name.', ENDL, 0
msg_dir_fail  db 'Disk read failed.', ENDL, 0
prompt        db '> ', 0
endl_string   db ENDL, 0

cmd_help      db 'help', 0
cmd_cls       db 'cls', 0
cmd_version   db 'version', 0
cmd_dir       db 'dir', 0

buffer               times 64 db 0
fat_name             times 11 db 0
boot_drive           db 0
current_cluster      dw 0

bdb_bytes_per_sector dw 512
bdb_reserved_sectors dw 1
bdb_fat_count        db 2
bdb_dir_entries_count dw 224
bdb_sectors_per_fat  db 9
bdb_sectors_per_track dw 18
bdb_heads            dw 2

fat_buffer           times 4608 db 0
root_directory       times 7168 db 0
