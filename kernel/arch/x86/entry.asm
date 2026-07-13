; TocinOS 32-bit Kernel Entry Point

[BITS 32]
[EXTERN kernel_main]
[GLOBAL _start]
[GLOBAL tocinboot_magic_reg]
[GLOBAL tocinboot_info_ptr]

; Multiboot header constants
MBALIGN  equ  1 << 0            ; align modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
FLAGS    equ  MBALIGN | MEMINFO ; Multiboot flags
MAGIC    equ  0x1BADB002        ; Multiboot magic number
CHECKSUM equ -(MAGIC + FLAGS)   ; checksum must equal 0

; Multiboot header (must be in first 8KB of kernel)
section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

; TocinBoot handoff registers (docs/BOOT_PROTOCOL.md §6.1), captured at entry
; before anything can clobber them. On legacy paths (BIOS stage2, multiboot
; QEMU -kernel) these hold whatever the loader left; kernel/bootinfo.c
; validates both magics before trusting them.
section .data
align 4
tocinboot_magic_reg: dd 0       ; EAX at entry (expect 0x70C1B007)
tocinboot_info_ptr:  dd 0       ; EBX at entry (phys addr of tocinboot_info)

section .text
_start:
    ; Capture the TocinBoot register contract FIRST (EAX=magic, EBX=info ptr)
    mov [tocinboot_magic_reg], eax
    mov [tocinboot_info_ptr], ebx

    ; Initialize stack
    mov esp, kernel_stack_top

    ; Call the C kernel main function
    call kernel_main
    
    ; Halt if kernel returns
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
kernel_stack_bottom:
    resb 16384          ; 16KB stack
kernel_stack_top:
