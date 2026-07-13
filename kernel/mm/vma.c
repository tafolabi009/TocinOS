/**
 * TocinOS Virtual Memory Area (VMA) Management (M2 rewrite)
 *
 * Per-process VMA lists + mm_struct lifecycle + the VMA-backed
 * mmap/munmap implementation. Audit notes on the original AI-generated
 * version of this file:
 *   - the list layer (sorted insert, overlap rejection, find, merge)
 *     was sound and is kept;
 *   - the mm layer was rewritten: mm_create copied higher-half kernel
 *     PDEs (768..1023) that this identity-mapped kernel does not use,
 *     mm_clone never copied page tables or bumped frame refcounts, and
 *     munmap freed shared frames blindly with no VMA splitting.
 *
 * All page-table access goes through the explicit-directory vmm_dir_*
 * API on mm->page_directory, so the same code manages the boot address
 * space (init's mm adopts the boot directory) and never-activated fork
 * children. Frame lifetimes are governed by frame_ref_* (framerefs.c):
 * a frame is returned to the PMM only when its last reference drops.
 */

#include "../../include/kernel/memory.h"
#include "../../include/kernel/kernel.h"

extern void serial_printf(const char *fmt, ...);

#ifndef NULL
#define NULL ((void *)0)
#endif

/* PDEs 0..31 identity-map [0, 128MB) for the kernel; a fork child must
 * never replace them (they are shared with the boot directory). */
#define KERNEL_IDENTITY_PDES 32u

// ==================== VMA / MM POOLS ====================

#define VMA_POOL_SIZE 256
static vma_t vma_pool[VMA_POOL_SIZE];
static vma_t *vma_free_list = NULL;
static int vma_initialized = 0;

#define MM_POOL_SIZE 64
static mm_struct_t mm_pool[MM_POOL_SIZE];
static mm_struct_t *mm_free_list = NULL;

static void vma_allocator_init(void) {
    if (vma_initialized) return;

    for (int i = 0; i < VMA_POOL_SIZE - 1; i++) {
        vma_pool[i].vm_next = &vma_pool[i + 1];
    }
    vma_pool[VMA_POOL_SIZE - 1].vm_next = NULL;
    vma_free_list = &vma_pool[0];

    for (int i = 0; i < MM_POOL_SIZE - 1; i++) {
        mm_pool[i].vmas = (vma_t *)&mm_pool[i + 1];  /* free-list link */
    }
    mm_pool[MM_POOL_SIZE - 1].vmas = NULL;
    mm_free_list = &mm_pool[0];

    vma_initialized = 1;
}

/** Reset both pools — host unit tests call this between cases. */
void vma_pools_reset(void) {
    vma_initialized = 0;
    vma_allocator_init();
}

