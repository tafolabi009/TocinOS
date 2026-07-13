/**
 * TocinOS Demand Paging (M2 rewrite)
 *
 * Page-fault-driven anonymous paging + COW resolution.
 *
 * Audit notes on the original AI-generated version of this file: it
 * hardcoded the page directory at 0x9C000, wired swap-in into the fault
 * path (out of M2 scope), "resolved" COW faults by always copying while
 * leaking the old frame (refcounts were a TODO), and answered every
 * unresolved user fault with cli;hlt — killing the machine instead of
 * the task. It was rewritten around a host-testable resolver core.
 *
 * Layering:
 *   mm_resolve_fault()  — pure resolution logic on an mm_struct; no CR2,
 *                         no inline asm; compiled into the host unit
 *                         tests (tests/unit/test_demand.c).
 *   page_fault_isr()    — thin i386-only glue: reads CR2, calls the
 *                         resolver for current_mm, kills the faulting
 *                         user task via the spawn-exit path on failure,
 *                         panics with a register dump for kernel faults.
 */

#include "../../include/kernel/memory.h"
#include "../../include/kernel/kernel.h"
#include "../../include/kernel/isr.h"

extern void serial_printf(const char *fmt, ...);

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Memory descriptor of the currently running process. Set by
 * process_init() once the init process exists; NULL before that (any
 * fault then is a kernel bug and panics). */
mm_struct_t *current_mm = NULL;

/* Cumulative fault statistics. */
static struct {
    uint32_t total_faults;
    uint32_t minor_faults;    /* resolved without I/O (demand-zero) */
    uint32_t major_faults;    /* resolved with I/O (none in M2 scope) */
    uint32_t cow_faults;      /* COW breaks (copy or in-place flip) */
    uint32_t segfaults;       /* unresolvable accesses */
} pf_stats = {0, 0, 0, 0, 0};

// ==================== FRAME HELPERS ====================
/* User frames come from the PMM window [0, 16MB), identity-mapped at
 * boot (and MAP_FIXED-mapped by the host test harness), so frames are
 * written through their physical address. */

static void zero_frame(uint32_t phys) {
    uint32_t *p = (uint32_t *)phys;
    for (int i = 0; i < 1024; i++) {
        p[i] = 0;
    }
}

static void copy_frame(uint32_t dst_phys, uint32_t src_phys) {
    uint32_t *dst = (uint32_t *)dst_phys;
    const uint32_t *src = (const uint32_t *)src_phys;
    for (int i = 0; i < 1024; i++) {
        dst[i] = src[i];
    }
}

// ==================== RESOLUTION CORE ====================

/**
 * Demand-zero: first touch of an anonymous page. Allocate a frame from
 * the PMM (user frames stay in the bitmap window per the allocator
 * ownership split), zero it, map it with the VMA's protections.
 */
static pf_result_t demand_zero(mm_struct_t *mm, vma_t *vma, uint32_t page) {
    uint32_t phys = pmm_alloc_page();
    if (!phys) {
        return PF_OOM;
    }
    zero_frame(phys);

    uint32_t fl = PAGE_USER;
    if (vma->vm_flags & VM_WRITE) {
        fl |= PAGE_WRITE;
    }
    if (vmm_dir_map_page(mm->page_directory, page, phys, fl) < 0) {
        pmm_free_page(phys);
        return PF_OOM;
    }
    frame_ref_set(phys, 1);

    mm->rss++;
    pf_stats.minor_faults++;
    return PF_HANDLED;
}

/**
 * COW break: write hit a present read-only PAGE_COW entry.
 *  - refcount > 1: allocate a private copy, map it writable, drop one
 *    reference on the shared frame;
 *  - refcount <= 1: last owner — flip the entry writable in place.
 */
static pf_result_t resolve_cow(mm_struct_t *mm, uint32_t page, uint32_t pte) {
    uint32_t old_phys = pte & ~0xFFFu;
    uint32_t keep = pte & (uint32_t)PAGE_USER;

    if (frame_ref_get(old_phys) > 1) {
        uint32_t new_phys = pmm_alloc_page();
        if (!new_phys) {
            return PF_OOM;
        }
        copy_frame(new_phys, old_phys);
        if (vmm_dir_map_page(mm->page_directory, page, new_phys,
                             keep | PAGE_WRITE) < 0) {
            pmm_free_page(new_phys);
            return PF_OOM;
        }
        frame_ref_set(new_phys, 1);
        frame_ref_drop(old_phys);  /* >1 before, so never reaches 0 here */
        /* rss unchanged: the page was already resident for this mm. */
    } else {
        /* Sole owner: make it writable in place, clear PAGE_COW. */
        if (vmm_dir_map_page(mm->page_directory, page, old_phys,
                             keep | PAGE_WRITE) < 0) {
            return PF_OOM;
        }
        frame_ref_set(old_phys, 1);  /* now tracked, single owner */
    }

    pf_stats.cow_faults++;
    return PF_HANDLED;
}

/**
 * Core page-fault resolution — see memory.h for the contract. Operates
 * on mm->page_directory through the explicit-directory VMM API, so the
 * ISR glue stays a five-line wrapper and the whole path runs host-side.
 */
