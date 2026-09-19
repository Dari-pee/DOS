[bits 32]

section .text

global bios_set_video_mode
global bios_read_sector

CODE_SEG32 equ 0x08
DATA_SEG32 equ 0x10
CODE_SEG16 equ 0x18
DATA_SEG16 equ 0x20

section .data
rm_saved_esp: dd 0
rm_video_mode: db 0
rm_operation:  db 0          ; 0 = video mode, 1 = disk sector read
rm_lba:        dw 0
rm_buffer:     dw 0          ; real-mode buffer offset; must be below 64 KiB
rm_result:     db 0

rm_idt_real:
    dw 0x3FF
    dd 0

rm_idt_saved:
    dw 0
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

; int bios_read_sector(unsigned short lba, void *buffer)
; Reads one 512-byte floppy sector to a buffer below physical 0x10000.
; Returns 0 on success and 1 when BIOS int 13h reports an error.
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
    ; Convert LBA to CHS for a 1.44 MiB floppy: 18 sectors/track, 2 heads.
    mov ax, [rm_lba]
    xor dx, dx
    mov bx, 36
    div bx
    mov ch, al

    mov ax, dx
    xor dx, dx
    mov bx, 18
    div bx
    mov dh, al
    mov cl, dl
    inc cl

    mov bx, [rm_buffer]
    mov dl, [0x0500]
    mov ah, 0x02
    mov al, 0x01
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
