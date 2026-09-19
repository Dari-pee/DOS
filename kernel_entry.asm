section .text
[bits 32]

global _start
extern kernel_main

_start:
    mov esp, 0x90000
    mov ebp, esp

    mov byte [0xB8000], 'E'
    mov byte [0xB8001], 0x0A

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang