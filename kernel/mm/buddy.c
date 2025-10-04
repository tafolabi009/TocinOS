/**
 * TocinOS Buddy Allocator Implementation
 * 
 * Efficient memory allocation using the buddy system algorithm.
 * Supports allocations in powers of 2 from 4KB to 8MB.
 */

#include "../include/kernel/memory.h"
#include "../include/kernel/kernel.h"

#define BUDDY_START_ADDR 0x400000  // 4MB
#define BUDDY_END_ADDR   0x8000000 // 128MB
#define BUDDY_TOTAL_PAGES ((BUDDY_END_ADDR - BUDDY_START_ADDR) / PAGE_SIZE)

static buddy_allocator_t buddy_allocator;
static buddy_page_t page_descriptors[BUDDY_TOTAL_PAGES];
static int buddy_initialized = 0;

/**
 * Helper: Get page descriptor for physical address
 */
static buddy_page_t* addr_to_page(void *addr) {
    uint32_t phys = (uint32_t)addr;
    if (phys < BUDDY_START_ADDR || phys >= BUDDY_END_ADDR) {
        return 0;
    }
    uint32_t index = (phys - BUDDY_START_ADDR) / PAGE_SIZE;
    return &page_descriptors[index];
}

/**
 * Helper: Get physical address from page descriptor
 */
static void* page_to_addr(buddy_page_t *page) {
    uint32_t index = page - page_descriptors;
    return (void *)(BUDDY_START_ADDR + (index * PAGE_SIZE));
}

/**
 * Helper: Get buddy address for a given address and order
 */
static void* get_buddy(void *addr, uint32_t order) {
    uint32_t phys = (uint32_t)addr;
    uint32_t block_size = PAGE_SIZE << order;
    return (void *)(phys ^ block_size);
}

/**
 * Helper: Remove page from free list
 */
static void remove_from_free_list(buddy_page_t *page, uint32_t order) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        buddy_allocator.free_lists[order] = page->next;
    }
    
    if (page->next) {
        page->next->prev = page->prev;
    }
    
    page->next = 0;
    page->prev = 0;
    buddy_allocator.free_pages[order]--;
}

/**
 * Helper: Add page to free list
 */
static void add_to_free_list(buddy_page_t *page, uint32_t order) {
    page->order = order;
    page->next = buddy_allocator.free_lists[order];
    page->prev = 0;
    
    if (buddy_allocator.free_lists[order]) {
        buddy_allocator.free_lists[order]->prev = page;
    }
    
    buddy_allocator.free_lists[order] = page;
    buddy_allocator.free_pages[order]++;
}

/**
 * Initialize buddy allocator
 */
void buddy_init(void) {
    if (buddy_initialized) {
        return;
    }
    
    // Initialize free lists
    for (uint32_t i = 0; i < MAX_ORDER; i++) {
        buddy_allocator.free_lists[i] = 0;
        buddy_allocator.free_pages[i] = 0;
    }
    
    buddy_allocator.total_free = 0;
    buddy_allocator.total_used = 0;
    
    // Initialize page descriptors
    for (uint32_t i = 0; i < BUDDY_TOTAL_PAGES; i++) {
        page_descriptors[i].next = 0;
        page_descriptors[i].prev = 0;
        page_descriptors[i].order = 0;
        page_descriptors[i].flags = 0;
        page_descriptors[i].ref_count = 0;
    }
    
    // Add all memory to free lists at maximum order
    uint32_t max_block_size = PAGE_SIZE << (MAX_ORDER - 1);
    uint32_t num_max_blocks = (BUDDY_END_ADDR - BUDDY_START_ADDR) / max_block_size;
    
    for (uint32_t i = 0; i < num_max_blocks; i++) {
        void *addr = (void *)(BUDDY_START_ADDR + (i * max_block_size));
        buddy_page_t *page = addr_to_page(addr);
        if (page) {
            add_to_free_list(page, MAX_ORDER - 1);
            buddy_allocator.total_free += (1 << (MAX_ORDER - 1));
        }
    }
    
    buddy_initialized = 1;
    kernel_print("[BUDDY] Buddy allocator initialized (4MB-128MB)\n");
}

