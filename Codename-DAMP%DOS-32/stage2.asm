%include "config.inc"
%if KERNEL_ADDR < 0x10000
  %if KERNEL_ADDR < 0x1000 || KERNEL_ADDR + KERNEL_SECTORS*512 > 0x7000
    %error "KERNEL_ADDR below 0x10000 only works for kernels that end before 0x7000"
  %endif
%elif KERNEL_ADDR + KERNEL_SECTORS*512 > 0x80000
  %error "Kernel too big: must end below 0x80000 (it is loaded with the BIOS)"
%endif
%if RAMTEST_MAX_MB > 4095
  %error "RAMTEST_MAX_MB must be <= 4095"
%endif

[bits 16]
[org STAGE2_ADDR]
stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    mov ax, 0x0003
    int 0x10
    call serial_init
    mov si, s_banner
    call puts16

    xor eax, eax
    mov di, BOOTINFO_ADDR
    mov cx, BI_SIZE/4
    rep stosd
    mov di, MMAP_ADDR
    mov cx, (MMAP_MAX*24)/4
    rep stosd
    mov dword [BI_MAGIC], BOOT_MAGIC
    mov dword [BI_VERSION], 1
    movzx eax, byte [boot_drive]
    mov [BI_BOOTDRV], eax
    mov dword [BI_KADDR], KERNEL_ADDR
    mov dword [BI_KSECT], KERNEL_SECTORS

    call disk_init


    mov si, s_a20
    call puts16
    call enable_a20
    test ax, ax
    jnz .a20ok
    mov si, s_a20_fail
    jmp fatal
.a20ok:
    or dword [BI_FLAGS], BIF_A20
    mov si, s_ok
    call puts16
    mov si, s_mem
    call puts16
    int 0x12
    movzx eax, ax
    mov [BI_LOWMEM], eax
    call detect_memory
    cmp dword [BI_MMAPCNT], 0
    jne .memok
    mov si, s_mem_fail
    jmp fatal
.memok:
    mov si, s_ok
    call puts16
    call detect_cpu
    mov si, s_kernel
    call puts16
    call load_kernel
    mov si, s_ok
    call puts16
    mov si, s_pm
    call puts16
    mov ah, 0x03
    xor bh, bh
    int 0x10
    mov [con_row], dh
    mov [con_col], dl

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pm_entry

disk_error:
    mov si, s_disk_err
fatal:
    call puts16
halt16:
    cli
    hlt
    jmp halt16

load_kernel:
    pusha
    mov ax, KERNEL_ADDR >> 4
    mov es, ax
    xor bx, bx
    mov eax, KERNEL_LBA
    mov cx, KERNEL_SECTORS
.l:
    call read_sector
    inc eax
    mov dx, es
    add dx, 0x20
    mov es, dx
    test cl, 31                 ; progress dot every 32 sectors
    jnz .nodot
    push ax
    mov al, '.'
    call putc16
    pop ax
.nodot:
    loop .l
    xor ax, ax
    mov es, ax
    popa
    ret

; A20 gate. Returns AX=1 if enabled, 0 if all methods failed.
; Methods: already on BIOS INT 15h and fast gate (port 0x92)

enable_a20:
    call a20_check
    test ax, ax
    jnz .ok
    mov ax, 0x2401              ; BIOS method
    int 0x15
    call a20_wait
    test ax, ax
    jnz .ok
    in al, 0x92                 ; fast A20 gate
    test al, 2
    jnz .kbc
    or al, 2
    and al, 0xFE                ; never write the reset bit
    out 0x92, al
    call a20_wait
    test ax, ax
    jnz .ok
.kbc:
    cli
    call kbc_wait_in 
    mov al, 0xAD                ; disable keyboard
    out 0x64, al
    call kbc_wait_in
    mov al, 0xD0                ; read output port
    out 0x64, al
    call kbc_wait_out
    in al, 0x60
    push ax
    call kbc_wait_in
    mov al, 0xD1                ; write output port
    out 0x64, al
    call kbc_wait_in
    pop ax
    or al, 2                    ; set A20 bit
    out 0x60, al
    call kbc_wait_in
    mov al, 0xAE                ; enable keyboard
    out 0x64, al
    call kbc_wait_in
    sti
    call a20_wait
    test ax, ax
    jnz .ok
    xor ax, ax
    ret
