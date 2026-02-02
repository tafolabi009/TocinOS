/**
 * TocinOS Virtual Memory Manager (VMM)
 * 
 * Manages virtual memory with paging support
 */

#include "../include/kernel/memory.h"

#define PAGE_PRESENT    0x1
#define PAGE_WRITE      0x2
#define PAGE_USER       0x4
#define PAGE_SIZE       4096
#define TABLES_PER_DIR  1024
#define PAGES_PER_TABLE 1024

typedef struct {
    unsigned int entries[PAGES_PER_TABLE];
} page_table_t;

typedef struct {
    unsigned int entries[TABLES_PER_DIR];
} page_directory_t;

static page_directory_t *kernel_directory = (page_directory_t *)0x9C000;
static unsigned int current_directory = 0x9C000;

/**
 * Initialize the virtual memory manager
 */
void vmm_init(void) {
    // Clear page directory
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        kernel_directory->entries[i] = 0;
    }
    
    // Identity map first 4MB (for kernel)
    page_table_t *first_table = (page_table_t *)0x9D000;
    
    for (int i = 0; i < PAGES_PER_TABLE; i++) {
        first_table->entries[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }
    
    kernel_directory->entries[0] = ((unsigned int)first_table) | PAGE_PRESENT | PAGE_WRITE;
    
    // Load page directory
    vmm_switch_directory((unsigned int)kernel_directory);
}

/**
 * Map a virtual address to a physical address
 */
void vmm_map_page(unsigned int virtual_addr, unsigned int physical_addr, unsigned int flags) {
    unsigned int dir_index = virtual_addr >> 22;
    unsigned int table_index = (virtual_addr >> 12) & 0x3FF;
    
    // Get or create page table
    if (!(kernel_directory->entries[dir_index] & PAGE_PRESENT)) {
        unsigned int table_phys = pmm_alloc_page();
        kernel_directory->entries[dir_index] = table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        
        page_table_t *table = (page_table_t *)(table_phys);
        for (int i = 0; i < PAGES_PER_TABLE; i++) {
            table->entries[i] = 0;
        }
    }
    
    page_table_t *table = (page_table_t *)(kernel_directory->entries[dir_index] & ~0xFFF);
    table->entries[table_index] = physical_addr | PAGE_PRESENT | flags;
}

/**
 * Unmap a virtual address
 */
void vmm_unmap_page(unsigned int virtual_addr) {
    unsigned int dir_index = virtual_addr >> 22;
    unsigned int table_index = (virtual_addr >> 12) & 0x3FF;
    
    if (kernel_directory->entries[dir_index] & PAGE_PRESENT) {
        page_table_t *table = (page_table_t *)(kernel_directory->entries[dir_index] & ~0xFFF);
        table->entries[table_index] = 0;
    }
}

/**
 * Switch page directory
 */
void vmm_switch_directory(unsigned int directory_phys) {
    current_directory = directory_phys;
    
    #ifdef __i386__
    __asm__ volatile(
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(directory_phys) : "eax"
    );
    #endif
}

/**
 * Get current page directory
 */
unsigned int vmm_get_current_directory(void) {
    return current_directory;
}

/**
 * Get physical address for a virtual address
 */
unsigned int vmm_get_physical(unsigned int virtual_addr) {
    unsigned int dir_index = virtual_addr >> 22;
    unsigned int table_index = (virtual_addr >> 12) & 0x3FF;
    
    if (!(kernel_directory->entries[dir_index] & PAGE_PRESENT)) {
        return 0;  // Page table not present
    }
    
    page_table_t *table = (page_table_t *)(kernel_directory->entries[dir_index] & ~0xFFF);
    if (!(table->entries[table_index] & PAGE_PRESENT)) {
        return 0;  // Page not present
    }
    
    return table->entries[table_index] & ~0xFFF;
}

// Alias for compatibility
uint32_t vmm_virt_to_phys(uint32_t virt_addr) {
    return vmm_get_physical(virt_addr);
}

/* Stub functions for incomplete features */
int vmm_mark_cow(unsigned int vaddr) {
    (void)vaddr;
    return 0; // TODO: Mark page as copy-on-write
}

int vmm_set_page_flags(unsigned int vaddr, unsigned int flags) {
    (void)vaddr;
    (void)flags;
    return 0; // TODO: Set page flags
}
