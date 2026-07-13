/**
 * TocinOS Virtual Memory Manager (VMM)
 *
 * 2-level x86 paging. Two API layers:
 *
 *  - vmm_dir_* : operate on an explicit page directory (physical address).
 *    Used by the VMA/COW layer to build and tear down NON-current address
 *    spaces (fork children). Page tables and directories always come from
 *    the PMM window [0, 16MB), which is identity-mapped at boot, so they
 *    are dereferenceable through their physical address both in the
 *    kernel and in the host unit tests (tests/framework/kmem_env.c maps
 *    the same fixed windows).
 *
 *  - classic vmm_*  : operate on the CURRENT directory. TLB entries are
 *    invalidated only when the modified directory is the current one.
 */

#include "../include/kernel/memory.h"

#define TABLES_PER_DIR  1024
#define PAGES_PER_TABLE 1024
#define PTE_FLAGS_MASK  0xFFFu

typedef struct {
    unsigned int entries[PAGES_PER_TABLE];
} page_table_t;

typedef struct {
    unsigned int entries[TABLES_PER_DIR];
} page_directory_t;

static page_directory_t *kernel_directory = (page_directory_t *)0x9C000;
static unsigned int current_directory = 0x9C000;

/* Invalidate one TLB entry — only meaningful (and only legal) in-kernel. */
static void tlb_flush_page(uint32_t virt) {
#ifdef __i386__
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
#else
    (void)virt; /* host unit tests: no TLB */
#endif
}

static void flush_if_current(uint32_t dir_phys, uint32_t virt) {
    if (dir_phys == current_directory) {
        tlb_flush_page(virt);
    }
}

/**
 * Return a pointer to the PTE for virt in the given directory.
 * When create != 0, a missing page table is allocated from the PMM and
 * zeroed. Returns NULL if the table is absent (and !create) or if the
 * PMM is exhausted.
 */
