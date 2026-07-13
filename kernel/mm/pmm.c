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
    
    // Mark first 2MB as used (for BIOS, bootloader, kernel at 1MB, and kernel data)
    // Kernel is loaded at 0x100000 (1MB) and is about 100KB
    for (unsigned int i = 0; i < (2 * 1024 * 1024 / PAGE_SIZE); i++) {
        pmm_set_page_used(i);
    }
}

/**
 * Mark every page overlapping [base, base+length) as used, clamped to the
 * managed 128MB window. Already-used pages are skipped so used_pages stays
 * exact when regions overlap (e.g. the always-reserved first 2MB).
 */
static void pmm_reserve_range(tb_u64 base, tb_u64 length) {
    if (length == 0 || base >= (tb_u64)MEMORY_SIZE) {
        return;
    }
    tb_u64 end = base + length;
    if (end < base || end > (tb_u64)MEMORY_SIZE) {
        end = MEMORY_SIZE;  // clamp to window (also handles u64 wrap)
    }
    unsigned int first = (unsigned int)(base / PAGE_SIZE);
    unsigned int last = (unsigned int)((end + PAGE_SIZE - 1) / PAGE_SIZE);
    for (unsigned int page = first; page < last; page++) {
        unsigned int byte = page / 8;
        unsigned int bit = page % 8;
        if (!(memory_bitmap[byte] & (1u << bit))) {
            memory_bitmap[byte] |= (1u << bit);
            used_pages++;
        }
    }
}

/**
 * Initialize the PMM from a TocinBoot memory map (docs/BOOT_PROTOCOL.md §4).
 *
 * Conservative v1: identical bitmap and low-2MB reservation as pmm_init(),
 * plus reservations for every non-USABLE memmap region inside the 128MB
 * window (BOOTLOADER included — reclaim is M2, spec §4.2) and for the
 * framebuffer range if it lies below 128MB.
 */
void pmm_init_from_bootinfo(const tocinboot_info *info) {
    pmm_init();

    if (!info) {
        return;
    }

    // Iterate by memmap_entry_size, never by sizeof (spec §8.6).
    if (info->memmap_addr != 0 && info->memmap_count != 0 &&
        info->memmap_entry_size >= sizeof(tocinboot_mmap_entry)) {
        const unsigned char *p =
            (const unsigned char *)(unsigned long)info->memmap_addr;
        for (tb_u32 i = 0; i < info->memmap_count; i++) {
            const tocinboot_mmap_entry *e = (const tocinboot_mmap_entry *)p;
            if (e->type != TOCINBOOT_MEM_USABLE) {
                pmm_reserve_range(e->base, e->length);
            }
            p += info->memmap_entry_size;
        }
    }

    // Framebuffer is device memory; keep it out of the allocator (spec §5).
    if ((info->flags & TOCINBOOT_F_FB) && info->fb_base != 0) {
        pmm_reserve_range(info->fb_base,
                          (tb_u64)info->fb_pitch * (tb_u64)info->fb_height);
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
