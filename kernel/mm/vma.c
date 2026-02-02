/**
 * TocinOS Virtual Memory Area (VMA) Management
 * 
 * Implements process virtual memory management with VMAs,
 * supporting demand paging, file-backed mappings, and COW.
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

// ==================== VMA SLAB CACHE ====================

/* Simple VMA free list for allocation */
#define VMA_POOL_SIZE 256
static vma_t vma_pool[VMA_POOL_SIZE];
static vma_t *vma_free_list = NULL;
static int vma_initialized = 0;

/* MM struct pool */
#define MM_POOL_SIZE 64
static mm_struct_t mm_pool[MM_POOL_SIZE];
static mm_struct_t *mm_free_list = NULL;

/**
 * Initialize VMA allocator
 */
static void vma_allocator_init(void) {
    if (vma_initialized) return;
    
    /* Build VMA free list */
    for (int i = 0; i < VMA_POOL_SIZE - 1; i++) {
        vma_pool[i].vm_next = &vma_pool[i + 1];
    }
    vma_pool[VMA_POOL_SIZE - 1].vm_next = NULL;
    vma_free_list = &vma_pool[0];
    
    /* Build MM free list */
    for (int i = 0; i < MM_POOL_SIZE - 1; i++) {
        mm_pool[i].vmas = (vma_t *)&mm_pool[i + 1];  /* Reuse pointer for free list */
    }
    mm_pool[MM_POOL_SIZE - 1].vmas = NULL;
    mm_free_list = &mm_pool[0];
    
    vma_initialized = 1;
    serial_printf("[VMA] Allocator initialized: %d VMAs, %d MMs\n", 
                  VMA_POOL_SIZE, MM_POOL_SIZE);
}

/**
 * Allocate a VMA from the pool
 */
static vma_t* vma_alloc(void) {
    if (!vma_initialized) vma_allocator_init();
    
    if (!vma_free_list) {
        serial_printf("[VMA] ERROR: Out of VMAs!\n");
        return NULL;
    }
    
    vma_t *vma = vma_free_list;
    vma_free_list = vma->vm_next;
    
    /* Clear the VMA */
    vma->vm_start = 0;
    vma->vm_end = 0;
    vma->vm_flags = 0;
    vma->vm_file_inode = 0;
    vma->vm_file_offset = 0;
    vma->vm_fd = -1;
    vma->swap_count = 0;
    vma->ref_count = 1;
    vma->vm_next = NULL;
    vma->vm_prev = NULL;
    
    return vma;
}

/**
 * Return a VMA to the pool
 */
static void vma_free(vma_t *vma) {
    if (!vma) return;
    
    vma->vm_next = vma_free_list;
    vma_free_list = vma;
}

// ==================== VMA MANAGEMENT ====================

/**
 * Create a new VMA
 */
vma_t* vma_create(uint32_t start, uint32_t end, uint32_t flags) {
    /* Validate addresses */
    if (start >= end || (start & 0xFFF) || (end & 0xFFF)) {
        serial_printf("[VMA] Invalid range: 0x%x-0x%x\n", start, end);
        return NULL;
    }
    
    vma_t *vma = vma_alloc();
    if (!vma) return NULL;
    
    vma->vm_start = start;
    vma->vm_end = end;
    vma->vm_flags = flags;
    vma->ref_count = 1;
    
    serial_printf("[VMA] Created: 0x%x-0x%x flags=0x%x\n", start, end, flags);
    return vma;
}

/**
 * Destroy a VMA
 */
void vma_destroy(vma_t *vma) {
    if (!vma) return;
    
    vma->ref_count--;
    if (vma->ref_count == 0) {
        serial_printf("[VMA] Destroyed: 0x%x-0x%x\n", vma->vm_start, vma->vm_end);
        vma_free(vma);
    }
}

/**
 * Find VMA containing the given address
 */
vma_t* vma_find(mm_struct_t *mm, uint32_t addr) {
    if (!mm) return NULL;
    
    vma_t *vma = mm->vmas;
    while (vma) {
        if (addr >= vma->vm_start && addr < vma->vm_end) {
            return vma;
        }
        vma = vma->vm_next;
    }
    return NULL;
}

/**
 * Find VMA containing or after the given address
 */
