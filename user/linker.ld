/* User program linker script for TocinOS */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);     /* R-X */
    data PT_LOAD FLAGS(6);     /* RW- */
    interp PT_INTERP;
}

SECTIONS
{
    /* Standard ELF load address for 32-bit Linux */
    . = 0x08048000;

    .interp : { *(.interp) } :interp :text

    .text : {
        *(.text)
        *(.text.*)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :text

    .data : {
        *(.data)
        *(.data.*)
    } :data

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data
}
