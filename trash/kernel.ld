ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(EXCLUDE_FILE(*realmode.o) .text)
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4) {
        *(EXCLUDE_FILE(*realmode.o) .data)
    }

    .realmode 0x1000 : AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 4)) {
        *realmode.o(.text)
        *realmode.o(.data)
    }

    . = ALIGN(LOADADDR(.realmode) + SIZEOF(.realmode), 4);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        . = ALIGN(4);
    }
    _end = .;

    __rm_load  = LOADADDR(.realmode);
    __rm_start = ADDR(.realmode);
    __rm_end   = ADDR(.realmode) + SIZEOF(.realmode);

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