vma_t* vma_find_nearest(mm_struct_t *mm, uint32_t addr) {
    if (!mm) return NULL;
    
    vma_t *nearest = NULL;
    vma_t *vma = mm->vmas;
    
    while (vma) {
        if (addr < vma->vm_end) {
            if (!nearest || vma->vm_start < nearest->vm_start) {
                nearest = vma;
            }
        }
        vma = vma->vm_next;
    }
    return nearest;
}

/**
 * Insert VMA into mm's VMA list (sorted by address)
 */
int vma_insert(mm_struct_t *mm, vma_t *vma) {
    if (!mm || !vma) return -1;
    
    /* Empty list case */
    if (!mm->vmas) {
        mm->vmas = vma;
        vma->vm_next = NULL;
        vma->vm_prev = NULL;
        mm->vma_count = 1;
        return 0;
    }
    
    /* Find insertion point (sorted by vm_start) */
    vma_t *curr = mm->vmas;
    vma_t *prev = NULL;
    
    while (curr && curr->vm_start < vma->vm_start) {
        prev = curr;
        curr = curr->vm_next;
    }
    
    /* Check for overlap with previous */
    if (prev && prev->vm_end > vma->vm_start) {
        serial_printf("[VMA] Insert overlap with prev: 0x%x-0x%x\n", 
                      prev->vm_start, prev->vm_end);
        return -1;
    }
    
    /* Check for overlap with next */
    if (curr && vma->vm_end > curr->vm_start) {
        serial_printf("[VMA] Insert overlap with next: 0x%x-0x%x\n", 
                      curr->vm_start, curr->vm_end);
        return -1;
    }
    
    /* Insert between prev and curr */
    vma->vm_prev = prev;
    vma->vm_next = curr;
    
    if (prev) {
        prev->vm_next = vma;
    } else {
        mm->vmas = vma;
    }
    
    if (curr) {
        curr->vm_prev = vma;
    }
    
    mm->vma_count++;
    
    /* Update total_vm */
    mm->total_vm += (vma->vm_end - vma->vm_start);
    
    return 0;
}

/**
 * Remove VMA from mm's list
 */
int vma_remove(mm_struct_t *mm, vma_t *vma) {
    if (!mm || !vma) return -1;
    
    if (vma->vm_prev) {
        vma->vm_prev->vm_next = vma->vm_next;
    } else {
        mm->vmas = vma->vm_next;
    }
    
    if (vma->vm_next) {
        vma->vm_next->vm_prev = vma->vm_prev;
    }
    
    mm->vma_count--;
    mm->total_vm -= (vma->vm_end - vma->vm_start);
    
    vma->vm_next = NULL;
    vma->vm_prev = NULL;
    
    return 0;
}

/**
 * Try to merge VMA with adjacent VMAs having same flags
 */
int vma_merge(mm_struct_t *mm, vma_t *vma) {
    if (!mm || !vma) return 0;
    
    int merged = 0;
    
    /* Try to merge with previous */
    if (vma->vm_prev && 
        vma->vm_prev->vm_end == vma->vm_start &&
        vma->vm_prev->vm_flags == vma->vm_flags &&
        vma->vm_prev->vm_fd == vma->vm_fd) {
        
        vma_t *prev = vma->vm_prev;
        prev->vm_end = vma->vm_end;
        
        /* Remove vma from list */
        prev->vm_next = vma->vm_next;
        if (vma->vm_next) {
            vma->vm_next->vm_prev = prev;
        }
        
        mm->vma_count--;
        vma_free(vma);
        vma = prev;
        merged++;
    }
    
    /* Try to merge with next */
    if (vma->vm_next &&
        vma->vm_end == vma->vm_next->vm_start &&
        vma->vm_flags == vma->vm_next->vm_flags &&
        vma->vm_fd == vma->vm_next->vm_fd) {
        
        vma_t *next = vma->vm_next;
        vma->vm_end = next->vm_end;
        
        /* Remove next from list */
        vma->vm_next = next->vm_next;
        if (next->vm_next) {
            next->vm_next->vm_prev = vma;
        }
        
        mm->vma_count--;
        vma_free(next);
        merged++;
    }
    
    return merged;
}

// ==================== MM STRUCT MANAGEMENT ====================

/**
 * Allocate an mm_struct from pool
 */