/**
 * Allocate memory block of given order
 */
void* buddy_alloc(uint32_t order) {
    if (!buddy_initialized || order >= MAX_ORDER) {
        return 0;
    }
    
    // Find the smallest available block >= requested order
    uint32_t current_order = order;
    while (current_order < MAX_ORDER && !buddy_allocator.free_lists[current_order]) {
        current_order++;
    }
    
    if (current_order >= MAX_ORDER) {
        return 0;  // Out of memory
    }
    
    // Get block from free list
    buddy_page_t *page = buddy_allocator.free_lists[current_order];
    remove_from_free_list(page, current_order);
    
    // Split block if necessary
    while (current_order > order) {
        current_order--;
        
        // Get buddy address
        void *addr = page_to_addr(page);
        void *buddy_addr = get_buddy(addr, current_order);
        buddy_page_t *buddy_page = addr_to_page(buddy_addr);
        
        // Add buddy to free list
        if (buddy_page) {
            add_to_free_list(buddy_page, current_order);
        }
    }
    
    page->order = order;
    page->ref_count = 1;
    page->flags = 0;
    
    buddy_allocator.total_free -= (1 << order);
    buddy_allocator.total_used += (1 << order);
    
    return page_to_addr(page);
}

/**
 * Free memory block of given order
 */
void buddy_free(void *addr, uint32_t order) {
    if (!buddy_initialized || !addr || order >= MAX_ORDER) {
        return;
    }
    
    buddy_page_t *page = addr_to_page(addr);
    if (!page) {
        return;
    }
    
    // Decrement reference count
    if (page->ref_count > 0) {
        page->ref_count--;
    }
    
    if (page->ref_count > 0) {
        return;  // Still referenced
    }
    
    buddy_allocator.total_free += (1 << order);
    buddy_allocator.total_used -= (1 << order);
    
    // Try to coalesce with buddy
    while (order < MAX_ORDER - 1) {
        void *buddy_addr = get_buddy(addr, order);
        buddy_page_t *buddy_page = addr_to_page(buddy_addr);
        
        if (!buddy_page || buddy_page->order != order || buddy_page->ref_count != 0) {
            break;  // Cannot coalesce
        }
        
        // Check if buddy is in free list
        int found = 0;
        buddy_page_t *curr = buddy_allocator.free_lists[order];
        while (curr) {
            if (curr == buddy_page) {
                found = 1;
                break;
            }
            curr = curr->next;
        }
        
        if (!found) {
            break;
        }
        
        // Remove buddy from free list
        remove_from_free_list(buddy_page, order);
        
        // Merge with buddy
        if ((uint32_t)addr > (uint32_t)buddy_addr) {
            addr = buddy_addr;
            page = buddy_page;
        }
        
        order++;
    }
    
    // Add merged block to free list
    page->order = order;
    add_to_free_list(page, order);
}

/**
 * Allocate multiple contiguous pages
 */
void* buddy_alloc_pages(uint32_t num_pages) {
    if (num_pages == 0) {
        return 0;
    }
    
    uint32_t order = buddy_get_order(num_pages * PAGE_SIZE);
    return buddy_alloc(order);
}

/**
 * Free multiple contiguous pages
 */
void buddy_free_pages(void *addr, uint32_t num_pages) {
    if (!addr || num_pages == 0) {
        return;
    }
    
    uint32_t order = buddy_get_order(num_pages * PAGE_SIZE);
    buddy_free(addr, order);
}

/**
 * Get order for given size
 */
uint32_t buddy_get_order(uint32_t size) {
    uint32_t order = 0;
    uint32_t block_size = PAGE_SIZE;
    
    while (block_size < size && order < MAX_ORDER - 1) {
        block_size <<= 1;
        order++;
    }
    
    return order;
}
