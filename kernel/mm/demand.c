/**
 * TocinOS Demand Paging Implementation
 * 
 * Handles page faults for demand-paged virtual memory:
 * - Anonymous pages (zero-filled on first access)
 * - File-backed pages (loaded from page cache)
 * - Copy-on-Write pages (duplicated on write)
 * - Stack growth
 * - Swap-in of swapped pages
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/memory.h"
#include "../../include/kernel/kernel.h"
#include "../../include/kernel/isr.h"

extern void serial_printf(const char *fmt, ...);

/* NULL definition */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Forward declarations */
vma_t* vma_find_nearest(mm_struct_t *mm, uint32_t addr);

/* Current process's memory descriptor */
mm_struct_t *current_mm = NULL;

/* Page fault statistics */
static struct {
    uint32_t total_faults;
    uint32_t minor_faults;    /* Fault resolved without disk I/O */
    uint32_t major_faults;    /* Fault required disk I/O */
    uint32_t cow_faults;      /* Copy-on-write faults */
    uint32_t segfaults;       /* Segmentation violations */
} pf_stats = {0};

// ==================== PAGE TABLE HELPERS ====================

/**
 * Get page table entry for a virtual address
 */
static uint32_t* get_pte(uint32_t virt_addr) {
    uint32_t dir_idx = virt_addr >> 22;
    uint32_t table_idx = (virt_addr >> 12) & 0x3FF;
    
    uint32_t *pd = (uint32_t *)0x9C000;  /* Kernel page directory */
    
    if (!(pd[dir_idx] & PAGE_PRESENT)) {
        return NULL;
    }
    
    uint32_t *pt = (uint32_t *)(pd[dir_idx] & ~0xFFF);
    return &pt[table_idx];
}

/**
 * Allocate and map a new page for demand paging
 */
static int alloc_and_map_page(uint32_t virt_addr, uint32_t flags) {
    /* Allocate physical page */
    uint32_t phys = pmm_alloc_page();
    if (!phys) {
        serial_printf("[DEMAND] Out of memory for 0x%x\n", virt_addr);
        return -1;
    }
    
    /* Zero the page */
    uint32_t *page = (uint32_t *)phys;
    for (int i = 0; i < 1024; i++) {
        page[i] = 0;
    }
    
    /* Map the page */
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if (flags & VM_WRITE) page_flags |= PAGE_WRITE;
    
    vmm_map_page(virt_addr & ~0xFFF, phys, page_flags);
    
    /* Update RSS */
    if (current_mm) {
        current_mm->rss++;
    }
    
    return 0;
}

// ==================== DEMAND PAGING HANDLERS ====================

/**
 * Handle anonymous page fault (heap, stack, anonymous mmap)
 */
int demand_page_anon(vma_t *vma, uint32_t fault_addr) {
    uint32_t page_addr = fault_addr & ~0xFFF;
    
    serial_printf("[DEMAND] Anonymous page at 0x%x\n", page_addr);
    
    if (alloc_and_map_page(page_addr, vma->vm_flags) < 0) {
        return -1;
    }
    
    pf_stats.minor_faults++;
    return 0;
}

/**
 * Handle file-backed page fault
 */
int demand_page_file(vma_t *vma, uint32_t fault_addr) {
    uint32_t page_addr = fault_addr & ~0xFFF;
    uint32_t vma_offset = page_addr - vma->vm_start;
    uint32_t file_offset = vma->vm_file_offset + vma_offset;
    uint32_t file_page = file_offset / PAGE_SIZE;
    
    serial_printf("[DEMAND] File page at 0x%x, inode=%d, offset=%d\n",
                  page_addr, vma->vm_file_inode, file_page);
    
    /* Check page cache first */
    vmm_page_cache_entry_t *cached = vmm_page_cache_lookup(vma->vm_file_inode, file_page);
    
    if (cached) {
        /* Page is in cache - just map it */
        uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
        
        if (vma->vm_flags & VM_SHARED) {
            /* Shared mapping - map the cached page directly */
            if (vma->vm_flags & VM_WRITE) page_flags |= PAGE_WRITE;
            vmm_map_page(page_addr, cached->phys_addr, page_flags);
            cached->ref_count++;
        } else {
            /* Private mapping - copy on first access if writable */
            if (vma->vm_flags & VM_WRITE) {
                /* Map read-only initially, COW on write */
                vmm_map_page(page_addr, cached->phys_addr, page_flags | PAGE_COW);
            } else {
                vmm_map_page(page_addr, cached->phys_addr, page_flags);
            }
            cached->ref_count++;
        }
        
        pf_stats.minor_faults++;
        return 0;
    }
    
    /* Page not in cache - need to read from disk */
    uint32_t phys = pmm_alloc_page();
    if (!phys) {
        serial_printf("[DEMAND] Out of memory for file page\n");
        return -1;
    }
    
    /* Read page from file */
    /* TODO: Implement actual file read via VFS */
    /* For now, just zero-fill as placeholder */
    uint32_t *page = (uint32_t *)phys;
    for (int i = 0; i < 1024; i++) {
        page[i] = 0;
    }
    
    /* Insert into page cache */
    vmm_page_cache_entry_t *entry = vmm_page_cache_insert(vma->vm_file_inode, file_page, phys);
    if (entry) {
        entry->uptodate = 1;
    }
    
    /* Map the page */
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_WRITE)) {
        page_flags |= PAGE_WRITE;
    } else if (vma->vm_flags & VM_WRITE) {
        page_flags |= PAGE_COW;  /* COW for private writable */
    }
    vmm_map_page(page_addr, phys, page_flags);
    
    if (current_mm) current_mm->rss++;
    pf_stats.major_faults++;
    
    return 0;
}