static mm_struct_t* mm_alloc(void) {
    if (!vma_initialized) vma_allocator_init();
    
    if (!mm_free_list) {
        serial_printf("[MM] ERROR: Out of mm_structs!\n");
        return NULL;
    }
    
    mm_struct_t *mm = mm_free_list;
    mm_free_list = (mm_struct_t *)mm->vmas;
    
    /* Clear the mm_struct */
    mm->vmas = NULL;
    mm->vma_count = 0;
    mm->page_directory = 0;
    mm->code_start = 0;
    mm->code_end = 0;
    mm->data_start = 0;
    mm->data_end = 0;
    mm->heap_start = 0;
    mm->heap_end = 0;
    mm->stack_start = 0;
    mm->stack_end = 0;
    mm->mmap_base = 0x40000000;  /* Default mmap base at 1GB */
    mm->total_vm = 0;
    mm->rss = 0;
    mm->shared = 0;
    mm->locked = 0;
    mm->ref_count = 1;
    
    return mm;
}

/**
 * Return mm_struct to pool
 */
static void mm_free_struct(mm_struct_t *mm) {
    if (!mm) return;
    
    mm->vmas = (vma_t *)mm_free_list;
    mm_free_list = mm;
}

/**
 * Create a new mm_struct
 */
mm_struct_t* mm_create(void) {
    mm_struct_t *mm = mm_alloc();
    if (!mm) return NULL;
    
    /* Allocate page directory */
    mm->page_directory = pmm_alloc_page();
    if (!mm->page_directory) {
        mm_free_struct(mm);
        return NULL;
    }
    
    /* Clear page directory */
    uint32_t *pd = (uint32_t *)mm->page_directory;
    for (int i = 0; i < 1024; i++) {
        pd[i] = 0;
    }
    
    /* Copy kernel mappings (upper 1GB) - entries 768-1023 */
    extern uint32_t *kernel_directory;
    uint32_t *kernel_pd = (uint32_t *)0x9C000;  /* Kernel page directory */
    for (int i = 768; i < 1024; i++) {
        pd[i] = kernel_pd[i];
    }
    
    serial_printf("[MM] Created mm_struct at 0x%x, page_dir=0x%x\n", 
                  (uint32_t)mm, mm->page_directory);
    
    return mm;
}

/**
 * Destroy an mm_struct and all its VMAs
 */
void mm_destroy(mm_struct_t *mm) {
    if (!mm) return;
    
    /* Free all VMAs */
    vma_t *vma = mm->vmas;
    while (vma) {
        vma_t *next = vma->vm_next;
        
        /* Unmap all pages in this VMA */
        for (uint32_t addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            uint32_t phys = vmm_virt_to_phys(addr);
            if (phys) {
                vmm_unmap_page(addr);
                pmm_free_page(phys);
            }
        }
        
        vma_destroy(vma);
        vma = next;
    }
    
    /* Free page directory */
    if (mm->page_directory) {
        /* TODO: Free page tables too */
        pmm_free_page(mm->page_directory);
    }
    
    serial_printf("[MM] Destroyed mm_struct at 0x%x\n", (uint32_t)mm);
    mm_free_struct(mm);
}

/**
 * Clone an mm_struct (for fork)
 */
mm_struct_t* mm_clone(mm_struct_t *src) {
    if (!src) return NULL;
    
    mm_struct_t *dst = mm_create();
    if (!dst) return NULL;
    
    /* Copy region info */
    dst->code_start = src->code_start;
    dst->code_end = src->code_end;
    dst->data_start = src->data_start;
    dst->data_end = src->data_end;
    dst->heap_start = src->heap_start;
    dst->heap_end = src->heap_end;
    dst->stack_start = src->stack_start;
    dst->stack_end = src->stack_end;
    dst->mmap_base = src->mmap_base;
    
    /* Clone VMAs */
    vma_t *src_vma = src->vmas;
    while (src_vma) {
        vma_t *dst_vma = vma_create(src_vma->vm_start, src_vma->vm_end, 
                                    src_vma->vm_flags);
        if (!dst_vma) {
            mm_destroy(dst);
            return NULL;
        }
        
        dst_vma->vm_file_inode = src_vma->vm_file_inode;
        dst_vma->vm_file_offset = src_vma->vm_file_offset;
        dst_vma->vm_fd = src_vma->vm_fd;
        
        vma_insert(dst, dst_vma);
        
        /* Mark pages as COW if private and writable */
        if ((src_vma->vm_flags & VM_PRIVATE) && (src_vma->vm_flags & VM_WRITE)) {
            for (uint32_t addr = src_vma->vm_start; addr < src_vma->vm_end; 
                 addr += PAGE_SIZE) {
                /* Mark source page as COW (read-only + COW flag) */
                vmm_mark_cow(addr);
            }
        }
        
        src_vma = src_vma->vm_next;
    }
    
    serial_printf("[MM] Cloned mm_struct from 0x%x to 0x%x\n", 
                  (uint32_t)src, (uint32_t)dst);
    
    return dst;
}

