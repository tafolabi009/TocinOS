/**
 * Interrupt Descriptor Table (IDT) for TocinOS
 * 
 * Provides interrupt handling infrastructure for x86/x86-64
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT entry structure
typedef struct {
    uint16_t offset_low;    // Lower 16 bits of handler address
    uint16_t selector;      // Kernel segment selector
    uint8_t  zero;          // Always 0
    uint8_t  type_attr;     // Type and attributes
    uint16_t offset_high;   // Upper 16 bits of handler address
} __attribute__((packed)) idt_entry_t;

// IDT pointer structure
typedef struct {
    uint16_t limit;         // Size of IDT - 1
    uint32_t base;          // Address of IDT
} __attribute__((packed)) idt_ptr_t;

// Number of IDT entries (256 for x86)
#define IDT_ENTRIES 256

// IDT gate types
#define IDT_TYPE_TASK       0x05
#define IDT_TYPE_INTERRUPT  0x0E
#define IDT_TYPE_TRAP       0x0F

// IDT attributes
#define IDT_ATTR_PRESENT    0x80
#define IDT_ATTR_DPL0       0x00
#define IDT_ATTR_DPL3       0x60

// Exception numbers
#define EXC_DIVIDE_ERROR    0
#define EXC_DEBUG           1
#define EXC_NMI             2
#define EXC_BREAKPOINT      3
#define EXC_OVERFLOW        4
#define EXC_BOUND_RANGE     5
#define EXC_INVALID_OPCODE  6
#define EXC_DEVICE_NA       7
#define EXC_DOUBLE_FAULT    8
#define EXC_COPROC_SEG      9
#define EXC_INVALID_TSS     10
#define EXC_SEGMENT_NP      11
#define EXC_STACK_FAULT     12
#define EXC_GENERAL_PROT    13
#define EXC_PAGE_FAULT      14
#define EXC_FPU_ERROR       16
#define EXC_ALIGN_CHECK     17
#define EXC_MACHINE_CHECK   18
#define EXC_SIMD_FP         19

// IRQ base (mapped to interrupt 32+)
#define IRQ_BASE            32
#define IRQ_TIMER           0
#define IRQ_KEYBOARD        1
#define IRQ_CASCADE         2
#define IRQ_COM2            3
#define IRQ_COM1            4
#define IRQ_LPT2            5
#define IRQ_FLOPPY          6
#define IRQ_LPT1            7
#define IRQ_RTC             8
#define IRQ_FREE1           9
#define IRQ_FREE2           10
#define IRQ_FREE3           11
#define IRQ_MOUSE           12
#define IRQ_COPROC          13
#define IRQ_ATA1            14
#define IRQ_ATA2            15

// Function prototypes
void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags);

// Interrupt handler type
typedef void (*isr_t)(void);

#endif // IDT_H
