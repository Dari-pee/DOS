disk_init:
    pusha
    mov byte [use_lba], 0
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .nolba
    cmp bx, 0xAA55
    jne .nolba
    test cl, 1
    jz .nolba
    mov byte [use_lba], 1
    popa
    ret
.nolba:
    push es                     ; AH=08 returns a table pointer in ES:DI - keep our ES
    mov ah, 0x08                ; get drive parameters
    mov dl, [boot_drive]
    xor di, di
    int 0x13
    pop es
    jc disk_error
    movzx ax, dh
    inc ax
    mov [heads], ax             ; heads = DH + 1
    and cx, 0x3F                ; sectors per track = CL bits 0-5
    mov [spt], cx
    popa
    ret

read_sector:
    pushad
    mov [dap_lba], eax
    mov byte [retries], 3
.retry:
    cmp byte [use_lba], 0
    je .chs
    mov [dap_off], bx
    mov [dap_seg], es
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jmp .check
.chs:
    mov eax, [dap_lba]
    xor edx, edx
    movzx ecx, word [spt]
    div ecx                     ; eax = lba / spt, edx = lba % spt
    mov cl, dl
    inc cl                      ; sector (1-based)
    xor edx, edx
    movzx edi, word [heads]
    div edi                     ; eax = cylinder, edx = head
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah                   ; cylinder bits 8-9 -> CL bits 6-7
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
.check:
    jnc .ok
    xor ah, ah                  ; reset disk and retry
    mov dl, [boot_drive]
    int 0x13
    dec byte [retries]
    jnz .retry
    jmp disk_error
.ok:
    popad
    ret

boot_drive: db 0
use_lba:    db 0
retries:    db 0
spt:        dw 0
heads:      dw 0
dap:        db 0x10, 0
            dw 1
dap_off:    dw 0
dap_seg:    dw 0
dap_lba:    dq 0
