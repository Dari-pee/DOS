[bits 32]

section .text

; w claude for adding HDD support
; w copilot for writing the easter egg command lines in kernel.c

global bios_set_video_mode
global bios_read_sector

CODE_SEG32 equ 0x08
DATA_SEG32 equ 0x10
CODE_SEG16 equ 0x18
DATA_SEG16 equ 0x20
BPB_SECTORS_PER_TRACK equ 0x7C00 + 0x18
BPB_HEADS             equ 0x7C00 + 0x1A

section .data
rm_saved_esp: dd 0
rm_video_mode: db 0
rm_operation:  db 0
rm_lba:        dw 0
rm_buffer:     dw 0
rm_result:     db 0

rm_idt_real:
    dw 0x3FF
    dd 0

rm_idt_saved:
    dw 0
    dd 0

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

section .text

bios_set_video_mode:
    push ebp
    mov ebp, esp
    pushad

    mov eax, esp
    mov [rm_saved_esp], eax

    mov al, [ebp + 8]
    mov [rm_video_mode], al
    mov byte [rm_operation], 0

    sidt [rm_idt_saved]

    cli

    jmp CODE_SEG16:pmode16_entry

bios_read_sector:
    push ebp
    mov ebp, esp
    pushad

    mov eax, esp
    mov [rm_saved_esp], eax

    mov ax, [ebp + 8]
    mov [rm_lba], ax
    mov eax, [ebp + 12]
    mov [rm_buffer], ax
    mov byte [rm_operation], 1
    mov byte [rm_result], 0

    sidt [rm_idt_saved]
    cli
    jmp CODE_SEG16:pmode16_entry

[bits 16]
pmode16_entry:
    mov ax, DATA_SEG16
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and al, 0xFE
    mov cr0, eax

    jmp 0x0000:realmode_entry

realmode_entry:
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    lidt [rm_idt_real]

    sti

    cmp byte [rm_operation], 1
    je realmode_disk_read

    mov al, [rm_video_mode]
    mov ah, 0x00
    int 0x10
    jmp realmode_done

realmode_disk_read:
    mov dl, [0x0500]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .use_chs
    cmp bx, 0xAA55
    jne .use_chs
    mov ax, [rm_lba]
    mov [dap_lba_lo], ax
    mov word [dap_lba_lo+2], 0
    mov ax, [rm_buffer]
    mov [dap_offset], ax
    mov word [dap_count], 1
    mov word [dap_segment], 0

    mov dl, [0x0500]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jnc realmode_done
    mov byte [rm_result], 1
    jmp realmode_done

.use_chs:
    mov ax, [rm_lba]
    xor dx, dx
    mov si, BPB_SECTORS_PER_TRACK
    div word [si]
    mov cl, dl
    inc cl
    xor dx, dx
    mov si, BPB_HEADS
    div word [si]
    mov ch, al
    mov dh, dl
    mov dl, [0x0500]
    mov bx, [rm_buffer]
    mov ax, 0x0201

    int 0x13
    jnc realmode_done
    mov byte [rm_result], 1

realmode_done:
    cli

    lidt [rm_idt_saved]

    mov eax, cr0
    or al, 1
    mov cr0, eax

    jmp CODE_SEG32:pmode32_entry

[bits 32]
pmode32_entry:
    mov ax, DATA_SEG32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, [rm_saved_esp]
    mov esp, eax

    popad
    pop ebp
    movzx eax, byte [rm_result]
    ret
