; EDITOR.ASM -- text editor application for DariOS (not DOS)
; NASM: nasm -f bin EDITOR.ASM -o EDITOR.BIN
;
; The DariOS shell loads .BIN files at 3000:0000 and CALL FARs them.
; This app uses the BIOS and the same 1.44 MB FAT12 layout as main.asm.
; It edits the already-existing TEST.TXT file in the floppy image.

bits 16
org 0

%define ENDL             13,10
%define ROOT_DIR_LBA     19
%define ROOT_DIR_SECTORS 14
%define DATA_LBA         33
%define MAX_BUFFER       4096

start:
    push cs
    pop ds
    push cs
    pop es
    mov [boot_drive],dl
    mov word [text_len],0
    mov word [cursor],0
    call load_text
    jc startup_error

editor_loop:
    call redraw
    xor ah,ah
    int 16h
    cmp al,27                 ; Esc: return to the DariOS shell
    je finish
    cmp al,19                 ; Ctrl-S: save
    je save_key
    cmp al,8
    je backspace
    cmp al,13
    je enter_key
    or al,al
    jz extended
    cmp al,32
    jb editor_loop
    call insert_byte
    jmp editor_loop

extended:
    xor ah,ah
    int 16h
    cmp ah,4Bh                ; left
    je left_key
    cmp ah,4Dh                ; right
    je right_key
    cmp ah,53h                ; delete
    je delete_key
    jmp editor_loop
left_key:
    cmp word [cursor],0
    je editor_loop
    dec word [cursor]
    jmp editor_loop
right_key:
    mov ax,[cursor]
    cmp ax,[text_len]
    jae editor_loop
    inc word [cursor]
    jmp editor_loop
backspace:
    cmp word [cursor],0
    je editor_loop
    dec word [cursor]
    call delete_byte
    jmp editor_loop
delete_key:
    call delete_byte
    jmp editor_loop
enter_key:
    mov al,13
    call insert_byte
    mov al,10
    call insert_byte
    jmp editor_loop
save_key:
    call save_text
    jmp editor_loop

startup_error:
    call clear_screen
    mov si,error_message
    call puts
    mov al,[error_code]
    call putc
    mov si,error_suffix
    call puts
    xor ah,ah
    int 16h
finish:
    retf

; Insert AL at cursor, if the TEST.TXT cluster chain has enough capacity.
insert_byte:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx,[text_len]
    cmp bx,[text_capacity]
    jae .done
    mov cx,bx
    sub cx,[cursor]
    jcxz .store
    mov si,text_buffer
    add si,bx
    dec si
    mov di,si
    inc di
    std
    rep movsb
    cld
.store:
    mov di,text_buffer
    add di,[cursor]
    pop ax
    push ax
    mov [di],al
    inc word [text_len]
    inc word [cursor]
.done:
    cld
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

delete_byte:
    push ax
    push cx
    push si
    push di
    mov ax,[cursor]
    cmp ax,[text_len]
    jae .done
    mov cx,[text_len]
    sub cx,ax
    dec cx
    jcxz .shrink
    mov si,text_buffer
    add si,ax
    inc si
    mov di,si
    dec di
    rep movsb
.shrink:
    dec word [text_len]
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; Draw whole display. This deliberately stays simple for a 16-bit BIOS system.
redraw:
    call clear_screen
    mov si,title
    call puts
    mov cx,[text_len]
    mov si,text_buffer
    jcxz .status
.text:
    lodsb
    or al,al
    jz .next                 ; never render unused zero-filled file space
    cmp al,13
    je .next
    cmp al,10
    jne .character
    mov al,13
    call putc
    mov al,10
    call putc
    jmp .next
.character:
    call putc
.next:
    loop .text
.status:
    mov dh,24
    xor dl,dl
    call set_cursor
    mov si,help
    call puts
    mov dh,1
    xor dl,dl
    mov cx,[cursor]
    mov si,text_buffer
    jcxz .set
.find_cursor:
    lodsb
    cmp al,10
    jne .column
    inc dh
    xor dl,dl
    jmp .advance
.column:
    cmp al,13
    je .advance
    inc dl
    cmp dl,80
    jb .advance
    xor dl,dl
    inc dh
.advance:
    loop .find_cursor
