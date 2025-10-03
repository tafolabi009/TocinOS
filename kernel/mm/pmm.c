/**
 * TocinOS Physical Memory Manager (PMM)
 * 
 * Manages physical memory allocation using a bitmap allocator
 */

#include "../include/kernel/memory.h"

#define MEMORY_SIZE (128 * 1024 * 1024)  // 128MB
#define PAGE_SIZE 4096
#define TOTAL_PAGES (MEMORY_SIZE / PAGE_SIZE)
#define BITMAP_SIZE (TOTAL_PAGES / 8)

static unsigned char memory_bitmap[BITMAP_SIZE];
static unsigned int total_pages;
static unsigned int used_pages;

/**
 * Initialize the physical memory manager
 */
void pmm_init(void) {
    total_pages = TOTAL_PAGES;
    used_pages = 0;
    
    // Mark all pages as free
    for (unsigned int i = 0; i < BITMAP_SIZE; i++) {
        memory_bitmap[i] = 0;
    }
    
    // Mark first 1MB as used (for BIOS, bootloader, kernel)
    for (unsigned int i = 0; i < (1024 * 1024 / PAGE_SIZE); i++) {
        pmm_set_page_used(i);
    }
}

/**
 * Allocate a physical page
 */
unsigned int pmm_alloc_page(void) {
    for (unsigned int i = 0; i < total_pages; i++) {
        unsigned int byte = i / 8;
        unsigned int bit = i % 8;
        
        if (!(memory_bitmap[byte] & (1 << bit))) {
            memory_bitmap[byte] |= (1 << bit);
            used_pages++;
            return i * PAGE_SIZE;
        }
    }
    return 0; // Out of memory
}

/**
 * Free a physical page
 */
void pmm_free_page(unsigned int address) {
    unsigned int page = address / PAGE_SIZE;
    unsigned int byte = page / 8;
    unsigned int bit = page % 8;
    
    if (memory_bitmap[byte] & (1 << bit)) {
        memory_bitmap[byte] &= ~(1 << bit);
        used_pages--;
    }
}

/**
 * Mark a page as used
 */
void pmm_set_page_used(unsigned int page) {
    unsigned int byte = page / 8;
    unsigned int bit = page % 8;
    memory_bitmap[byte] |= (1 << bit);
    used_pages++;
}

/**
 * Get total available pages
 */
unsigned int pmm_get_total_pages(void) {
    return total_pages;
}

/**
 * Get used pages count
 */
unsigned int pmm_get_used_pages(void) {
    return used_pages;
}

/**
 * Get free pages count
 */
unsigned int pmm_get_free_pages(void) {
    return total_pages - used_pages;
}
