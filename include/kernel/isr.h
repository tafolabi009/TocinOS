/**
 * Interrupt Service Routines (ISR) for TocinOS
 * 
 * Provides CPU exception and IRQ handlers
 */

#ifndef ISR_H
#define ISR_H

#include <stdint.h>

// Registers pushed by interrupt handlers
typedef struct {
    uint32_t ds;                                    // Data segment selector
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
    uint32_t int_no, err_code;                      // Interrupt number and error code
    uint32_t eip, cs, eflags, useresp, ss;          // Pushed by the processor automatically
} __attribute__((packed)) registers_t;

// Interrupt handler callback type
typedef void (*interrupt_handler_t)(registers_t *regs);

// Initialize ISR system
void isr_init(void);

// Register a custom interrupt handler
void isr_register_handler(uint8_t n, interrupt_handler_t handler);

// Exception ISRs (0-31)
extern void isr0(void);   // Divide by zero
extern void isr1(void);   // Debug
extern void isr2(void);   // Non-maskable interrupt
extern void isr3(void);   // Breakpoint
extern void isr4(void);   // Overflow
extern void isr5(void);   // Bound range exceeded
extern void isr6(void);   // Invalid opcode
extern void isr7(void);   // Device not available
extern void isr8(void);   // Double fault
extern void isr9(void);   // Coprocessor segment overrun
extern void isr10(void);  // Invalid TSS
extern void isr11(void);  // Segment not present
extern void isr12(void);  // Stack-segment fault
extern void isr13(void);  // General protection fault
extern void isr14(void);  // Page fault
extern void isr15(void);  // Reserved
extern void isr16(void);  // x87 FPU error
extern void isr17(void);  // Alignment check
extern void isr18(void);  // Machine check
extern void isr19(void);  // SIMD floating-point exception
extern void isr20(void);  // Reserved
extern void isr21(void);  // Reserved
extern void isr22(void);  // Reserved
extern void isr23(void);  // Reserved
extern void isr24(void);  // Reserved
extern void isr25(void);  // Reserved
extern void isr26(void);  // Reserved
extern void isr27(void);  // Reserved
extern void isr28(void);  // Reserved
extern void isr29(void);  // Reserved
extern void isr30(void);  // Reserved
extern void isr31(void);  // Reserved

// IRQ ISRs (32-47)
extern void irq0(void);   // Timer
extern void irq1(void);   // Keyboard
extern void irq2(void);   // Cascade
extern void irq3(void);   // COM2
extern void irq4(void);   // COM1
extern void irq5(void);   // LPT2
extern void irq6(void);   // Floppy
extern void irq7(void);   // LPT1
extern void irq8(void);   // RTC
extern void irq9(void);   // Free
extern void irq10(void);  // Free
extern void irq11(void);  // Free
extern void irq12(void);  // Mouse
extern void irq13(void);  // Coprocessor
extern void irq14(void);  // ATA1
extern void irq15(void);  // ATA2

#endif // ISR_H