.set:
    call set_cursor
    ret

putc:
    push ax
    push bx
    mov ah,0Eh
    xor bh,bh
    mov bl,7
    int 10h
    pop bx
    pop ax
    ret
puts:
    lodsb
    or al,al
    jz .done
    call putc
    jmp puts
.done:
    ret
clear_screen:
    mov ah,06h
    xor al,al
    mov bh,1Fh               ; white text on the kernel's blue background
    xor ch,ch
    xor cl,cl
    mov dh,24
    mov dl,79
    int 10h
    mov ah,02h
    xor bh,bh
    xor dh,dh
    xor dl,dl
    int 10h
    ret
set_cursor:
    mov ah,02h
    xor bh,bh
    int 10h
    ret

; Locate TEST.TXT in the root directory. Saves its first cluster, size and entry.
find_text_file:
    mov ax,ROOT_DIR_LBA
    mov [scan_lba],ax
    mov cx,ROOT_DIR_SECTORS
.sector:
    push cx
    mov bx,sector_buffer
    mov ax,[scan_lba]
    call disk_read
    jc .error
    mov di,sector_buffer
    mov cx,16
.entry:
    cmp byte [di],0
    je .missing
    cmp byte [di],0E5h
    je .skip
    mov al,[di+11]
    test al,18h
    jnz .skip
    cmp al,0Fh
    je .skip
    push di
    mov si,target_name
    mov cx,11
    repe cmpsb
    pop di
    je .found
.skip:
    add di,32
    loop .entry
    inc word [scan_lba]
    pop cx
    loop .sector
.missing:
    pop cx
    stc
    ret
.found:
    mov ax,[di+26]
    mov [first_cluster],ax
    mov ax,[di+28]
    mov [file_size],ax
    mov ax,di
    sub ax,sector_buffer
    mov [dir_offset],ax
    mov ax,[scan_lba]
    mov [dir_lba],ax
    pop cx
    clc
    ret
.error:
    pop cx
    stc
    ret

load_fat:
    mov ax,1
    mov bx,fat_buffer
    mov cx,9
    call disk_read_many
    ret

; Read TEST.TXT, and determine its allocated cluster-chain capacity.
load_text:
    call load_fat
    jnc .fat_ok
    mov byte [error_code],'F' ; FAT sectors could not be read
    stc
    ret
.fat_ok:
    call find_text_file
    jnc .file_ok
    mov byte [error_code],'N' ; TEST.TXT was not found in the root directory
    stc
    ret
.file_ok:
    mov ax,[file_size]
    cmp ax,MAX_BUFFER
    jbe .size_ok
    mov ax,MAX_BUFFER
.size_ok:
    mov [text_len],ax
    mov word [cursor],0
    mov word [text_capacity],0
    mov ax,[first_cluster]
    mov [current_cluster],ax
    mov di,text_buffer
    xor bp,bp                 ; bytes copied
.cluster:
    mov ax,[text_capacity]
    cmp ax,MAX_BUFFER
    jae .done
    add ax,512
    cmp ax,MAX_BUFFER
    jbe .capacity_ok
    mov ax,MAX_BUFFER
.capacity_ok:
    mov [text_capacity],ax
    mov ax,[current_cluster]
    call cluster_lba
    mov bx,sector_buffer
    call disk_read
    jc .error
    mov cx,[text_len]
    sub cx,bp
    jbe .no_copy
    cmp cx,512
    jbe .copy_count_ok
    mov cx,512
.copy_count_ok:
    push si
    push cx
    mov si,sector_buffer
    rep movsb
    pop cx
    pop si
    add bp,cx
.no_copy:
    call next_cluster
    cmp ax,0FF8h
    jae .done
    cmp ax,2
    jb .done
    mov [current_cluster],ax
    jmp .cluster
.done:
    ; A preallocated TEST.TXT is zero-filled by the build. Treat trailing
    ; zero bytes as unused space, so a new 4 KiB file opens as an empty text.
    mov bx,[text_len]
.trim_zeroes:
    or bx,bx
    jz .trim_done
    dec bx
    cmp byte [text_buffer+bx],0
    je .trim_zeroes
    inc bx
.trim_done:
    mov [text_len],bx
    clc
    ret
