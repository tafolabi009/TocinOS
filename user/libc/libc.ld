/* Shared library linker script for TocinOS libc.so */
OUTPUT_FORMAT("elf32-i386")

SECTIONS
{
    /* Load at a fixed address for now */
    . = 0x10000000;

    .text : {
        *(.text.start)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .dynamic : {
        *(.dynamic)
    }

    .dynsym : {
        *(.dynsym)
    }

    .dynstr : {
        *(.dynstr)
    }

    .hash : {
        *(.hash)
    }

    .rel.dyn : {
        *(.rel.dyn)
        *(.rel.plt)
    }

    .got : {
        *(.got)
        *(.got.plt)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
