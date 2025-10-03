; TocinOS 32-bit Kernel Entry Point

[BITS 32]
[EXTERN kernel_main]
[GLOBAL _start]

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
