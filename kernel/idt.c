/**
 * IDT Implementation for TocinOS
 * 
 * Sets up the Interrupt Descriptor Table
 */

#include "../include/kernel/idt.h"
#include "../include/kernel/kernel.h"

// IDT entries array
static idt_entry_t idt[IDT_ENTRIES];

// IDT pointer
static idt_ptr_t idtp;

// External assembly function to load IDT
extern void idt_load(uint32_t);

/**
 * Set an entry in the IDT
 */
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = flags;
}

/**
 * Initialize the IDT
 */
void idt_init(void) {
    // Set up IDT pointer
    idtp.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idtp.base = (uint32_t)&idt;
    
    // Clear IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    // Load IDT
    idt_load((uint32_t)&idtp);
}
