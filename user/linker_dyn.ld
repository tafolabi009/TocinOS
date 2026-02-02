/* Dynamically linked user program linker script for TocinOS */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

PHDRS
{
    headers PT_PHDR PHDRS;
    interp  PT_INTERP;
    text    PT_LOAD FLAGS(5);    /* R-X */
    data    PT_LOAD FLAGS(6);    /* RW- */
    dynamic PT_DYNAMIC;
}

SECTIONS
{
    /* Standard ELF load address for 32-bit Linux */
    . = 0x08048000;

    .interp : { 
        /* The interpreter path - FAT converts to uppercase */
        BYTE(0x2F)  /* / */
        BYTE(0x4C)  /* L */
        BYTE(0x44)  /* D */
        BYTE(0x2E)  /* . */
        BYTE(0x53)  /* S */
        BYTE(0x4F)  /* O */
        BYTE(0x00)  /* NUL */
    } :interp

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
