; TocinOS 32-bit Kernel Entry Point

[BITS 32]
[EXTERN kernel_main]
[GLOBAL _start]

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

section .text
_start:
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