static uint32_t *dir_pte_ptr(uint32_t dir_phys, uint32_t virt, int create) {
    uint32_t *dir = (uint32_t *)dir_phys;
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    if (!(dir[dir_index] & PAGE_PRESENT)) {
        if (!create) {
            return (uint32_t *)0;
        }
        uint32_t table_phys = pmm_alloc_page();
        if (!table_phys) {
            return (uint32_t *)0;
        }
        uint32_t *table = (uint32_t *)table_phys;
        for (int i = 0; i < PAGES_PER_TABLE; i++) {
            table[i] = 0;
        }
        dir[dir_index] = table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint32_t *table = (uint32_t *)(dir[dir_index] & ~PTE_FLAGS_MASK);
    return &table[table_index];
}

// ==================== EXPLICIT-DIRECTORY API ====================

/** Raw PTE for virt in dir (0 when the table or entry is absent). */
uint32_t vmm_dir_get_pte(uint32_t dir_phys, uint32_t virt) {
    uint32_t *pte = dir_pte_ptr(dir_phys, virt, 0);
    return pte ? *pte : 0;
}

/** Map virt -> phys in dir. PAGE_PRESENT is implied. 0 ok, -1 no memory. */
int vmm_dir_map_page(uint32_t dir_phys, uint32_t virt, uint32_t phys,
                     uint32_t flags) {
    uint32_t *pte = dir_pte_ptr(dir_phys, virt, 1);
    if (!pte) {
        return -1;
    }
    *pte = (phys & ~PTE_FLAGS_MASK) | PAGE_PRESENT | (flags & PTE_FLAGS_MASK);
    flush_if_current(dir_phys, virt);
    return 0;
}

/** Clear the PTE for virt in dir (frame is NOT freed). */
void vmm_dir_unmap_page(uint32_t dir_phys, uint32_t virt) {
    uint32_t *pte = dir_pte_ptr(dir_phys, virt, 0);
    if (pte && *pte) {
        *pte = 0;
        flush_if_current(dir_phys, virt);
    }
}

/**
 * Mark a present PTE copy-on-write: clear PAGE_WRITE, set PAGE_COW.
 * Already-COW entries are left untouched. Returns 0 when the entry is
 * (now) COW, -1 when there is no present mapping.
 */
int vmm_dir_mark_cow(uint32_t dir_phys, uint32_t virt) {
    uint32_t *pte = dir_pte_ptr(dir_phys, virt, 0);
    if (!pte || !(*pte & PAGE_PRESENT)) {
        return -1;
    }
    if (*pte & PAGE_WRITE) {
        *pte = (*pte & ~(uint32_t)PAGE_WRITE) | PAGE_COW;
        flush_if_current(dir_phys, virt);
    } else {
        *pte |= PAGE_COW;
    }
    return 0;
}

// ==================== CURRENT-DIRECTORY API ====================

/**
 * Initialize the virtual memory manager (boot path only): identity map
 * the first 4MB through the fixed low-memory tables and enable paging.
 */
void vmm_init(void) {
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        kernel_directory->entries[i] = 0;
    }

    page_table_t *first_table = (page_table_t *)0x9D000;
    for (int i = 0; i < PAGES_PER_TABLE; i++) {
        first_table->entries[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }
    kernel_directory->entries[0] =
        ((unsigned int)first_table) | PAGE_PRESENT | PAGE_WRITE;

    vmm_switch_directory((unsigned int)kernel_directory);
}

/**
 * Map a virtual address to a physical address in the current directory.
 */
void vmm_map_page(unsigned int virtual_addr, unsigned int physical_addr,
                  unsigned int flags) {
    vmm_dir_map_page(current_directory, virtual_addr, physical_addr, flags);
}

/**
 * Unmap a virtual address in the current directory.
 */
void vmm_unmap_page(unsigned int virtual_addr) {
    vmm_dir_unmap_page(current_directory, virtual_addr);
}

/**
 * Switch page directory (loads CR3, enables paging).
 */
void vmm_switch_directory(unsigned int directory_phys) {
    current_directory = directory_phys;

    #ifdef __i386__
    /* PG (bit 31) enables paging; WP (bit 16) makes ring 0 honor
     * read-only PTEs. Without WP, kernel-mode writes would silently
     * bypass the read-only + PAGE_COW protection that COW fork relies
     * on and scribble on shared frames. Every existing kernel mapping
     * (identity maps, page tables, framebuffer, ELF segments) is
     * PAGE_WRITE, so WP only exposes genuine COW/RO violations. */
    __asm__ volatile(
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80010000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(directory_phys) : "eax"
    );
    #endif
}

/**
 * Get current page directory (physical address).
 */
unsigned int vmm_get_current_directory(void) {
    return current_directory;
}

/**
 * Get physical address for a virtual address (0 if not present).
 */
unsigned int vmm_get_physical(unsigned int virtual_addr) {
    uint32_t pte = vmm_dir_get_pte(current_directory, virtual_addr);
    if (!(pte & PAGE_PRESENT)) {
        return 0;
    }
    return pte & ~PTE_FLAGS_MASK;
}

// Alias for compatibility
uint32_t vmm_virt_to_phys(uint32_t virt_addr) {
    return vmm_get_physical(virt_addr);
}

/**
 * 1 if the page containing virt_addr is present in the current directory.
 * (Roadmap bug #7: was declared but never implemented.)
 */
int vmm_is_mapped(uint32_t virt_addr) {
    return (vmm_dir_get_pte(current_directory, virt_addr) & PAGE_PRESENT)
               ? 1 : 0;
}

/**
 * PTE flag bits (low 12) for virt_addr, or -1 when no mapping exists.
 * PAGE_COW (bit 9) is included. (Roadmap bug #7.)
 */
int vmm_get_page_flags(uint32_t virt_addr) {
    uint32_t pte = vmm_dir_get_pte(current_directory, virt_addr);
    if (pte == 0) {
        return -1;
    }
    return (int)(pte & PTE_FLAGS_MASK);
}

/**
 * Replace the flag bits of an existing PTE (frame is preserved).
 * Returns 0 on success, -1 when no mapping exists.
 */
int vmm_set_page_flags(unsigned int vaddr, unsigned int flags) {
    uint32_t *pte = dir_pte_ptr(current_directory, vaddr, 0);
    if (!pte || *pte == 0) {
        return -1;
    }
    *pte = (*pte & ~PTE_FLAGS_MASK) | (flags & PTE_FLAGS_MASK);
    tlb_flush_page(vaddr);
    return 0;
}

/**
 * Map size bytes from phys_start at virt_start (current directory).
 * Both starts must be page aligned; size is rounded up to whole pages.
 * Returns 0 on success, -1 on bad arguments or PMM exhaustion.
 * (Roadmap bug #7.)
 */
int vmm_map_range(uint32_t virt_start, uint32_t phys_start, uint32_t size,
                  uint32_t flags) {
    if ((virt_start & (PAGE_SIZE - 1)) || (phys_start & (PAGE_SIZE - 1)) ||
        size == 0) {
        return -1;
    }
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        if (vmm_dir_map_page(current_directory, virt_start + i * PAGE_SIZE,
                             phys_start + i * PAGE_SIZE, flags) < 0) {
            /* Roll back the pages mapped so far. */
            while (i > 0) {
                i--;
                vmm_dir_unmap_page(current_directory,
                                   virt_start + i * PAGE_SIZE);
            }
            return -1;
        }
    }
    return 0;
}

/**
 * Unmap size bytes starting at virt_start (current directory). Frames
 * are NOT freed — this is the pure inverse of vmm_map_range().
 * Returns 0 on success, -1 on bad arguments. (Roadmap bug #7.)
 */
int vmm_unmap_range(uint32_t virt_start, uint32_t size) {
    if ((virt_start & (PAGE_SIZE - 1)) || size == 0) {
        return -1;
    }
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        vmm_dir_unmap_page(current_directory, virt_start + i * PAGE_SIZE);
    }
    return 0;
}

/**
 * Mark the page at vaddr copy-on-write in the current directory
 * (read-only + PAGE_COW; resolved by the #PF path in mm/demand.c).
 */
int vmm_mark_cow(unsigned int vaddr) {
    return vmm_dir_mark_cow(current_directory, vaddr);
}