/**
 * Decrement reference count and destroy if zero
 */
void mm_release(mm_struct_t *mm) {
    if (!mm) return;
    
    mm->ref_count--;
    if (mm->ref_count == 0) {
        mm_destroy(mm);
    }
}

// ==================== MMAP IMPLEMENTATION ====================

/**
 * Find free address space in mm
 */
static uint32_t find_free_region(mm_struct_t *mm, uint32_t size) {
    if (!mm) return 0;
    
    uint32_t addr = mm->mmap_base;
    vma_t *vma = mm->vmas;
    
    while (vma) {
        if (addr + size <= vma->vm_start) {
            /* Found gap before this VMA */
            return addr;
        }
        if (vma->vm_end > addr) {
            addr = vma->vm_end;
        }
        vma = vma->vm_next;
    }
    
    /* Check if there's space after all VMAs */
    if (addr + size <= 0xC0000000) {  /* Below kernel space */
        return addr;
    }
    
    return 0;  /* No space found */
}

/**
 * System call: mmap
 */
void* sys_mmap(void *addr, uint32_t length, int prot, int flags, 
               int fd, uint32_t offset) {
    /* Get current task's mm */
    extern mm_struct_t *current_mm;
    mm_struct_t *mm = current_mm;
    if (!mm) {
        serial_printf("[MMAP] No mm_struct for current task\n");
        return MAP_FAILED;
    }
    
    /* Round up length to page size */
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (length == 0) return MAP_FAILED;
    
    /* Determine mapping address */
    uint32_t map_addr;
    if (flags & MAP_FIXED) {
        map_addr = (uint32_t)addr & ~(PAGE_SIZE - 1);
        if (map_addr < 0x1000 || map_addr >= 0xC0000000) {
            return MAP_FAILED;
        }
        /* TODO: Unmap existing mappings at this address */
    } else {
        map_addr = find_free_region(mm, length);
        if (!map_addr) {
            serial_printf("[MMAP] No free region for %d bytes\n", length);
            return MAP_FAILED;
        }
    }
    
    /* Convert prot to VMA flags */
    uint32_t vm_flags = VM_LAZY;  /* Always use demand paging */
    if (prot & PROT_READ)  vm_flags |= VM_READ;
    if (prot & PROT_WRITE) vm_flags |= VM_WRITE;
    if (prot & PROT_EXEC)  vm_flags |= VM_EXEC;
    
    if (flags & MAP_SHARED)    vm_flags |= VM_SHARED;
    if (flags & MAP_PRIVATE)   vm_flags |= VM_PRIVATE;
    if (flags & MAP_ANONYMOUS) vm_flags |= VM_ANONYMOUS;
    if (flags & MAP_LOCKED)    vm_flags |= VM_LOCKED;
    if (flags & MAP_STACK)     vm_flags |= VM_STACK | VM_GROWSDOWN;
    
    /* File-backed mapping */
    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0) return MAP_FAILED;
        vm_flags |= VM_FILE;
    }
    
    /* Create VMA */
    vma_t *vma = vma_create(map_addr, map_addr + length, vm_flags);
    if (!vma) return MAP_FAILED;
    
    vma->vm_fd = fd;
    vma->vm_file_offset = offset;
    
    /* Insert VMA */
    if (vma_insert(mm, vma) < 0) {
        vma_destroy(vma);
        return MAP_FAILED;
    }
    
    /* If MAP_LOCKED, fault in all pages now */
    if (flags & MAP_LOCKED) {
        for (uint32_t a = map_addr; a < map_addr + length; a += PAGE_SIZE) {
            handle_page_fault(a, PF_WRITE);  /* Force fault-in */
        }
    }
    
    serial_printf("[MMAP] Mapped 0x%x-0x%x flags=0x%x fd=%d\n",
                  map_addr, map_addr + length, vm_flags, fd);
    
    return (void *)map_addr;
}

/**
 * System call: munmap
 */