.ok:
    mov ax, 1
    ret

a20_wait:
    mov cx, 0x400
.l: call a20_check
    test ax, ax
    jnz .r
    loop .l
.r: ret

a20_check:
    pushf
    push ds
    push es
    push si
    push di
    cli
    xor ax, ax
    mov es, ax
    not ax
    mov ds, ax                  ; DS = 0xFFFF
    mov di, 0x7DFE
    mov si, 0x7E0E
    mov al, [es:di]
    push ax
    mov al, [ds:si]
    push ax
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    cmp byte [es:di], 0xFF
    pop ax
    mov [ds:si], al             ; restore both bytes
    pop ax
    mov [es:di], al
    mov ax, 0
    je .done
    mov ax, 1
.done:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

kbc_wait_in: ; waiy for input buff
    push cx
    mov cx, 0xFFFF
.l: in al, 0x64
    test al, 2
    jz .d
    loop .l
.d: pop cx
    ret

kbc_wait_out: ; wait for output buff
    push cx
    mov cx, 0xFFFF
.l: in al, 0x64
    test al, 1
    jnz .d
    loop .l
.d: pop cx
    ret

detect_memory:
    pushad
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .no801
    cmp ah, 0x86
    je .no801
    cmp ah, 0x80
    je .no801
    jcxz .axbx
    mov ax, cx                  ; some BIOSes return the result in CX/DX
    mov bx, dx
.axbx:
    movzx eax, ax
    movzx ebx, bx
    mov [BI_EXTKB], eax
    mov [BI_EXT64K], ebx
    mov byte [have801], 1
.no801:
    call detect_e820
    cmp word [mmap_cnt], 0
    je .synth
    or dword [BI_FLAGS], BIF_E820
    jmp .done

.synth:
    mov di, MMAP_ADDR
    mov eax, [BI_LOWMEM]
    shl eax, 10
    mov [di+8], eax
    mov dword [di+16], 1
    add di, 24
    inc word [mmap_cnt]
    cmp byte [have801], 0
    je .try88
    mov dword [di], 0x100000
    mov eax, [BI_EXTKB]
    shl eax, 10
    mov [di+8], eax
    mov dword [di+16], 1
    add di, 24
    inc word [mmap_cnt]
    mov eax, [BI_EXT64K]
    test eax, eax
    jz .done
    mov dword [di], 0x1000000   ; entry 2: above 16 MB
    shl eax, 16
    mov [di+8], eax
    mov dword [di+16], 1
    inc word [mmap_cnt]
    jmp .done
.try88:
    mov ah, 0x88                ; KB above 1 MB (max 64 MB)
    int 0x15
    jc .done
    movzx eax, ax
    test eax, eax
    jz .done
    mov dword [di], 0x100000
    shl eax, 10
    mov [di+8], eax
    mov dword [di+16], 1
    inc word [mmap_cnt]
.done:
    movzx eax, word [mmap_cnt]
    mov [BI_MMAPCNT], eax
    mov dword [BI_MMAPADDR], MMAP_ADDR
    popad
    ret

detect_e820:
    xor ebx, ebx
    mov di, MMAP_ADDR
    mov word [mmap_cnt], 0
