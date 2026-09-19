[bits 32]
section .text
global desktop_draw
global desktop_draw_start_menu

SCREEN equ 0xA0000
WIDTH  equ 320

; AL = colour, EDI = destination, ECX = width, EDX = height.
; Each row starts exactly WIDTH pixels after the previous one.
fill_rect:
    push ebx
    push esi
    mov esi, ecx
    mov ebx, edx
.row:
    push edi
    mov ecx, esi
    rep stosb
    pop edi
    add edi, WIDTH
    dec ebx
    jnz .row
    pop esi
    pop ebx
    ret

; Draw one 5x7 uppercase glyph. AL = A..Z, EDI = top-left, BL = colour.
draw_glyph:
    push eax
    push ecx
    push edx
    push esi
    push ebp
    cmp al, 'A'
    jb .done
    cmp al, 'Z'
    ja .done
    movzx eax, al
    sub eax, 'A'
    imul eax, 7
    mov esi, font_data
    add esi, eax
    mov edx, edi
    mov ebp, 7
.row:
    mov al, [esi]
    inc esi
    shl al, 3
    xor ecx, ecx
.column:
    shl al, 1
    jnc .skip_pixel
    mov [edx + ecx], bl
.skip_pixel:
    inc ecx
    cmp ecx, 5
    jb .column
    add edx, WIDTH
    dec ebp
    jnz .row
.done:
    pop ebp
    pop esi
    pop edx
    pop ecx
    pop eax
    ret

; ESI = NUL-terminated uppercase text, EDI = top-left, BL = colour.
draw_text:
.next:
    lodsb
    test al, al
    jz .done
    call draw_glyph
    add edi, 6
    jmp .next
.done:
    ret

desktop_draw:
    pushad
    cld

    ; Neutral background, top bar, and taskbar.
    mov edi, SCREEN
    mov ecx, 320
    mov edx, 200
    mov al, 8
    call fill_rect

    mov edi, SCREEN
    mov ecx, 320
    mov edx, 18
    mov al, 0
    call fill_rect

    mov edi, SCREEN + (WIDTH * 178)
    mov ecx, 320
    mov edx, 22
    mov al, 7
    call fill_rect

    ; Start button.
    mov edi, SCREEN + (WIDTH * 182) + 6
    mov ecx, 54
    mov edx, 14
    mov al, 2
    call fill_rect
    mov esi, label_start
    mov edi, SCREEN + (WIDTH * 185) + 12
    mov bl, 15
    call draw_text

    ; Two icons: white border and coloured centre.
    mov edi, SCREEN + (WIDTH * 30) + 20
    mov ecx, 42
    mov edx, 42
    mov al, 15
    call fill_rect
    mov edi, SCREEN + (WIDTH * 33) + 23
    mov ecx, 36
    mov edx, 36
    mov al, 1
    call fill_rect

    mov edi, SCREEN + (WIDTH * 90) + 20
    mov ecx, 42
    mov edx, 42
    mov al, 15
    call fill_rect
    mov edi, SCREEN + (WIDTH * 93) + 23
    mov ecx, 36
    mov edx, 36
    mov al, 5
    call fill_rect

    ; Main window: black border, white body, and a green title bar.
    mov edi, SCREEN + (WIDTH * 32) + 82
    mov ecx, 180
    mov edx, 118
    mov al, 0
    call fill_rect
    mov edi, SCREEN + (WIDTH * 34) + 84
    mov ecx, 176
    mov edx, 114
    mov al, 7
    call fill_rect
    mov edi, SCREEN + (WIDTH * 34) + 84
    mov ecx, 176
    mov edx, 18
    mov al, 2
    call fill_rect

    ; Red close button; coordinates match kernel.c.
    mov edi, SCREEN + (WIDTH * 4) + 296
    mov ecx, 16
    mov edx, 16
    mov al, 4
    call fill_rect

    mov esi, label_dos
    mov edi, SCREEN + 8
    mov bl, 15
    call draw_text

    popad
    ret

; Popup menu displayed above the Start button.  Application entries are
; colour-coded placeholders until the first built-in programs are added.
desktop_draw_start_menu:
    pushad
    cld

    ; Black outline and light menu body.
    mov edi, SCREEN + (WIDTH * 104) + 6
    mov ecx, 122
    mov edx, 74
    mov al, 0
    call fill_rect
    mov edi, SCREEN + (WIDTH * 106) + 8
    mov ecx, 118
    mov edx, 70
    mov al, 7
    call fill_rect

    ; Header plus three menu entries.
    mov edi, SCREEN + (WIDTH * 108) + 10
    mov ecx, 114
    mov edx, 12
    mov al, 2
    call fill_rect

    mov edi, SCREEN + (WIDTH * 124) + 14
    mov ecx, 106
    mov edx, 14
    mov al, 1
    call fill_rect
    mov edi, SCREEN + (WIDTH * 142) + 14
    mov ecx, 106
    mov edx, 14
    mov al, 5
    call fill_rect
    mov edi, SCREEN + (WIDTH * 160) + 14
    mov ecx, 106
    mov edx, 14
    mov al, 6
    call fill_rect

    mov esi, label_programs
    mov edi, SCREEN + (WIDTH * 111) + 14
    mov bl, 15
    call draw_text
    mov esi, label_about
    mov edi, SCREEN + (WIDTH * 127) + 18
    mov bl, 15
    call draw_text
    mov esi, label_shell
    mov edi, SCREEN + (WIDTH * 145) + 18
    mov bl, 15
    call draw_text
    mov esi, label_exit
    mov edi, SCREEN + (WIDTH * 163) + 18
    mov bl, 15
    call draw_text

    popad
    ret

section .rodata
label_start:    db 'START', 0
label_dos:      db 'DOS', 0
label_programs: db 'PROGRAMS', 0
label_about:    db 'ABOUT', 0
label_shell:    db 'SHELL', 0
label_exit:     db 'EXIT', 0

; Five pixels wide, seven rows high, ordered A through Z.
font_data:
    db 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 ; A
    db 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E ; B
    db 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E ; C
    db 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E ; D
    db 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F ; E
    db 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 ; F
    db 0x0E,0x11,0x10,0x17,0x11,0x11,0x0E ; G
    db 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 ; H
    db 0x1F,0x04,0x04,0x04,0x04,0x04,0x1F ; I
    db 0x01,0x01,0x01,0x01,0x11,0x11,0x0E ; J
    db 0x11,0x12,0x14,0x18,0x14,0x12,0x11 ; K
    db 0x10,0x10,0x10,0x10,0x10,0x10,0x1F ; L
    db 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 ; M
    db 0x11,0x19,0x15,0x13,0x11,0x11,0x11 ; N
    db 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E ; O
    db 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 ; P
    db 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D ; Q
    db 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 ; R
    db 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E ; S
    db 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 ; T
    db 0x11,0x11,0x11,0x11,0x11,0x11,0x0E ; U
    db 0x11,0x11,0x11,0x11,0x11,0x0A,0x04 ; V
    db 0x11,0x11,0x11,0x15,0x15,0x15,0x0A ; W
    db 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 ; X
    db 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 ; Y
    db 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F ; Z
