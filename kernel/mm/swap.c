/**
 * TocinOS Swap Manager
 * 
 * Provides swap space management for moving pages to/from disk.
 * Supports both file-backed and device-backed swap areas.
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/memory.h"
#include "../../include/kernel/kernel.h"

extern void serial_printf(const char *fmt, ...);

/* NULL definition */
#ifndef NULL
#define NULL ((void *)0)
#endif

// ==================== SWAP DATA STRUCTURES ====================

/* Swap area descriptor */
#define MAX_SWAP_AREAS 4
static swap_area_t swap_areas[MAX_SWAP_AREAS];
static int num_swap_areas = 0;

/* Primary swap area bitmap (embedded for bootstrap) */
#define EMBEDDED_SWAP_PAGES 4096  /* 16MB embedded swap capacity */
static uint32_t embedded_swap_bitmap[EMBEDDED_SWAP_PAGES / 32];

/* Swap statistics */
static struct {
    uint32_t pages_swapped_out;
    uint32_t pages_swapped_in;
    uint32_t swap_hits;
    uint32_t swap_misses;
} swap_stats = {0};

/* Swap initialized flag */
static int swap_initialized = 0;

/* Simulated swap storage (for testing without real disk) */
#define SWAP_STORAGE_SIZE (256 * PAGE_SIZE)  /* 1MB simulated swap */
static uint8_t swap_storage[SWAP_STORAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

// ==================== BITMAP HELPERS ====================

/**
 * Set a bit in bitmap
 */
static void bitmap_set(uint32_t *bitmap, uint32_t bit) {
    bitmap[bit / 32] |= (1U << (bit % 32));
}

/**
 * Clear a bit in bitmap
 */
static void bitmap_clear(uint32_t *bitmap, uint32_t bit) {
    bitmap[bit / 32] &= ~(1U << (bit % 32));
}

/**
 * Test a bit in bitmap
 */
static int bitmap_test(uint32_t *bitmap, uint32_t bit) {
    return (bitmap[bit / 32] & (1U << (bit % 32))) != 0;
}

/**
 * Find first clear bit in bitmap
 */
static int bitmap_find_free(uint32_t *bitmap, uint32_t max_bits) {
    for (uint32_t i = 0; i < max_bits; i++) {
        if (!bitmap_test(bitmap, i)) {
            return i;
        }
    }
    return -1;
}

// ==================== SWAP SLOT MANAGEMENT ====================

/**
 * Allocate a swap slot
 */
uint32_t swap_alloc_slot(void) {
    if (!swap_initialized) {
        serial_printf("[SWAP] Not initialized!\n");
        return (uint32_t)-1;
    }
    
    /* Try each swap area */
    for (int i = 0; i < num_swap_areas; i++) {
        swap_area_t *sa = &swap_areas[i];
        
        if (sa->free_pages == 0) continue;
        
        int slot = bitmap_find_free(sa->bitmap, sa->total_pages);
        if (slot >= 0) {
            bitmap_set(sa->bitmap, slot);
            sa->free_pages--;
            
            /* Encode swap area index in upper bits */
            return (i << 24) | slot;
        }
    }
    
    serial_printf("[SWAP] Out of swap space!\n");
    return (uint32_t)-1;
}

/**
 * Free a swap slot
 */
void swap_free_slot(uint32_t slot) {
    if (slot == (uint32_t)-1) return;
    
    int area_idx = (slot >> 24) & 0xFF;
    int page_slot = slot & 0x00FFFFFF;
    
    if (area_idx >= num_swap_areas) {
        serial_printf("[SWAP] Invalid swap area index: %d\n", area_idx);
        return;
    }
    
    swap_area_t *sa = &swap_areas[area_idx];
    
    if ((uint32_t)page_slot >= sa->total_pages) {
        serial_printf("[SWAP] Invalid swap slot: %d\n", page_slot);
        return;
    }
    
    bitmap_clear(sa->bitmap, page_slot);
    sa->free_pages++;
}

// ==================== SWAP I/O ====================

/**
 * Write a page to swap space
 */
static int swap_write_page(uint32_t slot, uint32_t phys_addr) {
    int area_idx = (slot >> 24) & 0xFF;
    int page_slot = slot & 0x00FFFFFF;
    
    if (area_idx >= num_swap_areas) return -1;
    
    swap_area_t *sa = &swap_areas[area_idx];
    
    /* For simulated swap, just copy to storage */
    if (sa->device == -1) {
        uint32_t offset = page_slot * PAGE_SIZE;
        if (offset + PAGE_SIZE <= SWAP_STORAGE_SIZE) {
            /* Copy page to swap storage */
            uint8_t *src = (uint8_t *)phys_addr;
            uint8_t *dst = &swap_storage[offset];
            for (int i = 0; i < PAGE_SIZE; i++) {
                dst[i] = src[i];
            }
            return 0;
        }
        return -1;
    }
    
    /* TODO: Implement real disk I/O */
    /* For now, just report success */
    serial_printf("[SWAP] Would write page to disk: slot=%d phys=0x%x\n",
                  page_slot, phys_addr);
    
    return 0;
}

/**
 * Read a page from swap space
 */
static int swap_read_page(uint32_t slot, uint32_t phys_addr) {
    int area_idx = (slot >> 24) & 0xFF;
    int page_slot = slot & 0x00FFFFFF;
    
    if (area_idx >= num_swap_areas) return -1;
    
    swap_area_t *sa = &swap_areas[area_idx];
    
    /* For simulated swap, copy from storage */
    if (sa->device == -1) {
        uint32_t offset = page_slot * PAGE_SIZE;
        if (offset + PAGE_SIZE <= SWAP_STORAGE_SIZE) {
            /* Copy from swap storage */
            uint8_t *src = &swap_storage[offset];
            uint8_t *dst = (uint8_t *)phys_addr;
            for (int i = 0; i < PAGE_SIZE; i++) {
                dst[i] = src[i];
            }
            return 0;
        }
        return -1;
    }
    
    /* TODO: Implement real disk I/O */
    serial_printf("[SWAP] Would read page from disk: slot=%d phys=0x%x\n",
                  page_slot, phys_addr);
    
    return 0;
}

// ==================== SWAP API ====================

/**
 * Initialize swap subsystem
 */
void swap_init(void) {
    if (swap_initialized) return;
    
    /* Clear swap areas */
    for (int i = 0; i < MAX_SWAP_AREAS; i++) {
        swap_areas[i].start_block = 0;
        swap_areas[i].total_pages = 0;
        swap_areas[i].free_pages = 0;
        swap_areas[i].bitmap = NULL;
        swap_areas[i].device = 0;
        swap_areas[i].flags = 0;
    }
    num_swap_areas = 0;
    
    /* Clear embedded bitmap */
    for (int i = 0; i < EMBEDDED_SWAP_PAGES / 32; i++) {
        embedded_swap_bitmap[i] = 0;
    }
    
    /* Add embedded swap area (simulated) */
    swap_areas[0].start_block = 0;
    swap_areas[0].total_pages = SWAP_STORAGE_SIZE / PAGE_SIZE;  /* 256 pages */
    swap_areas[0].free_pages = swap_areas[0].total_pages;
    swap_areas[0].bitmap = embedded_swap_bitmap;
    swap_areas[0].device = -1;  /* -1 indicates simulated/embedded */
    swap_areas[0].flags = 0;
    num_swap_areas = 1;
    
    swap_initialized = 1;
    
    serial_printf("[SWAP] Initialized with %d pages (%d KB) simulated swap\n",
                  swap_areas[0].total_pages, 
                  swap_areas[0].total_pages * PAGE_SIZE / 1024);
}

/**
 * Swap out a page
 * 
 * Takes a virtual address and its PTE, writes the page to swap,
 * frees the physical page, and updates the PTE.
 */
int swap_out_page(uint32_t virt_addr, uint32_t *pte) {
    if (!swap_initialized) {
        serial_printf("[SWAP] Not initialized!\n");
        return -1;
    }
    
    if (!pte || !(*pte & PAGE_PRESENT)) {
        serial_printf("[SWAP] Page not present: 0x%x\n", virt_addr);
        return -1;
    }
    
    uint32_t phys_addr = *pte & ~0xFFF;
    
    /* Allocate swap slot */
    uint32_t slot = swap_alloc_slot();
    if (slot == (uint32_t)-1) {
        return -1;  /* Out of swap space */
    }
    
    /* Write page to swap */
    if (swap_write_page(slot, phys_addr) < 0) {
        swap_free_slot(slot);
        return -1;
    }
    
    /* Update PTE to indicate swapped page */
    *pte = (slot << PTE_SWAP_SLOT_SHIFT) | PTE_SWAPPED;
    
    /* Free physical page */
    pmm_free_page(phys_addr);
    
    /* Invalidate TLB entry */
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    
    swap_stats.pages_swapped_out++;
    
    serial_printf("[SWAP] Swapped out 0x%x to slot %d\n", virt_addr, slot);
    
    return 0;
}

/**
 * Swap in a page
 * 
 * Reads a page from swap into a newly allocated physical page.
 */
int swap_in_page(uint32_t virt_addr, uint32_t pte) {
    if (!swap_initialized) {
        serial_printf("[SWAP] Not initialized!\n");
        return -1;
    }
    
    if (!(pte & PTE_SWAPPED)) {
        serial_printf("[SWAP] Page not swapped: 0x%x\n", virt_addr);
        return -1;
    }
    
    uint32_t slot = pte >> PTE_SWAP_SLOT_SHIFT;
    
    /* Allocate physical page */
    uint32_t phys_addr = pmm_alloc_page();
    if (!phys_addr) {
        serial_printf("[SWAP] Out of memory during swap-in!\n");
        return -1;
    }
    
    /* Read page from swap */
    if (swap_read_page(slot, phys_addr) < 0) {
        pmm_free_page(phys_addr);
        return -1;
    }
    
    /* Free swap slot */
    swap_free_slot(slot);
    
    /* Map the page */
    vmm_map_page(virt_addr & ~0xFFF, phys_addr, 
                 PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    
    swap_stats.pages_swapped_in++;
    
    serial_printf("[SWAP] Swapped in 0x%x from slot %d to phys 0x%x\n",
                  virt_addr, slot & 0x00FFFFFF, phys_addr);
    
    return 0;
}

/**
 * Add a swap area (partition or file)
 */
int swap_add_area(int device, uint32_t start, uint32_t size) {
    if (num_swap_areas >= MAX_SWAP_AREAS) {
        serial_printf("[SWAP] Too many swap areas!\n");
        return -1;
    }
    
    uint32_t pages = size / PAGE_SIZE;
    if (pages > SWAP_MAX_PAGES) {
        pages = SWAP_MAX_PAGES;
    }
    
    /* Allocate bitmap */
    uint32_t bitmap_size = (pages + 31) / 32;
    uint32_t *bitmap = (uint32_t *)kmalloc(bitmap_size * sizeof(uint32_t));
    if (!bitmap) {
        serial_printf("[SWAP] Failed to allocate bitmap!\n");
        return -1;
    }
    
    /* Clear bitmap */
    for (uint32_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0;
    }
    
    /* Add swap area */
    int idx = num_swap_areas++;
    swap_areas[idx].start_block = start;
    swap_areas[idx].total_pages = pages;
    swap_areas[idx].free_pages = pages;
    swap_areas[idx].bitmap = bitmap;
    swap_areas[idx].device = device;
    swap_areas[idx].flags = 0;
    
    serial_printf("[SWAP] Added swap area: dev=%d start=%d pages=%d (%d KB)\n",
                  device, start, pages, pages * PAGE_SIZE / 1024);
    
    return 0;
}

/**
 * Get swap statistics
 */
void swap_get_stats(uint32_t *total, uint32_t *free) {
    uint32_t t = 0, f = 0;
    
    for (int i = 0; i < num_swap_areas; i++) {
        t += swap_areas[i].total_pages;
        f += swap_areas[i].free_pages;
    }
    
    if (total) *total = t;
    if (free)  *free = f;
}

/**
 * Print swap information
 */
void swap_print_info(void) {
    serial_printf("[SWAP] === Swap Info ===\n");
    
    for (int i = 0; i < num_swap_areas; i++) {
        swap_area_t *sa = &swap_areas[i];
        serial_printf("[SWAP] Area %d: dev=%d total=%d free=%d (%d%% used)\n",
                      i, sa->device, sa->total_pages, sa->free_pages,
                      sa->total_pages ? 
                        ((sa->total_pages - sa->free_pages) * 100 / sa->total_pages) : 0);
    }
    
    serial_printf("[SWAP] Stats: out=%d in=%d\n",
                  swap_stats.pages_swapped_out, swap_stats.pages_swapped_in);
}

// ==================== PAGE RECLAIM ====================

/**
 * Try to reclaim pages when memory is low
 * Returns number of pages freed
 */
uint32_t swap_try_reclaim(uint32_t target_pages) {
    uint32_t reclaimed = 0;
    
    /* First, try to evict page cache entries */
    reclaimed += vmm_page_cache_shrink(target_pages);
    
    if (reclaimed >= target_pages) {
        return reclaimed;
    }
    
    /* TODO: Implement active page swapping */
    /* This would scan process page tables and swap out cold pages */
    
    serial_printf("[SWAP] Reclaimed %d pages (target was %d)\n",
                  reclaimed, target_pages);
    
    return reclaimed;
}

/**
 * Check if swap space is critically low
 */
int swap_is_low(void) {
    uint32_t total, free;
    swap_get_stats(&total, &free);
    
    /* Consider low if less than 10% free */
    return (total > 0) && (free * 100 / total < 10);
}