.next:
    mov dword [di+20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .end
    cmp eax, 0x534D4150
    jne .end
    cmp ecx, 20
    jb .skip
    mov eax, [di+8]
    or eax, [di+12]
    jz .skip
    test byte [di+20], 1
    jz .skip
    add di, 24
    inc word [mmap_cnt]
    cmp word [mmap_cnt], MMAP_MAX
    jae .end
.skip:
    test ebx, ebx
    jnz .next
.end:
    ret


detect_cpu:
    pushad
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x00200000
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 0x00200000
    jz .done
    or dword [BI_FLAGS], BIF_CPUID
    xor eax, eax
    cpuid
    mov [BI_CPUIDMAX], eax
    mov [BI_VENDOR], ebx
    mov [BI_VENDOR+4], edx
    mov [BI_VENDOR+8], ecx
    cmp dword [BI_CPUIDMAX], 1
    jb .ext
    mov eax, 1
    cpuid
    mov [BI_FEAT_EDX], edx
    mov [BI_FEAT_ECX], ecx
.ext:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .done
    mov eax, 0x80000001
    cpuid
    mov [BI_EXT_EDX], edx
    test edx, 1 << 29
    jz .done
    or dword [BI_FLAGS], BIF_LONGMODE
.done:
    popad
    ret

serial_init:
    pusha
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 3
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    mov dx, 0x3FF
    mov al, 0xAE
    out dx, al
    in al, dx
    cmp al, 0xAE
    jne .done
    mov byte [serial_ok], 1
.done:
    popa
    ret

putc16:
    pusha
    cmp byte [serial_ok], 0
    je .bios
    mov bl, al
    mov dx, 0x3FD
.w: in al, dx
    test al, 0x20
    jz .w
    mov dx, 0x3F8
    mov al, bl
    out dx, al
    mov al, bl
.bios:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    popa
    ret

puts16:
    push ax
.l: lodsb
    test al, al
    jz .d
    call putc16
    jmp .l
.d: pop ax
    ret

%include "diskread.inc"

mmap_cnt:   dw 0
serial_ok:  db 0
have801:    db 0
con_row:    db 0
con_col:    db 0

s_banner:   db "Stage 2 loader", 13, 10, 0
s_a20:      db "A20 gate........ ", 0
s_mem:      db "Memory map...... ", 0
s_kernel:   db "Loading kernel.. ", 0
s_pm:       db "Entering protected mode", 13, 10, 13, 10, 0
s_ok:       db "OK", 13, 10, 0
s_a20_fail: db "FAILED", 13, 10, "Could not enable the A20 line.", 13, 10, 0
s_mem_fail: db "FAILED", 13, 10, "No usable memory map from BIOS.", 13, 10, 0
s_disk_err: db 13, 10, "Disk read error", 13, 10, 0

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

;  PROTECTED MODE
[bits 32]

%macro PUTS 1
    mov esi, %1
    call con_puts
%endmacro

pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    cld

    call print_summary
    call ram_test
    call update_cursor

    mov eax, BOOT_MAGIC
    mov ebx, BOOTINFO_ADDR
    push dword BOOTINFO_ADDR
    push dword 0
    jmp KERNEL_ADDR

; compute totals measure total kb and limit of ram
compute_totals:
    pushad
    xor edi, edi                ; total KB
    xor ebp, ebp                ; top of RAM (clamped to 4 GB)
    mov esi, MMAP_ADDR
    mov ecx, [BI_MMAPCNT]
.l: test ecx, ecx
    jz .done
    cmp dword [esi+16], 1
    jne .next
    mov eax, [esi+8]
    mov edx, [esi+12]
    shrd eax, edx, 10           ; length in KB
    add edi, eax
    mov eax, [esi]
    mov edx, [esi+4]
    add eax, [esi+8]
    adc edx, [esi+12]
    test edx, edx
    jz .nc
    mov eax, 0xFFFFFFFF
.nc:
    cmp eax, ebp
    jbe .next
    mov ebp, eax
.next:
    add esi, 24
    dec ecx
    jmp .l
.done:
    mov [BI_TOTALKB], edi
    mov [BI_RAMTOP], ebp
    popad
    ret

print_summary:
    pushad
    call compute_totals

    PUTS s_drive
    mov eax, [BI_BOOTDRV]
    call con_hex32
    PUTS s_crlf

    PUTS s_lowmem
    mov eax, [BI_LOWMEM]
    call con_dec
    PUTS s_kb_int12

    cmp dword [BI_EXTKB], 0
    jne .e801
    cmp dword [BI_EXT64K], 0
    je .nomap801
.e801:
    PUTS s_e801a
    mov eax, [BI_EXTKB]
    call con_dec
    PUTS s_e801b
    mov eax, [BI_EXT64K]
    call con_dec
    PUTS s_e801c
.nomap801:

    PUTS s_mmap
    mov eax, [BI_MMAPCNT]
    call con_dec
    test dword [BI_FLAGS], BIF_E820
    jnz .e820
    PUTS s_entries_synth
    jmp .list
.e820:
    PUTS s_entries_e820
.list:
    mov ebx, MMAP_ADDR
    mov ecx, [BI_MMAPCNT]
.ent:
    test ecx, ecx
    jz .entdone
    PUTS s_indent
    mov eax, [ebx+4]
    call con_hex32_bare
    mov eax, [ebx]
    call con_hex32_bare
    PUTS s_plus
    mov eax, [ebx+12]
    call con_hex32_bare
    mov eax, [ebx+8]
    call con_hex32_bare
    mov al, ' '
    call con_putc
    mov esi, s_t_unk
    mov eax, [ebx+16]
    cmp eax, 1
    jb .tp
    cmp eax, 5
    ja .tp
    mov esi, [type_tbl + eax*4 - 4]
.tp:
    call con_puts
    PUTS s_crlf
    add ebx, 24
    dec ecx
    jmp .ent
.entdone:

    PUTS s_usable
    mov eax, [BI_TOTALKB]
    call con_dec
    PUTS s_kb_paren
    mov eax, [BI_TOTALKB]
    shr eax, 10
    call con_dec
    PUTS s_mb_close

    PUTS s_ramtop
    mov eax, [BI_RAMTOP]
    call con_hex32
    PUTS s_crlf

    PUTS s_cpu
    test dword [BI_FLAGS], BIF_CPUID
    jnz .cpuid
    PUTS s_nocpuid
    jmp .cpudone
.cpuid:
    mov ecx, 12
    mov ebx, BI_VENDOR
.v: mov al, [ebx]
    call con_putc
    inc ebx
    dec ecx
    jnz .v
    test dword [BI_FLAGS], BIF_LONGMODE
    jnz .lm
    PUTS s_nolm
    jmp .cpudone
.lm:
    PUTS s_lm
.cpudone:

    PUTS s_kinfo
    mov eax, KERNEL_ADDR
    call con_hex32
    PUTS s_comma
    mov eax, KERNEL_SECTORS
    call con_dec
    PUTS s_sectors
    mov eax, KERNEL_SECTORS
    shr eax, 1
    call con_dec
    PUTS s_kb_close

    PUTS s_crlf
    popad
    ret

ram_test:
%if RAMTEST_MAX_MB == 0
    ret
%else
    pushad
    PUTS s_rt_hdr
    mov eax, RAMTEST_MAX_MB
    call con_dec
    PUTS s_rt_hdr2
    mov ebp, RAMTEST_MAX_MB << 20
    mov ebx, MMAP_ADDR
    mov ecx, [BI_MMAPCNT]
.region:
    test ecx, ecx
    jz .done
    test ebp, ebp
    jz .done
    cmp dword [ebx+16], 1
    jne .skip
    cmp dword [ebx+4], 0
    jne .skip
    mov esi, [ebx]
    cmp esi, 0x100000
    jae .s1
    mov esi, 0x100000
.s1:
    add esi, 0xFFF
    and esi, 0xFFFFF000
    mov edx, [ebx+12]               ; clamped to 4 GB
    mov eax, [ebx]
    add eax, [ebx+8]
    adc edx, 0
    jz .e1
    mov eax, 0xFFFFF000
.e1:
    and eax, 0xFFFFF000
    cmp esi, eax
    jae .skip
    sub eax, esi
    cmp eax, ebp
    jbe .sz
    mov eax, ebp
.sz:
    mov edi, esi
    add edi, eax
    sub ebp, eax
    call test_range
    jc .failed
    mov eax, edi
    sub eax, esi
    shr eax, 10
    add [BI_RTKB], eax
.skip:
    add ebx, 24
    dec ecx
    jmp .region

.done:
    cmp dword [BI_RTKB], 0
    je .none
    mov dword [BI_RTSTATUS], 1
    or dword [BI_FLAGS], BIF_RAMTEST
    PUTS s_rt_pass1
    mov eax, [BI_RTKB]
    call con_dec
    PUTS s_rt_pass2
    jmp .out
.none:
    PUTS s_rt_none
    jmp .out
.failed:
    mov dword [BI_RTSTATUS], 2
    or dword [BI_FLAGS], BIF_RAMTEST
    PUTS s_rt_failed
.out:
    popad
    ret

test_range:
    pushad
    mov ebx, esi
    mov ebp, edi
    PUTS s_rt_line
    mov eax, ebx
    call con_hex32
    PUTS s_dash
    mov eax, ebp
    call con_hex32
    PUTS s_dots
    mov edx, ebp
    sub edx, ebx
    shr edx, 2

    mov eax, 0xAAAAAAAA
    call pat_pass
    jc .fail
    mov eax, 0x55555555
    call pat_pass
    jc .fail

    mov edi, ebx
    mov ecx, edx
.aw:
    mov [edi], edi
    add edi, 4
    dec ecx
    jnz .aw
    mov edi, ebx
    mov ecx, edx
.av:
    mov eax, edi
    cmp [edi], eax
    jne .fail
    add edi, 4
    dec ecx
    jnz .av

    mov edi, ebx
    mov ecx, edx
.iw:
    mov eax, edi
    not eax
    mov [edi], eax
    add edi, 4
    dec ecx
    jnz .iw
    mov edi, ebx
    mov ecx, edx
.iv:
    mov eax, edi
    not eax
    cmp [edi], eax
    jne .fail
    add edi, 4
    dec ecx
    jnz .iv

    xor eax, eax                    ; leave RAM zeroed
    call pat_pass
    jc .fail

    PUTS s_ok
    clc
    popad
    ret

.fail:
    mov edx, [edi]
    mov [BI_RTADDR], edi
    mov [BI_RTEXP], eax
    mov [BI_RTACT], edx
    PUTS s_fail1
    mov eax, [BI_RTADDR]
    call con_hex32
    PUTS s_fail2
    mov eax, [BI_RTEXP]
    call con_hex32
    PUTS s_fail3
    mov eax, [BI_RTACT]
    call con_hex32
    PUTS s_crlf
%if RAMTEST_HALT_ON_FAIL
    PUTS s_halt
    cli
.halt:
    hlt
    jmp .halt
%endif
    stc
    popad
    ret

pat_pass:
    mov edi, ebx
    mov ecx, edx
    rep stosd
    mov edi, ebx
    mov ecx, edx
    repe scasd
    je .ok
    sub edi, 4
    stc
    ret
.ok:
    clc
    ret
%endif

; Console (VGA text at 0xB8000 + COM1). All preserve registers
; except con_puts, which advances ESI.

con_putc:
    pushad
    mov bl, al
    cmp byte [serial_ok], 0
    je .vga
    mov dx, 0x3FD
.w: in al, dx
    test al, 0x20
    jz .w
    mov dx, 0x3F8
    mov al, bl
    out dx, al
.vga:
    cmp bl, 13
    je .cr
    cmp bl, 10
    je .lf
    movzx eax, byte [con_row]
    imul eax, 80
    movzx ecx, byte [con_col]
    add eax, ecx
    mov byte [0xB8000 + eax*2], bl
    mov byte [0xB8000 + eax*2 + 1], 0x07
    inc byte [con_col]
    cmp byte [con_col], 80
    jb .done
    mov byte [con_col], 0
.lf:
    inc byte [con_row]
    cmp byte [con_row], 25
    jb .done
    call con_scroll
    mov byte [con_row], 24
    jmp .done
.cr:
    mov byte [con_col], 0
.done:
    popad
    ret

con_scroll:
    pushad
    mov esi, 0xB8000 + 160
    mov edi, 0xB8000
    mov ecx, 80*24*2/4
    rep movsd
    mov edi, 0xB8000 + 80*24*2
    mov eax, 0x07200720
    mov ecx, 80*2/4
    rep stosd
    popad
    ret

con_puts:
    push eax
.l: lodsb
    test al, al
    jz .d
    call con_putc
    jmp .l
.d: pop eax
    ret

con_hex32:
    push eax
    mov al, '0'
    call con_putc
    mov al, 'x'
    call con_putc
    pop eax
con_hex32_bare:
    push eax
    push ecx
    mov ecx, 8
.l: rol eax, 4
    push eax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p: call con_putc
    pop eax
    dec ecx
    jnz .l
    pop ecx
    pop eax
    ret

con_dec:                        ; eax -> unsigned decimal
    pushad
    mov ebx, 10
    xor ecx, ecx
.d: xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .d
.p: pop eax
    add al, '0'
    call con_putc
    dec ecx
    jnz .p
    popad
    ret

update_cursor:                  ; move the blinking hardware cursor
    pushad
    movzx eax, byte [con_row]
    imul eax, 80
    movzx ecx, byte [con_col]
    add eax, ecx
    mov ebx, eax
    mov dx, 0x3D4
    mov al, 0x0F
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    dec dx
    mov al, 0x0E
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    popad
    ret

s_crlf:         db 13, 10, 0
s_indent:       db "  ", 0
s_plus:         db " +", 0
s_dash:         db "-", 0
s_dots:         db " ... ", 0
s_comma:        db ", ", 0
s_drive:        db "Boot drive : ", 0
s_lowmem:       db "Low memory : ", 0
s_kb_int12:     db " KB (INT 12h)", 13, 10, 0
s_e801a:        db "E801       : ", 0
s_e801b:        db " KB (1-16 MB), ", 0
s_e801c:        db " x 64 KB (>16 MB)", 13, 10, 0
s_mmap:         db "Memory map : ", 0
s_entries_e820: db " entries (BIOS E820)", 13, 10, 0
s_entries_synth:db " entries (built from INT 12h/E801/88h)", 13, 10, 0
s_usable:       db "Usable RAM : ", 0
s_kb_paren:     db " KB (", 0
s_mb_close:     db " MB)", 13, 10, 0
s_ramtop:       db "Top of RAM : ", 0
s_cpu:          db "CPU        : ", 0
s_nocpuid:      db "no CPUID (pre-Pentium?)", 13, 10, 0
s_lm:           db ", 64-bit capable", 13, 10, 0
s_nolm:         db ", 32-bit only", 13, 10, 0
s_kinfo:        db "Kernel     : ", 0
s_sectors:      db " sectors (", 0
s_kb_close:     db " KB)", 13, 10, 0

s_t1:           db "usable", 0
s_t2:           db "reserved", 0
s_t3:           db "ACPI reclaimable", 0
s_t4:           db "ACPI NVS", 0
s_t5:           db "bad memory", 0
s_t_unk:        db "unknown", 0
type_tbl:       dd s_t1, s_t2, s_t3, s_t4, s_t5

s_rt_hdr:       db "RAM test   : up to ", 0
s_rt_hdr2:      db " MB above 1 MB", 13, 10, 0
s_rt_line:      db "  testing ", 0
s_rt_pass1:     db "RAM test PASSED, ", 0
s_rt_pass2:     db " KB tested", 13, 10, 13, 10, 0
s_rt_none:      db "RAM test skipped: no usable RAM above 1 MB", 13, 10, 13, 10, 0
s_rt_failed:    db "RAM test FAILED - booting anyway, see boot_info", 13, 10, 13, 10, 0
s_fail1:        db "FAIL", 13, 10, "  address ", 0
s_fail2:        db " expected ", 0
s_fail3:        db " read ", 0
s_halt:         db "Halting.", 13, 10, 0

times STAGE2_SECTORS*512 - ($-$$) db 0
