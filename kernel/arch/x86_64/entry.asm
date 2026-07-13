; TocinOS 64-bit Kernel Entry Point

[BITS 64]
[EXTERN kernel_main]
[GLOBAL _start]
[GLOBAL tocinboot_magic_reg]
[GLOBAL tocinboot_info_ptr]

; TocinBoot handoff registers (docs/BOOT_PROTOCOL.md §6.2): RAX=magic,
; RDI=phys addr of tocinboot_info (guaranteed < 4 GiB, low halves suffice).
; kernel/bootinfo.c validates both magics before trusting them.
section .data
align 4
tocinboot_magic_reg: dd 0
tocinboot_info_ptr:  dd 0

section .text
_start:
    ; Capture the TocinBoot register contract FIRST
    mov [tocinboot_magic_reg], eax
    mov [tocinboot_info_ptr], edi

    ; Initialize stack
    mov rsp, kernel_stack_top
    
    ; Clear the direction flag
    cld
    
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
    resb 32768          ; 32KB stack
kernel_stack_top:
