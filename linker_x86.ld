/* TocinOS x86 32-bit Linker Script */

OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;  /* Kernel loads at 1MB for multiboot */

    /* Multiboot header MUST be in first 8KB */
    .multiboot : ALIGN(4)
    {
        *(.multiboot)
    }

    .text : ALIGN(4K)
    {
        *(.text)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /* End of the kernel image in memory (text+rodata+data+bss, including
     * the 16KB boot stack in entry.asm's BSS). kernel_main derives the
     * PMM low-memory reservation from this symbol (roadmap bug #8) so the
     * reserve tracks the real footprint instead of a hardcoded guess. */
    _kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
    }
}
