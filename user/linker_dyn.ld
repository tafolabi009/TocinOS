/* Dynamically linked user program linker script for TocinOS */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

INTERP = /lib/ld.so

SECTIONS
{
    /* Standard ELF load address for 32-bit Linux */
    . = 0x08048000;

    .interp : { *(.interp) }

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
    }

    .got.plt : {
        *(.got.plt)
    }

    .plt : {
        *(.plt)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
