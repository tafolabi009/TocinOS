/**
 * TocinOS GDT (Global Descriptor Table) Management
 * 
 * Provides complete GDT setup including user mode segments and TSS
 */

#ifndef GDT_H
#define GDT_H

#include "../stdint.h"

// GDT entry structure (8 bytes per entry)
typedef struct {
    uint16_t limit_low;     // Lower 16 bits of limit
    uint16_t base_low;      // Lower 16 bits of base
    uint8_t  base_middle;   // Next 8 bits of base
    uint8_t  access;        // Access byte
    uint8_t  granularity;   // Granularity and upper 4 bits of limit
    uint8_t  base_high;     // Upper 8 bits of base
} __attribute__((packed)) gdt_entry_t;

// GDT pointer structure (for lgdt instruction)
typedef struct {
    uint16_t limit;         // Size of GDT - 1
    uint32_t base;          // Address of GDT
} __attribute__((packed)) gdt_ptr_t;

// GDT segment selectors
#define GDT_NULL_SEG        0x00
#define GDT_KERNEL_CODE_SEL 0x08
#define GDT_KERNEL_DATA_SEL 0x10
#define GDT_USER_CODE_SEL   0x18
#define GDT_USER_DATA_SEL   0x20
#define GDT_TSS_SEL         0x28

// Selector with Ring 3 privilege
#define GDT_USER_CODE_SEL_RPL3  (GDT_USER_CODE_SEL | 3)  // 0x1B
#define GDT_USER_DATA_SEL_RPL3  (GDT_USER_DATA_SEL | 3)  // 0x23

// GDT access byte flags
#define GDT_ACCESS_PRESENT      0x80    // Segment is present
#define GDT_ACCESS_DPL0         0x00    // Ring 0
#define GDT_ACCESS_DPL1         0x20    // Ring 1
#define GDT_ACCESS_DPL2         0x40    // Ring 2
#define GDT_ACCESS_DPL3         0x60    // Ring 3
#define GDT_ACCESS_DESCRIPTOR   0x10    // 1 = code/data, 0 = system
#define GDT_ACCESS_EXECUTABLE   0x08    // Executable (code) segment
#define GDT_ACCESS_DIRECTION    0x04    // Direction/Conforming bit
#define GDT_ACCESS_RW           0x02    // Read/Write
#define GDT_ACCESS_ACCESSED     0x01    // Accessed

// GDT granularity flags
#define GDT_GRAN_4K             0x80    // 4KB granularity
#define GDT_GRAN_32BIT          0x40    // 32-bit protected mode
#define GDT_GRAN_64BIT          0x20    // 64-bit mode (for x86_64)

// TSS access byte
#define GDT_ACCESS_TSS_32       0x89    // 32-bit TSS (available)
#define GDT_ACCESS_TSS_32_BUSY  0x8B    // 32-bit TSS (busy)

// GDT API
void gdt_init(void);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void gdt_set_tss(int num, uint32_t base, uint32_t limit);
void gdt_load(void);
void gdt_update_tss(uint32_t tss_addr, uint32_t tss_size);

#endif // GDT_H
