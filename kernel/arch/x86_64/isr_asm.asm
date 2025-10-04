; Interrupt Service Routines (ISR) Assembly Stubs
; TocinOS x86_64 Architecture

[BITS 64]

; External C handlers
extern isr_handler
extern irq_handler

; IDT load function
global idt_load
idt_load:
    lidt [rdi]          ; Load IDT (first argument in RDI)
    ret

; Common ISR stub - saves processor state, calls C handler, restores state
isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Save segment registers
    mov ax, ds
    push rax
    mov ax, es
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Call C handler (stack pointer in RDI)
    mov rdi, rsp
    call isr_handler
    
    ; Restore segment registers
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up pushed error code and ISR number
    add rsp, 16
    
    ; Return from interrupt
    iretq

; Common IRQ stub
irq_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Save segment registers
    mov ax, ds
    push rax
    mov ax, es
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Call C handler
    mov rdi, rsp
    call irq_handler
    
    ; Restore segment registers
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up pushed error code and IRQ number
    add rsp, 16
    
    ; Return from interrupt
    iretq

; Macro to create ISR without error code
%macro ISR_NOERRCODE 1
    global isr%1
    isr%1:
        cli
        push qword 0         ; Push dummy error code
        push qword %1        ; Push interrupt number
        jmp isr_common_stub
%endmacro

; Macro to create ISR with error code
%macro ISR_ERRCODE 1
    global isr%1
    isr%1:
        cli
        push qword %1        ; Push interrupt number (error code already pushed by CPU)
        jmp isr_common_stub
%endmacro

; Macro to create IRQ handler
%macro IRQ 2
    global irq%1
    irq%1:
        cli
        push qword 0         ; Push dummy error code
        push qword %2        ; Push IRQ number (32 + IRQ)
        jmp irq_common_stub
%endmacro

; CPU Exception ISRs (0-31)
ISR_NOERRCODE 0     ; Divide By Zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; Non Maskable Interrupt
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound Range Exceeded
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; Device Not Available
ISR_ERRCODE   8     ; Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun
ISR_ERRCODE   10    ; Invalid TSS
ISR_ERRCODE   11    ; Segment Not Present
ISR_ERRCODE   12    ; Stack Fault
ISR_ERRCODE   13    ; General Protection Fault
ISR_ERRCODE   14    ; Page Fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU Error
ISR_ERRCODE   17    ; Alignment Check
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Floating-Point Exception
ISR_NOERRCODE 20    ; Virtualization Exception
ISR_NOERRCODE 21    ; Control Protection Exception
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Hypervisor Injection Exception
ISR_NOERRCODE 29    ; VMM Communication Exception
ISR_ERRCODE   30    ; Security Exception
ISR_NOERRCODE 31    ; Reserved

; IRQ Handlers (32-47)
IRQ 0,  32          ; Timer
IRQ 1,  33          ; Keyboard
IRQ 2,  34          ; Cascade
IRQ 3,  35          ; COM2
IRQ 4,  36          ; COM1
IRQ 5,  37          ; LPT2
IRQ 6,  38          ; Floppy
IRQ 7,  39          ; LPT1
IRQ 8,  40          ; RTC
IRQ 9,  41          ; Free
IRQ 10, 42          ; Free
IRQ 11, 43          ; Free
IRQ 12, 44          ; Mouse
IRQ 13, 45          ; Coprocessor
IRQ 14, 46          ; ATA1
IRQ 15, 47          ; ATA2

; System call handler (interrupt 0x80)
global isr128
isr128:
    cli
    push qword 0         ; Push dummy error code
    push qword 128       ; Push interrupt number (0x80 = 128)
    jmp isr_common_stub
