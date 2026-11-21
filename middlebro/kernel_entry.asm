section .text
[bits 32]

global _start
extern kernel_main
extern __rm_load
extern __rm_start
extern __rm_end
extern __bss_start
extern _end

_start:
    mov esp, 0x90000
    mov ebp, esp

    mov byte [0xB8000], 'E'
    mov byte [0xB8001], 0x0A

    cld
    mov esi, __rm_load
    mov edi, __rm_start
    mov ecx, __rm_end
    sub ecx, edi
    rep movsb
    mov edi, __bss_start
    mov ecx, _end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
