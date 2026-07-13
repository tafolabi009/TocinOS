/**
 * ISR Implementation for TocinOS
 * 
 * Handles CPU exceptions and IRQs
 */

#include "../include/kernel/isr.h"
#include "../include/kernel/idt.h"
#include "../include/kernel/kernel.h"

// PIC ports
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

// PIC commands
#define PIC_EOI         0x20

// Array of interrupt handlers
static interrupt_handler_t interrupt_handlers[256];

// Exception messages
static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// Port I/O functions
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Remap the PIC to avoid conflicts with CPU exceptions
 */
static void pic_remap(void) {
    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
    
    // Start initialization sequence
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    
    // Set vector offsets
    outb(PIC1_DATA, IRQ_BASE);        // Master PIC offset
    outb(PIC2_DATA, IRQ_BASE + 8);    // Slave PIC offset
    
    // Tell master PIC there's a slave at IRQ2
    outb(PIC1_DATA, 0x04);
    // Tell slave PIC its cascade identity
    outb(PIC2_DATA, 0x02);
    
    // Set 8086 mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    // Restore masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/**
 * Initialize ISR system
 */
void isr_init(void) {
    // Remap PIC
    pic_remap();
    
    // Install exception handlers (ISR 0-31)
    idt_set_gate(0, (uint32_t)isr0, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(1, (uint32_t)isr1, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(2, (uint32_t)isr2, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(3, (uint32_t)isr3, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(4, (uint32_t)isr4, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(5, (uint32_t)isr5, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(6, (uint32_t)isr6, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(7, (uint32_t)isr7, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(8, (uint32_t)isr8, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(9, (uint32_t)isr9, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(10, (uint32_t)isr10, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(11, (uint32_t)isr11, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(12, (uint32_t)isr12, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(13, (uint32_t)isr13, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(14, (uint32_t)isr14, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(15, (uint32_t)isr15, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(16, (uint32_t)isr16, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(17, (uint32_t)isr17, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(18, (uint32_t)isr18, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(19, (uint32_t)isr19, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(20, (uint32_t)isr20, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(21, (uint32_t)isr21, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(22, (uint32_t)isr22, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(23, (uint32_t)isr23, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(24, (uint32_t)isr24, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(25, (uint32_t)isr25, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(26, (uint32_t)isr26, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(27, (uint32_t)isr27, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(28, (uint32_t)isr28, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(29, (uint32_t)isr29, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(30, (uint32_t)isr30, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(31, (uint32_t)isr31, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    
    // Install IRQ handlers (ISR 32-47)
    idt_set_gate(32, (uint32_t)irq0, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(33, (uint32_t)irq1, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(34, (uint32_t)irq2, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(35, (uint32_t)irq3, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(36, (uint32_t)irq4, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(37, (uint32_t)irq5, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(38, (uint32_t)irq6, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(39, (uint32_t)irq7, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(40, (uint32_t)irq8, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(41, (uint32_t)irq9, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(42, (uint32_t)irq10, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(43, (uint32_t)irq11, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(44, (uint32_t)irq12, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(45, (uint32_t)irq13, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(46, (uint32_t)irq14, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    idt_set_gate(47, (uint32_t)irq15, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT);
    
    // Enable interrupts
    __asm__ volatile ("sti");
}

/**
 * Register a custom interrupt handler
 */
void isr_register_handler(uint8_t n, interrupt_handler_t handler) {
    interrupt_handlers[n] = handler;
}

/**
 * Common ISR handler
 */
void isr_handler(registers_t *regs) {
    extern void serial_printf(const char *fmt, ...);
    
    // Check if we have a custom handler
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    } else {
        // Default exception handler
        serial_printf("\n[EXCEPTION] int=%d ", regs->int_no);
        if (regs->int_no < 32) {
            kernel_print(exception_messages[regs->int_no]);
            serial_printf("%s", exception_messages[regs->int_no]);
        } else {
            kernel_print("Unknown Exception");
        }
        serial_printf("\n[EXCEPTION] EIP=0x%x CS=0x%x ERR=0x%x\n",
                      regs->eip, regs->cs, regs->err_code);
        if (regs->int_no == 14) {
            // Page fault before demand_paging_init() registered the real
            // handler (kernel/mm/demand.c): still dump the fault address.
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            serial_printf("[EXCEPTION] CR2=0x%x (early #PF, no handler armed)\n",
                          cr2);
        }
        kernel_print("\n");
        
        // Halt the system
        __asm__ volatile("cli; hlt");
    }
}

/**
 * Common IRQ handler
 */
void irq_handler(registers_t *regs) {
    // Send EOI to PICs
    if (regs->int_no >= 40) {
        // Send EOI to slave PIC
        outb(PIC2_COMMAND, PIC_EOI);
    }
    // Send EOI to master PIC
    outb(PIC1_COMMAND, PIC_EOI);
    
    // Call custom handler if registered
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
}
