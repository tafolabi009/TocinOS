/* Dynamic linker (ld.so) linker script for TocinOS */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS
{
    /* Load at a fixed high address to avoid conflicts with user programs */
    . = 0x40000000;

    .text : {
        *(.text.start)      /* Entry point first */
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