pf_result_t mm_resolve_fault(mm_struct_t *mm, uint32_t fault_addr,
                             uint32_t error_code) {
    pf_stats.total_faults++;

    if (!mm) {
        pf_stats.segfaults++;
        return PF_SIGSEGV;
    }
    if (error_code & PF_RESERVED) {
        pf_stats.segfaults++;
        return PF_SIGSEGV;  /* corrupt PTE — never auto-fixable */
    }

    uint32_t page = fault_addr & ~0xFFFu;

    vma_t *vma = vma_find(mm, fault_addr);
    if (!vma) {
        pf_stats.segfaults++;
        return PF_SIGSEGV;
    }

    if ((error_code & PF_WRITE) && !(vma->vm_flags & VM_WRITE)) {
        pf_stats.segfaults++;
        return PF_SIGSEGV;  /* write to read-only mapping */
    }
    if ((error_code & PF_USER) &&
        !(vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC))) {
        pf_stats.segfaults++;
        return PF_SIGSEGV;  /* PROT_NONE region */
    }

    uint32_t pte = vmm_dir_get_pte(mm->page_directory, page);

    if ((error_code & PF_PRESENT) && (pte & PAGE_PRESENT)) {
        /* Protection-level fault on a present page. */
        if (error_code & PF_WRITE) {
            if (pte & PAGE_COW) {
                return resolve_cow(mm, page, pte);
            }
            if (!(pte & PAGE_WRITE)) {
                /* VMA allows writes but the PTE is RO without COW
                 * (e.g. after mprotect upgrade): make it writable. */
                uint32_t fl = (pte & 0xFFFu) | PAGE_WRITE;
                vmm_dir_map_page(mm->page_directory, page,
                                 pte & ~0xFFFu, fl);
                return PF_HANDLED;
            }
            return PF_HANDLED;  /* stale TLB — already writable */
        }
        if ((error_code & PF_USER) && !(pte & PAGE_USER)) {
            pf_stats.segfaults++;
            return PF_SIGSEGV;  /* user touch of a supervisor page */
        }
        return PF_HANDLED;  /* stale TLB read */
    }

    /* Not-present fault inside a valid VMA. */
    if (pte & PAGE_PRESENT) {
        /* Error code says not-present but the PTE is live: stale TLB or
         * a lost race — never demand-zero over an existing frame. */
        return PF_HANDLED;
    }
    if (vma->vm_flags & VM_FILE) {
        /* File paging needs the page cache — deliberately not wired in
         * M2's anonymous scope (sys_mmap refuses fd-backed maps). */
        pf_stats.segfaults++;
        return PF_SIGBUS;
    }

    return demand_zero(mm, vma, page);
}

/**
 * Get page fault statistics.
 */
void demand_paging_stats(uint32_t *total, uint32_t *minor, uint32_t *major,
                         uint32_t *cow, uint32_t *segv) {
    if (total) *total = pf_stats.total_faults;
    if (minor) *minor = pf_stats.minor_faults;
    if (major) *major = pf_stats.major_faults;
    if (cow)   *cow   = pf_stats.cow_faults;
    if (segv)  *segv  = pf_stats.segfaults;
}

/** Host tests: reset counters between cases. */
void demand_paging_stats_reset(void) {
    pf_stats.total_faults = 0;
    pf_stats.minor_faults = 0;
    pf_stats.major_faults = 0;
    pf_stats.cow_faults = 0;
    pf_stats.segfaults = 0;
}

// ==================== ISR GLUE (i386 only) ====================

#ifdef __i386__

extern int sys_exit(uint32_t code);

/**
 * #PF (vector 14) handler. CR2 is 32 bits wide on i386 — this glue is
 * compiled only for __i386__, so reading it into a uint32_t is exact
 * (the old code's CR2-into-uint32_t was only a latent x86-64 hazard).
 */
static void page_fault_isr(registers_t *regs) {
    uint32_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

    pf_result_t result = mm_resolve_fault(current_mm, fault_addr,
                                          regs->err_code);
    if (result == PF_HANDLED) {
        return;
    }

    int user_mode = ((regs->cs & 3) == 3) || (regs->err_code & PF_USER);

    serial_printf("[PF] UNRESOLVED fault addr=0x%x err=0x%x "
                  "(P=%d W=%d U=%d) EIP=0x%x result=%d\n",
                  fault_addr, regs->err_code,
                  (regs->err_code & PF_PRESENT) ? 1 : 0,
                  (regs->err_code & PF_WRITE) ? 1 : 0,
                  (regs->err_code & PF_USER) ? 1 : 0,
                  regs->eip, (int)result);

    if (user_mode) {
        /* Kill only the faulting user task: exit code 139 = SIGSEGV.
         * sys_exit() unwinds to the spawn return context (the same path
         * every user program exits through today). */
        serial_printf("[PF] SIGSEGV: killing user task (exit 139)\n");
        sys_exit(139);
        /* sys_exit returns only when no spawn context exists. */
    }

    /* Kernel fault (or unkillable task): panic with a clear dump. */
    serial_printf("[PF] KERNEL PANIC: unhandled page fault in kernel context\n");
    serial_printf("[PF]  EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x\n",
                  regs->eax, regs->ebx, regs->ecx, regs->edx);
    serial_printf("[PF]  ESI=0x%x EDI=0x%x EBP=0x%x ESP=0x%x\n",
                  regs->esi, regs->edi, regs->ebp, regs->esp);
    serial_printf("[PF]  EIP=0x%x CS=0x%x EFLAGS=0x%x CR2=0x%x\n",
                  regs->eip, regs->cs, regs->eflags, fault_addr);
    kernel_print("\n*** KERNEL PAGE FAULT - system halted ***\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

/**
 * Arm demand paging: frame refcount table + #PF vector. Swap and the
 * VMM page cache are deliberately NOT initialized (dormant in M2).
 */
void demand_paging_init(void) {
    frame_ref_init();
    isr_register_handler(14, page_fault_isr);
    serial_printf("[MM] demand paging armed (#PF -> VMA fault resolver)\n");
}

#endif /* __i386__ */
