[bits 32]

section .text
global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
    cli
    cld

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    push ebx
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
