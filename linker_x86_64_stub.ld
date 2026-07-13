/* TocinOS M2 64-bit stub kernel linker script (root target kernel64-stub).
 *
 * Standalone ELF64 ET_EXEC proof binary for the TocinBoot §6.2 long-mode
 * handoff. Linked and loaded at 2 MiB physical: TocinBoot loads every
 * PT_LOAD at its exact p_paddr (docs/BOOT_PROTOCOL.md §7), and 2 MiB
 * avoids both firmware-owned low memory and the 32-bit kernel's 1 MiB
 * link address. The full M2 kernel keeps linker_x86_64.ld.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x200000;  /* 2 MiB physical */

    .text : ALIGN(4K)
    {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
    }
}