int sys_munmap(void *addr, uint32_t length) {
    extern mm_struct_t *current_mm;
    mm_struct_t *mm = current_mm;
    if (!mm) return -1;
    
    uint32_t start = (uint32_t)addr & ~(PAGE_SIZE - 1);
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t end = start + length;
    
    /* Find and remove overlapping VMAs */
    vma_t *vma = mm->vmas;
    while (vma) {
        vma_t *next = vma->vm_next;
        
        if (vma->vm_end <= start || vma->vm_start >= end) {
            /* No overlap */
            vma = next;
            continue;
        }
        
        /* Handle partial overlaps */
        if (vma->vm_start >= start && vma->vm_end <= end) {
            /* VMA completely within unmap range - remove entirely */
            for (uint32_t a = vma->vm_start; a < vma->vm_end; a += PAGE_SIZE) {
                uint32_t phys = vmm_virt_to_phys(a);
                if (phys) {
                    vmm_unmap_page(a);
                    pmm_free_page(phys);
                    mm->rss--;
                }
            }
            vma_remove(mm, vma);
            vma_destroy(vma);
        } else {
            /* Partial overlap - split VMA */
            /* TODO: Implement VMA splitting */
            serial_printf("[MUNMAP] TODO: VMA splitting not implemented\n");
        }
        
        vma = next;
    }
    
    serial_printf("[MUNMAP] Unmapped 0x%x-0x%x\n", start, end);
    return 0;
}

/**
 * System call: mprotect
 */
int sys_mprotect(void *addr, uint32_t length, int prot) {
    extern mm_struct_t *current_mm;
    mm_struct_t *mm = current_mm;
    if (!mm) return -1;
    
    uint32_t start = (uint32_t)addr & ~(PAGE_SIZE - 1);
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    vma_t *vma = vma_find(mm, start);
    if (!vma || vma->vm_start > start || vma->vm_end < start + length) {
        return -1;  /* Address not mapped */
    }
    
    /* Update VMA flags */
    uint32_t new_flags = vma->vm_flags & ~(VM_READ | VM_WRITE | VM_EXEC);
    if (prot & PROT_READ)  new_flags |= VM_READ;
    if (prot & PROT_WRITE) new_flags |= VM_WRITE;
    if (prot & PROT_EXEC)  new_flags |= VM_EXEC;
    vma->vm_flags = new_flags;
    
    /* Update page table entries */
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if (prot & PROT_WRITE) page_flags |= PAGE_WRITE;
    
    for (uint32_t a = start; a < start + length; a += PAGE_SIZE) {
        vmm_set_page_flags(a, page_flags);
    }
    
    /* Flush TLB */
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
    
    return 0;
}

/**
 * System call: msync
 */
int sys_msync(void *addr, uint32_t length, int flags) {
    extern mm_struct_t *current_mm;
    mm_struct_t *mm = current_mm;
    if (!mm) return -1;
    
    uint32_t start = (uint32_t)addr & ~(PAGE_SIZE - 1);
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    vma_t *vma = vma_find(mm, start);
    if (!vma || !(vma->vm_flags & VM_FILE)) {
        return -1;  /* Not a file mapping */
    }
    
    /* Sync dirty pages to file via page cache */
    vmm_page_cache_sync(vma->vm_file_inode);
    
    serial_printf("[MSYNC] Synced 0x%x-0x%x\n", start, start + length);
    return 0;
}

/**
 * System call: brk
 */
void* sys_brk(void *addr) {
    extern mm_struct_t *current_mm;
    mm_struct_t *mm = current_mm;
    if (!mm) return (void *)-1;
    
    uint32_t new_brk = (uint32_t)addr;
    
    if (new_brk == 0) {
        /* Return current brk */
        return (void *)mm->heap_end;
    }
    
    /* Align to page boundary */
    new_brk = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    if (new_brk < mm->heap_start) {
        return (void *)-1;  /* Can't shrink below heap start */
    }
    
    if (new_brk > mm->stack_start - 0x100000) {
        return (void *)-1;  /* Too close to stack */
    }
    
    if (new_brk > mm->heap_end) {
        /* Expand heap - create VMA if needed */
        vma_t *heap_vma = vma_find(mm, mm->heap_start);
        if (heap_vma) {
            heap_vma->vm_end = new_brk;
        } else {
            heap_vma = vma_create(mm->heap_end, new_brk, 
                                  VM_READ | VM_WRITE | VM_ANONYMOUS | VM_LAZY);
            if (!heap_vma) return (void *)-1;
            vma_insert(mm, heap_vma);
        }
    }
    /* Shrinking: pages will be freed on next munmap or at process exit */
    
    mm->heap_end = new_brk;
    return (void *)new_brk;
}
