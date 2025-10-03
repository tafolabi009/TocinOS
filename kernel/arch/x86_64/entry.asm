; TocinOS 64-bit Kernel Entry Point

[BITS 64]
[EXTERN kernel_main]
[GLOBAL _start]

section .text
_start:
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
