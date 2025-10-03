; Interrupt Service Routines (ISR) Assembly Stubs
; TocinOS x86 Architecture

[BITS 32]

; External C handlers
extern isr_handler
extern irq_handler

; IDT load function
global idt_load
idt_load:
    mov eax, [esp+4]    ; Get IDT pointer address
    lidt [eax]          ; Load IDT
    ret

; Common ISR stub - saves processor state, calls C handler, restores state
isr_common_stub:
    pusha               ; Push all general purpose registers
    
    mov ax, ds          ; Save data segment
    push eax
    
    mov ax, 0x10        ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    call isr_handler    ; Call C handler
    
    pop eax             ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa                ; Restore general purpose registers
    add esp, 8          ; Clean up pushed error code and ISR number
    sti                 ; Re-enable interrupts
    iret                ; Return from interrupt

; Common IRQ stub
irq_common_stub:
    pusha
    
    mov ax, ds
    push eax
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    call irq_handler
    
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa
    add esp, 8
    sti
    iret

; Macro to create ISR without error code
%macro ISR_NOERRCODE 1
    global isr%1
    isr%1:
        cli
        push byte 0         ; Push dummy error code
        push byte %1        ; Push interrupt number
        jmp isr_common_stub
%endmacro

; Macro to create ISR with error code
%macro ISR_ERRCODE 1
    global isr%1
    isr%1:
        cli
        push byte %1        ; Push interrupt number
        jmp isr_common_stub
%endmacro

; Macro to create IRQ handler
%macro IRQ 2
    global irq%1
    irq%1:
        cli
        push byte 0         ; Push dummy error code
        push byte %2        ; Push IRQ number (32 + IRQ)
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
ISR_NOERRCODE 20    ; Reserved
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
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