.error:
    mov byte [error_code],'D' ; a TEST.TXT data sector could not be read
    stc
    ret

; Save the buffer into the existing TEST.TXT cluster chain, then update its size.
save_text:
    mov ax,[first_cluster]
    mov [current_cluster],ax
    mov si,text_buffer
    mov bp,[text_len]
.cluster:
    cmp bp,0
    je .update_directory
    mov di,sector_buffer
    xor ax,ax
    mov cx,256
    rep stosw
    mov cx,bp
    cmp cx,512
    jbe .copy_count_ok
    mov cx,512
.copy_count_ok:
    push cx
    mov di,sector_buffer
    rep movsb
    pop cx
    sub bp,cx
    mov ax,[current_cluster]
    call cluster_lba
    mov bx,sector_buffer
    call disk_write
    jc .error
    cmp bp,0
    je .update_directory
    call next_cluster
    cmp ax,0FF8h
    jae .error
    mov [current_cluster],ax
    jmp .cluster
.update_directory:
    mov ax,[dir_lba]
    mov bx,sector_buffer
    call disk_read
    jc .error
    mov di,sector_buffer
    add di,[dir_offset]
    mov ax,[text_len]
    mov [di+28],ax
    mov word [di+30],0
    mov ax,[dir_lba]
    mov bx,sector_buffer
    call disk_write
.error:
    ret

; AX cluster number -> AX LBA.  One sector per cluster on this floppy format.
cluster_lba:
    sub ax,2
    add ax,DATA_LBA
    ret
; Return next FAT12 cluster in AX (input AX = current cluster).
next_cluster:
    push bx
    push dx
    mov bx,3
    mul bx
    mov bx,2
    div bx
    mov bx,fat_buffer
    add bx,ax
    mov ax,[bx]
    or dx,dx
    jz .even
    shr ax,4
    jmp .done
.even:
    and ax,0FFFh
.done:
    pop dx
    pop bx
    ret

; AX=LBA, ES:BX=buffer. Sector count in CX for read-many.
disk_read_many:
.loop:
    push ax
    push cx
    call disk_read
    jc .fail
    pop cx
    pop ax
    inc ax
    add bx,512
    loop .loop
    clc
    ret
.fail:
    pop cx
    pop ax
    stc
    ret
disk_read:
    push bx
    push cx
    push dx
    push di
    mov [io_lba],ax
    mov di,3
.retry:
    mov ax,[io_lba]
    call lba_to_chs
    mov ax,0201h
    int 13h
    jnc .done
    xor ah,ah
    mov dl,[boot_drive]
    int 13h
    dec di
    jnz .retry
    stc
.done:
    pop di
    pop dx
    pop cx
    pop bx
    ret
disk_write:
    push bx
    push cx
    push dx
    push di
    mov [io_lba],ax
    mov di,3
.retry:
    mov ax,[io_lba]
    call lba_to_chs
    mov ax,0301h
    int 13h
    jnc .done
    xor ah,ah
    mov dl,[boot_drive]
    int 13h
    dec di
    jnz .retry
    stc
.done:
    pop di
    pop dx
    pop cx
    pop bx
    ret
lba_to_chs:
    push bx
    xor dx,dx
    mov bx,18
    div bx
    inc dx
    mov cx,dx
    xor dx,dx
    mov bx,2
    div bx
    mov dh,dl
    mov ch,al
    shl ah,6
    or cl,ah
    mov dl,[boot_drive]
    pop bx
    ret

title          db 'DariOS Editor - TEST.TXT',ENDL,0
help           db 'Esc shell | Ctrl-S save | arrows, Backspace, Del',0
error_message  db 'Editor load error: ',0
error_suffix   db ENDL,'F=FAT  N=name  D=data sector',ENDL
               db 'Press a key to return.',0
target_name    db 'TEST    TXT'
boot_drive     db 0
scan_lba       dw 0
dir_lba        dw 0
dir_offset     dw 0
first_cluster  dw 0
current_cluster dw 0
file_size      dw 0
text_len       dw 0
text_capacity  dw 0
cursor         dw 0
io_lba         dw 0
error_code     db '?'
sector_buffer  times 512 db 0
fat_buffer     times 4608 db 0
text_buffer    times MAX_BUFFER db 0