static vma_t* vma_alloc(void) {
    if (!vma_initialized) vma_allocator_init();

    if (!vma_free_list) {
        serial_printf("[VMA] ERROR: out of VMAs\n");
        return NULL;
    }

    vma_t *vma = vma_free_list;
    vma_free_list = vma->vm_next;

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

static void vma_free(vma_t *vma) {
    if (!vma) return;
    vma->vm_next = vma_free_list;
    vma_free_list = vma;
}

// ==================== VMA MANAGEMENT ====================

vma_t* vma_create(uint32_t start, uint32_t end, uint32_t flags) {
    if (start >= end || (start & 0xFFF) || (end & 0xFFF)) {
        serial_printf("[VMA] invalid range 0x%x-0x%x\n", start, end);
        return NULL;
    }

    vma_t *vma = vma_alloc();
    if (!vma) return NULL;

    vma->vm_start = start;
    vma->vm_end = end;
    vma->vm_flags = flags;
    vma->ref_count = 1;
    return vma;
}

void vma_destroy(vma_t *vma) {
    if (!vma) return;

    vma->ref_count--;
    if (vma->ref_count == 0) {
        vma_free(vma);
    }
}

/** Find the VMA containing addr, or NULL. */
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

/** Find the lowest VMA whose end lies above addr (containing or after). */
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

/** Insert into the address-sorted list; overlaps are rejected (-1). */
int vma_insert(mm_struct_t *mm, vma_t *vma) {
    if (!mm || !vma) return -1;

    if (!mm->vmas) {
        mm->vmas = vma;
        vma->vm_next = NULL;
        vma->vm_prev = NULL;
        mm->vma_count = 1;
        mm->total_vm += (vma->vm_end - vma->vm_start);
        return 0;
    }

    vma_t *curr = mm->vmas;
    vma_t *prev = NULL;

    while (curr && curr->vm_start < vma->vm_start) {
        prev = curr;
        curr = curr->vm_next;
    }

    if (prev && prev->vm_end > vma->vm_start) {
        return -1;  /* overlaps predecessor */
    }
    if (curr && vma->vm_end > curr->vm_start) {
        return -1;  /* overlaps successor */
    }

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
    mm->total_vm += (vma->vm_end - vma->vm_start);
    return 0;
}

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

/** Merge with byte-adjacent neighbours of identical flags/backing. */
int vma_merge(mm_struct_t *mm, vma_t *vma) {
    if (!mm || !vma) return 0;

    int merged = 0;

    if (vma->vm_prev &&
        vma->vm_prev->vm_end == vma->vm_start &&
        vma->vm_prev->vm_flags == vma->vm_flags &&
        vma->vm_prev->vm_fd == vma->vm_fd) {

        vma_t *prev = vma->vm_prev;
        prev->vm_end = vma->vm_end;

        prev->vm_next = vma->vm_next;
        if (vma->vm_next) {
            vma->vm_next->vm_prev = prev;
        }

        mm->vma_count--;
        vma_free(vma);
        vma = prev;
        merged++;
    }

    if (vma->vm_next &&
        vma->vm_end == vma->vm_next->vm_start &&
        vma->vm_flags == vma->vm_next->vm_flags &&
        vma->vm_fd == vma->vm_next->vm_fd) {

        vma_t *next = vma->vm_next;
        vma->vm_end = next->vm_end;

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

// ==================== PAGE HELPERS ====================

/**
 * Unmap every present page of [start, end) from mm's directory,
 * dropping frame references and freeing frames whose last reference
 * went away.
 */
static void mm_unmap_pages(mm_struct_t *mm, uint32_t start, uint32_t end) {
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t pte = vmm_dir_get_pte(mm->page_directory, addr);
        if (!(pte & PAGE_PRESENT)) {
            continue;
        }
        uint32_t phys = pte & ~0xFFFu;
        vmm_dir_unmap_page(mm->page_directory, addr);
        if (frame_ref_drop(phys) == 0) {
            pmm_free_page(phys);
        }
        if (mm->rss > 0) {
            mm->rss--;
        }
    }
}

// ==================== MM STRUCT MANAGEMENT ====================

static mm_struct_t* mm_alloc(void) {
    if (!vma_initialized) vma_allocator_init();

    if (!mm_free_list) {
        serial_printf("[MM] ERROR: out of mm_structs\n");
        return NULL;
    }

    mm_struct_t *mm = mm_free_list;
    mm_free_list = (mm_struct_t *)mm->vmas;

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
    /* mmap area base: matches the legacy syscall placement and stays
     * clear of 0x40000000, where the ELF loader places ld.so. */
    mm->mmap_base = 0x30000000;
    mm->total_vm = 0;
    mm->rss = 0;
    mm->shared = 0;
    mm->locked = 0;
    mm->ref_count = 1;
    mm->owns_pd = 1;

    return mm;
}

static void mm_free_struct(mm_struct_t *mm) {
    if (!mm) return;
    mm->vmas = (vma_t *)mm_free_list;
    mm_free_list = mm;
}

/**
 * Create an mm with a fresh page directory. Every PDE of the CURRENT
 * directory is copied so the new space shares the kernel's identity
 * mappings (PDEs 0..31) and any live user page tables; mm_clone()
 * replaces the slots it needs with private tables afterwards.
 */
mm_struct_t* mm_create(void) {
    mm_struct_t *mm = mm_alloc();
    if (!mm) return NULL;

    mm->page_directory = pmm_alloc_page();
    if (!mm->page_directory) {
        mm_free_struct(mm);
        return NULL;
    }

    uint32_t *pd = (uint32_t *)mm->page_directory;
    const uint32_t *cur = (const uint32_t *)vmm_get_current_directory();
    for (int i = 0; i < 1024; i++) {
        pd[i] = cur[i];
    }

    return mm;
}

void mm_adopt_current_directory(mm_struct_t *mm) {
    if (!mm) return;
    if (mm->owns_pd && mm->page_directory) {
        pmm_free_page(mm->page_directory);
    }
    mm->page_directory = vmm_get_current_directory();
    mm->owns_pd = 0;
}

/**
 * Destroy an mm: drop every mapped frame of every VMA (refcounted), free
 * the page tables this mm owns, then the directory and the struct.
 *
 * Owned tables are identified by diffing against the current (boot)
 * directory: mm_create copied the boot PDEs verbatim and the boot
 * directory never replaces a present PDE, so any differing present
 * entry is a table allocated for this mm.
 */
void mm_destroy(mm_struct_t *mm) {
    if (!mm) return;

    vma_t *vma = mm->vmas;
    while (vma) {
        vma_t *next = vma->vm_next;
        mm_unmap_pages(mm, vma->vm_start, vma->vm_end);
        vma_destroy(vma);
        vma = next;
    }
    mm->vmas = NULL;
    mm->vma_count = 0;

    if (mm->owns_pd && mm->page_directory) {
        uint32_t *pd = (uint32_t *)mm->page_directory;
        const uint32_t *cur = (const uint32_t *)vmm_get_current_directory();
        for (uint32_t i = KERNEL_IDENTITY_PDES; i < 1024; i++) {
            if ((pd[i] & PAGE_PRESENT) && pd[i] != cur[i]) {
                pmm_free_page(pd[i] & ~0xFFFu);
            }
        }
        pmm_free_page(mm->page_directory);
    }
    mm->page_directory = 0;

    mm_free_struct(mm);
}

/**
 * COW clone for fork: copy the VMA list, give the child its own page
 * tables for every VMA range, share the parent's frames read-only with
 * PAGE_COW (private mappings) or verbatim (shared mappings), and bump
 * the frame refcounts. The parent's own PTEs are downgraded to
 * read-only + PAGE_COW so its next write triggers the COW break.
 */
mm_struct_t* mm_clone(mm_struct_t *src) {
    if (!src) return NULL;

    mm_struct_t *dst = mm_create();
    if (!dst) return NULL;

    dst->code_start = src->code_start;
    dst->code_end = src->code_end;
    dst->data_start = src->data_start;
    dst->data_end = src->data_end;
    dst->heap_start = src->heap_start;
    dst->heap_end = src->heap_end;
    dst->stack_start = src->stack_start;
    dst->stack_end = src->stack_end;
    dst->mmap_base = src->mmap_base;

    uint32_t *dst_pd = (uint32_t *)dst->page_directory;

    /* Pass A: clone the VMA list and clear the child's PDE slots that
     * VMAs cover, so pass B builds private page tables there instead of
     * writing through table pointers shared with the parent. Kernel
     * identity slots (0..31) are never cleared. */
    for (vma_t *sv = src->vmas; sv; sv = sv->vm_next) {
        if (sv->vm_flags & VM_DONTCOPY) {
            continue;
        }

        vma_t *dv = vma_create(sv->vm_start, sv->vm_end, sv->vm_flags);
        if (!dv) {
            mm_destroy(dst);
            return NULL;
        }
        dv->vm_file_inode = sv->vm_file_inode;
        dv->vm_file_offset = sv->vm_file_offset;
        dv->vm_fd = sv->vm_fd;
        if (vma_insert(dst, dv) < 0) {
            vma_destroy(dv);
            mm_destroy(dst);
            return NULL;
        }

        uint32_t first_pde = sv->vm_start >> 22;
        uint32_t last_pde = (sv->vm_end - 1) >> 22;
        for (uint32_t di = first_pde; di <= last_pde; di++) {
            if (di >= KERNEL_IDENTITY_PDES) {
                dst_pd[di] = 0;
            }
        }
    }

    /* Pass B: share every present page. */
    for (vma_t *sv = src->vmas; sv; sv = sv->vm_next) {
        if (sv->vm_flags & VM_DONTCOPY) {
            continue;
        }
        int private_vma = !(sv->vm_flags & VM_SHARED);

        for (uint32_t addr = sv->vm_start; addr < sv->vm_end;
             addr += PAGE_SIZE) {
            uint32_t pte = vmm_dir_get_pte(src->page_directory, addr);
            if (!(pte & PAGE_PRESENT)) {
                continue;  /* not faulted in yet — child demand-faults */
            }

            uint32_t phys = pte & ~0xFFFu;
            uint32_t fl = pte & 0xFFFu;

            if (private_vma && (sv->vm_flags & VM_WRITE)) {
                /* Downgrade the parent to RO+COW (idempotent) and give
                 * the child the same RO+COW view of the shared frame. */
                vmm_dir_mark_cow(src->page_directory, addr);
                fl = (fl & ~(uint32_t)PAGE_WRITE) | PAGE_COW;
            }

            if (vmm_dir_map_page(dst->page_directory, addr, phys, fl) < 0) {
                mm_destroy(dst);
                return NULL;
            }
            frame_ref_share(phys);
            dst->rss++;
        }
    }

    return dst;
}

void mm_release(mm_struct_t *mm) {
    if (!mm) return;

    mm->ref_count--;
    if (mm->ref_count == 0) {
        mm_destroy(mm);
    }
}

// ==================== MMAP IMPLEMENTATION ====================

static uint32_t find_free_region(mm_struct_t *mm, uint32_t size) {
    if (!mm) return 0;

    uint32_t addr = mm->mmap_base;
    vma_t *vma = mm->vmas;

    while (vma) {
        if (vma->vm_end <= addr) {
            vma = vma->vm_next;
            continue;
        }
        if (addr + size <= vma->vm_start) {
            return addr;  /* gap before this VMA */
        }
        addr = vma->vm_end;
        vma = vma->vm_next;
    }

    if (addr + size > addr && addr + size <= 0xC0000000) {
        return addr;
    }
    return 0;
}

/**
 * VMA-backed anonymous mmap: creates the mapping WITHOUT eager frames —
 * pages are faulted in on first touch by mm_resolve_fault(). File-backed
 * mappings are refused (the page cache is not wired in M2's anonymous
 * scope). MAP_LOCKED populates immediately through the same fault path.
 */
void* sys_mmap(void *addr, uint32_t length, int prot, int flags,
               int fd, uint32_t offset) {
    (void)offset;
    mm_struct_t *mm = current_mm;
    if (!mm) {
        return MAP_FAILED;
    }

    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (length == 0) return MAP_FAILED;

    /* Only anonymous mappings are supported; an fd-backed request with a
     * real fd is refused rather than silently half-mapped. */
    if (!(flags & MAP_ANONYMOUS) && fd >= 0) {
        serial_printf("[MMAP] file-backed mmap not supported (fd=%d)\n", fd);
        return MAP_FAILED;
    }

    uint32_t map_addr;
    if (flags & MAP_FIXED) {
        map_addr = (uint32_t)addr & ~(PAGE_SIZE - 1);
        if (map_addr < 0x1000 || map_addr >= 0xC0000000) {
            return MAP_FAILED;
        }
        sys_munmap((void *)map_addr, length);  /* replace existing */
    } else {
        map_addr = find_free_region(mm, length);
        if (!map_addr) {
            serial_printf("[MMAP] no free region for %u bytes\n", length);
            return MAP_FAILED;
        }
    }

    uint32_t vm_flags = VM_LAZY | VM_ANONYMOUS;
    if (prot & PROT_READ)  vm_flags |= VM_READ;
    if (prot & PROT_WRITE) vm_flags |= VM_WRITE;
    if (prot & PROT_EXEC)  vm_flags |= VM_EXEC;

    if (flags & MAP_SHARED)    vm_flags |= VM_SHARED;
    else                       vm_flags |= VM_PRIVATE;
    if (flags & MAP_LOCKED)    vm_flags |= VM_LOCKED;
    if (flags & MAP_STACK)     vm_flags |= VM_STACK | VM_GROWSDOWN;

    vma_t *vma = vma_create(map_addr, map_addr + length, vm_flags);
    if (!vma) return MAP_FAILED;

    if (vma_insert(mm, vma) < 0) {
        vma_destroy(vma);
        return MAP_FAILED;
    }

    if (flags & MAP_LOCKED) {
        uint32_t ec = (vm_flags & VM_WRITE) ? PF_WRITE : 0;
        for (uint32_t a = map_addr; a < map_addr + length; a += PAGE_SIZE) {
            if (mm_resolve_fault(mm, a, ec) != PF_HANDLED) {
                sys_munmap((void *)map_addr, length);
                return MAP_FAILED;
            }
        }
    }

    return (void *)map_addr;
}

/**
 * munmap with full VMA splitting. Frames are refcount-dropped, so
 * COW-shared frames survive until their last mapping goes away.
 */
int sys_munmap(void *addr, uint32_t length) {
    mm_struct_t *mm = current_mm;
    if (!mm) return -1;

    uint32_t start = (uint32_t)addr & ~(PAGE_SIZE - 1);
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (length == 0) return -1;
    uint32_t end = start + length;
    if (end < start) return -1;

    vma_t *vma = mm->vmas;
    while (vma) {
        vma_t *next = vma->vm_next;

        if (vma->vm_end <= start || vma->vm_start >= end) {
            vma = next;
            continue;
        }

        uint32_t vstart = vma->vm_start;
        uint32_t vend = vma->vm_end;

        if (vstart >= start && vend <= end) {
            /* fully covered — remove */
            mm_unmap_pages(mm, vstart, vend);
            vma_remove(mm, vma);
            vma_destroy(vma);
        } else if (vstart < start && vend > end) {
            /* middle punch — split into [vstart,start) + [end,vend) */
            mm_unmap_pages(mm, start, end);

            vma_t *tail = vma_create(end, vend, vma->vm_flags);
            if (!tail) return -1;
            tail->vm_file_inode = vma->vm_file_inode;
            tail->vm_file_offset = vma->vm_file_offset;
            tail->vm_fd = vma->vm_fd;

            vma->vm_end = start;
            mm->total_vm -= (vend - start);
            if (vma_insert(mm, tail) < 0) {
                vma_free(tail);
                return -1;
            }
        } else if (vstart < start) {
            /* tail overlap — trim the end */
            mm_unmap_pages(mm, start, vend);
            vma->vm_end = start;
            mm->total_vm -= (vend - start);
        } else {
            /* head overlap — trim the start */
            mm_unmap_pages(mm, vstart, end);
            vma->vm_start = end;
            mm->total_vm -= (end - vstart);
        }

        vma = next;
    }

    return 0;
}

/**
 * mprotect: update VMA flags and present PTEs. COW entries keep their
 * read-only + PAGE_COW state even when PROT_WRITE is granted — the next
 * write faults and resolves through the COW path, preserving sharing.
 */
int sys_mprotect(void *addr, uint32_t length, int prot) {
    mm_struct_t *mm = current_mm;
    if (!mm) return -1;

    uint32_t start = (uint32_t)addr & ~(PAGE_SIZE - 1);
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    vma_t *vma = vma_find(mm, start);
    if (!vma || vma->vm_start > start || vma->vm_end < start + length) {
        return -1;
    }

    uint32_t new_flags = vma->vm_flags & ~(VM_READ | VM_WRITE | VM_EXEC);
    if (prot & PROT_READ)  new_flags |= VM_READ;
    if (prot & PROT_WRITE) new_flags |= VM_WRITE;
    if (prot & PROT_EXEC)  new_flags |= VM_EXEC;
    vma->vm_flags = new_flags;

    for (uint32_t a = start; a < start + length; a += PAGE_SIZE) {
        uint32_t pte = vmm_dir_get_pte(mm->page_directory, a);
        if (!(pte & PAGE_PRESENT)) {
            continue;
        }
        uint32_t fl = pte & 0xFFFu;
        if (pte & PAGE_COW) {
            /* stays RO+COW; write access re-checks via the fault path */
            continue;
        }
        if (prot & PROT_WRITE) fl |= PAGE_WRITE;
        else                   fl &= ~(uint32_t)PAGE_WRITE;
        vmm_dir_map_page(mm->page_directory, a, pte & ~0xFFFu, fl);
    }

    return 0;
}

/**
 * msync: nothing to sync — file-backed mappings are refused by sys_mmap
 * while the page cache stays dormant (see audit note at file top).
 */
int sys_msync(void *addr, uint32_t length, int flags) {
    (void)addr; (void)length; (void)flags;
    mm_struct_t *mm = current_mm;
    if (!mm) return -1;
    return -1;  /* no file-backed VMAs can exist yet */
}

/**
 * brk: grow/shrink the heap VMA (demand-paged, no eager frames).
 */
void* sys_brk(void *addr) {
    mm_struct_t *mm = current_mm;
    if (!mm) return (void *)-1;

    uint32_t new_brk = (uint32_t)addr;

    if (new_brk == 0) {
        return (void *)mm->heap_end;
    }

    new_brk = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (new_brk < mm->heap_start) {
        return (void *)-1;
    }
    if (mm->stack_start && new_brk > mm->stack_start - 0x100000) {
        return (void *)-1;
    }

    if (new_brk > mm->heap_end) {
        vma_t *heap_vma = mm->heap_end > mm->heap_start
                              ? vma_find(mm, mm->heap_end - 1)
                              : NULL;
        if (heap_vma && heap_vma->vm_end == mm->heap_end) {
            heap_vma->vm_end = new_brk;
            mm->total_vm += (new_brk - mm->heap_end);
        } else {
            heap_vma = vma_create(mm->heap_end, new_brk,
                                  VM_READ | VM_WRITE | VM_PRIVATE |
                                  VM_ANONYMOUS | VM_LAZY);
            if (!heap_vma) return (void *)-1;
            if (vma_insert(mm, heap_vma) < 0) {
                vma_destroy(heap_vma);
                return (void *)-1;
            }
        }
    } else if (new_brk < mm->heap_end) {
        sys_munmap((void *)new_brk, mm->heap_end - new_brk);
    }

    mm->heap_end = new_brk;
    return (void *)new_brk;
}
