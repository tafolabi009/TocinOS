/* User program linker script for TocinOS */
ENTRY(_start)

SECTIONS
{
    /* Standard ELF load address for 32-bit Linux */
    . = 0x08048000;

    .text : {
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

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
