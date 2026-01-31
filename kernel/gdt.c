/**
 * TocinOS GDT (Global Descriptor Table) Implementation
 * 
 * Sets up the GDT with kernel, user mode segments and TSS
 */

#include "../include/kernel/gdt.h"
#include "../include/kernel/usermode.h"

// GDT entries: NULL, Kernel Code, Kernel Data, User Code, User Data, TSS
#define GDT_ENTRIES 6
static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t gdt_pointer;

// External TSS from usermode.c
extern tss_entry_t *tss_get(void);

/**
 * Set a GDT entry
 */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (num < 0 || num >= GDT_ENTRIES) {
        return;
    }
    
    // Set base address
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    // Set limit
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    
    // Set granularity and access
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

/**
 * Set TSS descriptor in GDT
 */
void gdt_set_tss(int num, uint32_t base, uint32_t limit) {
    // TSS descriptor has special format
    // Access: Present=1, DPL=0, Type=0x09 (32-bit TSS available)
    // = 0x89
    gdt_set_gate(num, base, limit, GDT_ACCESS_TSS_32, 0x00);
}

/**
 * Load the GDT and update segment registers
 */
void gdt_load(void) {
    // Load GDT using lgdt instruction
    __asm__ volatile(
        "lgdt %0\n"
        // Reload segment registers with kernel segments
        "mov $0x10, %%ax\n"     // Kernel data segment
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        // Far jump to reload CS with kernel code segment
        "ljmp $0x08, $1f\n"
        "1:\n"
        :
        : "m"(gdt_pointer)
        : "eax", "memory"
    );
}

/**
 * Initialize the GDT
 */
void gdt_init(void) {
    // Setup GDT pointer
    gdt_pointer.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_pointer.base = (uint32_t)&gdt;
    
    // Clear all entries first
    for (int i = 0; i < GDT_ENTRIES; i++) {
        gdt_set_gate(i, 0, 0, 0, 0);
    }
    
    // Entry 0: NULL descriptor (required by x86)
    gdt_set_gate(0, 0, 0, 0, 0);
    
    // Entry 1 (0x08): Kernel Code Segment
    // Base = 0, Limit = 4GB, DPL = 0
    // Access: Present, Ring 0, Code/Data, Executable, Readable
    gdt_set_gate(1, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    // Entry 2 (0x10): Kernel Data Segment
    // Base = 0, Limit = 4GB, DPL = 0
    // Access: Present, Ring 0, Code/Data, Writable
    gdt_set_gate(2, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_RW,
        GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    // Entry 3 (0x18): User Code Segment
    // Base = 0, Limit = 4GB, DPL = 3
    // Access: Present, Ring 3, Code/Data, Executable, Readable
    gdt_set_gate(3, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    // Entry 4 (0x20): User Data Segment
    // Base = 0, Limit = 4GB, DPL = 3
    // Access: Present, Ring 3, Code/Data, Writable
    gdt_set_gate(4, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_RW,
        GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    // Entry 5 (0x28): TSS (Task State Segment)
    // Will be filled in by tss_init() after TSS is setup
    // For now, set a placeholder
    tss_entry_t *tss = tss_get();
    if (tss) {
        gdt_set_tss(5, (uint32_t)tss, sizeof(tss_entry_t) - 1);
    }
    
    // Load the new GDT
    gdt_load();
}

/**
 * Update TSS entry in GDT (called after TSS is initialized)
 */
void gdt_update_tss(uint32_t tss_addr, uint32_t tss_size) {
    gdt_set_tss(5, tss_addr, tss_size);
    
    // Reload GDT (but don't need to reload segments)
    __asm__ volatile("lgdt %0" : : "m"(gdt_pointer) : "memory");
}