/**
 * Handle Copy-on-Write page fault
 */
int handle_cow_fault(uint32_t fault_addr, uint32_t pte) {
    uint32_t page_addr = fault_addr & ~0xFFF;
    uint32_t old_phys = pte & ~0xFFF;
    
    serial_printf("[COW] Fault at 0x%x, old_phys=0x%x\n", page_addr, old_phys);
    
    /* Allocate new page */
    uint32_t new_phys = pmm_alloc_page();
    if (!new_phys) {
        serial_printf("[COW] Out of memory!\n");
        return -1;
    }
    
    /* Copy page contents */
    uint32_t *src = (uint32_t *)old_phys;
    uint32_t *dst = (uint32_t *)new_phys;
    for (int i = 0; i < 1024; i++) {
        dst[i] = src[i];
    }
    
    /* Remap with write permission */
    vmm_map_page(page_addr, new_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    
    /* Decrement old page reference count */
    /* TODO: Track page reference counts properly */
    
    /* Update stats */
    if (current_mm) current_mm->rss++;
    pf_stats.cow_faults++;
    
    serial_printf("[COW] Copied page to 0x%x\n", new_phys);
    return 0;
}

/**
 * Handle stack growth
 */
static int handle_stack_growth(mm_struct_t *mm, vma_t *vma, uint32_t fault_addr) {
    uint32_t page_addr = fault_addr & ~0xFFF;
    
    /* Stack grows down - check if fault is just below current stack VMA */
    if (fault_addr < vma->vm_start && 
        fault_addr >= vma->vm_start - PAGE_SIZE) {
        
        /* Extend stack VMA downward */
        vma->vm_start -= PAGE_SIZE;
        mm->stack_start = vma->vm_start;
        
        serial_printf("[STACK] Growing stack to 0x%x\n", vma->vm_start);
        
        /* Allocate the new stack page */
        return alloc_and_map_page(page_addr, vma->vm_flags);
    }
    
    return -1;  /* Not a valid stack growth */
}

/**
 * Handle swap-in
 */
static int handle_swap_in(uint32_t fault_addr, uint32_t pte) {
    uint32_t page_addr = fault_addr & ~0xFFF;
    uint32_t swap_slot = pte >> PTE_SWAP_SLOT_SHIFT;
    
    serial_printf("[SWAP] Swapping in page at 0x%x from slot %d\n", 
                  page_addr, swap_slot);
    
    /* Allocate new page */
    uint32_t phys = pmm_alloc_page();
    if (!phys) {
        /* Try to evict some pages first */
        vmm_page_cache_evict(4);
        phys = pmm_alloc_page();
        if (!phys) {
            serial_printf("[SWAP] Out of memory during swap-in!\n");
            return -1;
        }
    }
    
    /* Read from swap */
    if (swap_in_page(page_addr, pte) < 0) {
        pmm_free_page(phys);
        return -1;
    }
    
    /* Free swap slot */
    swap_free_slot(swap_slot);
    
    /* Map the page */
    vmm_map_page(page_addr, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    
    if (current_mm) current_mm->rss++;
    pf_stats.major_faults++;
    
    return 0;
}

// ==================== MAIN PAGE FAULT HANDLER ====================

/**
 * Enhanced page fault handler
 * 
 * Called from the ISR when a page fault occurs (interrupt 14)
 */
pf_result_t handle_page_fault(uint32_t fault_addr, uint32_t error_code) {
    pf_stats.total_faults++;
    
    serial_printf("[PF] Fault at 0x%x, error=0x%x (P=%d W=%d U=%d)\n",
                  fault_addr, error_code,
                  (error_code & PF_PRESENT) ? 1 : 0,
                  (error_code & PF_WRITE) ? 1 : 0,
                  (error_code & PF_USER) ? 1 : 0);
    
    /* Get PTE for faulting address */
    uint32_t *pte_ptr = get_pte(fault_addr);
    uint32_t pte = pte_ptr ? *pte_ptr : 0;
    
    /* Check if this is a swapped page */
    if (pte_ptr && (pte & PTE_SWAPPED) && !(pte & PAGE_PRESENT)) {
        return (handle_swap_in(fault_addr, pte) == 0) ? PF_HANDLED : PF_SWAP_ERROR;
    }
    
    /* Check if this is a COW fault */
    if ((error_code & PF_PRESENT) && (error_code & PF_WRITE) && (pte & PAGE_COW)) {
        return (handle_cow_fault(fault_addr, pte) == 0) ? PF_HANDLED : PF_OOM;
    }
    
    /* Get current mm and find VMA */
    if (!current_mm) {
        /* Kernel page fault - this is bad */
        if (!(error_code & PF_USER)) {
            serial_printf("[PF] Kernel page fault at 0x%x!\n", fault_addr);
            return PF_SIGSEGV;
        }
        pf_stats.segfaults++;
        return PF_SIGSEGV;
    }
    
    vma_t *vma = vma_find(current_mm, fault_addr);
    
    if (!vma) {
        /* Check for stack growth */
        vma = vma_find_nearest(current_mm, fault_addr);
        if (vma && (vma->vm_flags & VM_STACK)) {
            if (handle_stack_growth(current_mm, vma, fault_addr) == 0) {
                return PF_HANDLED;
            }
        }
        
        /* Address not in any VMA - segfault */
        serial_printf("[PF] No VMA for address 0x%x\n", fault_addr);
        pf_stats.segfaults++;
        return PF_SIGSEGV;
    }
    
    /* Check permissions */
    if ((error_code & PF_WRITE) && !(vma->vm_flags & VM_WRITE)) {
        serial_printf("[PF] Write to read-only VMA at 0x%x\n", fault_addr);
        pf_stats.segfaults++;
        return PF_SIGSEGV;
    }
    
    if ((error_code & PF_USER) && !(vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC))) {
        serial_printf("[PF] Access to protected VMA at 0x%x\n", fault_addr);
        pf_stats.segfaults++;
        return PF_SIGSEGV;
    }
    
    /* Handle demand paging based on VMA type */
    int result;
    
    if (vma->vm_flags & VM_ANONYMOUS) {
        result = demand_page_anon(vma, fault_addr);
    } else if (vma->vm_flags & VM_FILE) {
        result = demand_page_file(vma, fault_addr);
    } else {
        /* Default to anonymous */
        result = demand_page_anon(vma, fault_addr);
    }
    
    if (result < 0) {
        return PF_OOM;
    }
    
    return PF_HANDLED;
}

// ==================== ISR INTEGRATION ====================

/**
 * Page fault ISR handler wrapper
 * Called from isr_handler when interrupt 14 occurs
 */
static void page_fault_isr(registers_t *regs) {
    uint32_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    pf_result_t result = handle_page_fault(fault_addr, regs->err_code);
    
    switch (result) {
        case PF_HANDLED:
            /* Fault handled, return to faulting instruction */
            return;
            
        case PF_SIGSEGV:
            serial_printf("[PF] SIGSEGV: Segmentation fault at 0x%x, EIP=0x%x\n",
                          fault_addr, regs->eip);
            /* TODO: Send SIGSEGV to process */
            /* For now, just halt */
            __asm__ volatile("cli; hlt");
            break;
            
        case PF_SIGBUS:
            serial_printf("[PF] SIGBUS: Bus error at 0x%x\n", fault_addr);
            __asm__ volatile("cli; hlt");
            break;
            
        case PF_OOM:
            serial_printf("[PF] OOM: Out of memory at 0x%x\n", fault_addr);
            /* TODO: Try to reclaim memory, kill process if needed */
            __asm__ volatile("cli; hlt");
            break;
            
        case PF_SWAP_ERROR:
            serial_printf("[PF] Swap error at 0x%x\n", fault_addr);
            __asm__ volatile("cli; hlt");
            break;
    }
}

/**
 * Initialize demand paging
 */
void demand_paging_init(void) {
    /* Register page fault handler */
    isr_register_handler(14, page_fault_isr);
    
    /* Initialize page cache */
    vmm_page_cache_init();
    
    /* Initialize swap */
    swap_init();
    
    serial_printf("[DEMAND] Demand paging initialized\n");
}

/**
 * Get page fault statistics
 */
void demand_paging_stats(uint32_t *total, uint32_t *minor, uint32_t *major, 
                         uint32_t *cow, uint32_t *segv) {
    if (total) *total = pf_stats.total_faults;
    if (minor) *minor = pf_stats.minor_faults;
    if (major) *major = pf_stats.major_faults;
    if (cow)   *cow   = pf_stats.cow_faults;
    if (segv)  *segv  = pf_stats.segfaults;
}

// ==================== EXTERNAL DECLARATIONS ====================
// These functions are implemented in other mm files

/* VMA management - from vma.c */
extern vma_t* vma_find_nearest(mm_struct_t *mm, uint32_t addr);

/* Page cache - from page_cache.c */
extern vmm_page_cache_entry_t* vmm_page_cache_lookup(uint32_t inode, uint32_t offset);
extern vmm_page_cache_entry_t* vmm_page_cache_insert(uint32_t inode, uint32_t offset, uint32_t phys);
extern void vmm_page_cache_evict(uint32_t num_pages);

/* Swap - from swap.c */
extern int swap_in_page(uint32_t virt_addr, uint32_t pte);
extern void swap_free_slot(uint32_t slot);
