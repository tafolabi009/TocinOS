/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL demand-paging core (kernel/mm/demand.c),
 * compiled directly into the test runner — no mocks. The #PF resolution
 * core mm_resolve_fault() is deliberately factored free of CR2/ISR glue
 * so faults can be simulated here by calling it directly, per the M2
 * acceptance list:
 *   - demand-zero populates + maps
 *   - fault outside any VMA reports an error
 *   - COW fault with refcount 2 copies and decrements
 *   - COW fault with refcount 1 flips writable in place
 * plus the full mm_clone() COW-fork semantics including teardown.
 */

#include "../framework/unittest.h"
#include "../framework/kmem_env.h"
#include "../../include/kernel/memory.h"

extern void vma_pools_reset(void);
extern void demand_paging_stats_reset(void);

#define TEST_VA 0x40000000u

static mm_struct_t *fresh_mm_with_vma(uint32_t pages, uint32_t vm_flags) {
    kmem_env_reset();
    pmm_init();
    vma_pools_reset();
    frame_ref_init();
    demand_paging_stats_reset();
    current_mm = NULL;

    mm_struct_t *mm = mm_create();
    if (!mm) return NULL;

    vma_t *vma = vma_create(TEST_VA, TEST_VA + pages * PAGE_SIZE, vm_flags);
    if (!vma || vma_insert(mm, vma) < 0) return NULL;
    return mm;
}

TEST_SUITE(demand_tests)

    // Demand-zero: first touch allocates, zeroes and maps a frame
    TEST_CASE(demand_zero_populates)
        mm_struct_t *mm = fresh_mm_with_vma(2, VM_READ | VM_WRITE |
                                               VM_PRIVATE | VM_ANONYMOUS);
        ASSERT_NE(mm, NULL, "mm with anon VMA");

        unsigned int free_before = pmm_get_free_pages();
        ASSERT_EQ(vmm_dir_get_pte(mm->page_directory, TEST_VA), 0u,
                  "no PTE before the fault");

        pf_result_t r = mm_resolve_fault(mm, TEST_VA + 0x123u, PF_WRITE);
        ASSERT_EQ(r, PF_HANDLED, "not-present write inside anon VMA handled");

        uint32_t pte = vmm_dir_get_pte(mm->page_directory, TEST_VA);
        ASSERT_NE(pte & PAGE_PRESENT, 0u, "page mapped after fault");
        ASSERT_NE(pte & PAGE_WRITE, 0u, "writable VMA maps writable PTE");
        ASSERT_NE(pte & PAGE_USER, 0u, "user-accessible PTE");

        uint32_t phys = pte & ~0xFFFu;
        const uint32_t *frame = (const uint32_t *)(uintptr_t)phys;
        uint32_t acc = 0;
        for (int i = 0; i < 1024; i++) acc |= frame[i];
        ASSERT_EQ(acc, 0u, "frame is zero-filled");
        ASSERT_EQ(frame_ref_get(phys), 1u, "fresh frame has refcount 1");
        ASSERT_EQ(pmm_get_free_pages(), free_before - 2,
                  "one frame + one page table consumed");
        ASSERT_EQ(mm->rss, 1u, "rss counts the resident page");

        uint32_t minor = 0;
        demand_paging_stats(NULL, &minor, NULL, NULL, NULL);
        ASSERT_EQ(minor, 1u, "counted as a minor fault");

        // Read-only VMA maps read-only PTEs
        mm_struct_t *ro = fresh_mm_with_vma(1, VM_READ | VM_PRIVATE |
                                               VM_ANONYMOUS);
        ASSERT_NE(ro, NULL, "read-only mm");
        ASSERT_EQ(mm_resolve_fault(ro, TEST_VA, 0), PF_HANDLED,
                  "read fault inside RO VMA handled");
        uint32_t ro_pte = vmm_dir_get_pte(ro->page_directory, TEST_VA);
        ASSERT_NE(ro_pte & PAGE_PRESENT, 0u, "RO page mapped");
        ASSERT_EQ(ro_pte & PAGE_WRITE, 0u, "RO VMA maps without write bit");
    END_TEST_CASE()

    // Faults the resolver must refuse
    TEST_CASE(fault_errors)
        mm_struct_t *mm = fresh_mm_with_vma(1, VM_READ | VM_PRIVATE |
                                               VM_ANONYMOUS);
        ASSERT_NE(mm, NULL, "mm with RO anon VMA");

        ASSERT_EQ(mm_resolve_fault(mm, 0x50000000u, PF_WRITE), PF_SIGSEGV,
                  "fault outside any VMA is SIGSEGV");
        ASSERT_EQ(mm_resolve_fault(mm, TEST_VA - 4u, 0), PF_SIGSEGV,
                  "fault just below the VMA is SIGSEGV");
        ASSERT_EQ(mm_resolve_fault(mm, TEST_VA + PAGE_SIZE, 0), PF_SIGSEGV,
                  "fault just past vm_end is SIGSEGV");
        ASSERT_EQ(mm_resolve_fault(mm, TEST_VA, PF_WRITE), PF_SIGSEGV,
                  "write into a read-only VMA is SIGSEGV");
        ASSERT_EQ(mm_resolve_fault(NULL, TEST_VA, 0), PF_SIGSEGV,
                  "no mm (kernel context) is SIGSEGV");
        ASSERT_EQ(mm_resolve_fault(mm, TEST_VA, PF_RESERVED), PF_SIGSEGV,
                  "reserved-bit violation is SIGSEGV");

        uint32_t segv = 0;
        demand_paging_stats(NULL, NULL, NULL, NULL, &segv);
        ASSERT_EQ(segv, 6u, "all six refusals counted");

        // File-backed VMA: page cache not wired -> SIGBUS, never mapped
        mm_struct_t *fm = fresh_mm_with_vma(1, VM_READ | VM_WRITE |
                                               VM_PRIVATE | VM_FILE);
        ASSERT_NE(fm, NULL, "mm with file VMA");
        ASSERT_EQ(mm_resolve_fault(fm, TEST_VA, 0), PF_SIGBUS,
                  "file-backed fault is SIGBUS while page cache is dormant");
        ASSERT_EQ(vmm_dir_get_pte(fm->page_directory, TEST_VA), 0u,
                  "no mapping created for the file fault");
    END_TEST_CASE()

    // COW fault with refcount 2: copy, remap writable, decrement
    TEST_CASE(cow_refcount2_copies)
        mm_struct_t *mm = fresh_mm_with_vma(1, VM_READ | VM_WRITE |
                                               VM_PRIVATE | VM_ANONYMOUS);
        ASSERT_NE(mm, NULL, "mm with anon VMA");

        // Populate, pattern the frame, then simulate a fork share
        ASSERT_EQ(mm_resolve_fault(mm, TEST_VA, PF_WRITE), PF_HANDLED,
                  "populate via demand-zero");
        uint32_t old_pte = vmm_dir_get_pte(mm->page_directory, TEST_VA);
        uint32_t old_phys = old_pte & ~0xFFFu;
        uint32_t *old_frame = (uint32_t *)(uintptr_t)old_phys;
        for (int i = 0; i < 1024; i++) old_frame[i] = 0xA5A50000u + i;

        frame_ref_share(old_phys);                       /* now 2 */
        ASSERT_EQ(frame_ref_get(old_phys), 2u, "frame shared twice");
        ASSERT_EQ(vmm_dir_mark_cow(mm->page_directory, TEST_VA), 0,
                  "mark COW succeeds");
        uint32_t cow_pte = vmm_dir_get_pte(mm->page_directory, TEST_VA);
        ASSERT_EQ(cow_pte & PAGE_WRITE, 0u, "COW entry is read-only");
        ASSERT_NE(cow_pte & PAGE_COW, 0u, "COW bit set");

        // The write fault: present + write
        pf_result_t r = mm_resolve_fault(mm, TEST_VA + 8u,
                                         PF_PRESENT | PF_WRITE);
        ASSERT_EQ(r, PF_HANDLED, "COW fault handled");

        uint32_t new_pte = vmm_dir_get_pte(mm->page_directory, TEST_VA);
        uint32_t new_phys = new_pte & ~0xFFFu;
        ASSERT_NE(new_phys, old_phys, "refcount 2 forces a private copy");
        ASSERT_NE(new_pte & PAGE_WRITE, 0u, "copy is writable");
        ASSERT_EQ(new_pte & PAGE_COW, 0u, "copy is no longer COW");

        const uint32_t *new_frame = (const uint32_t *)(uintptr_t)new_phys;
        int identical = 1;
        for (int i = 0; i < 1024; i++) {
            if (new_frame[i] != 0xA5A50000u + (uint32_t)i) identical = 0;
        }
        ASSERT_EQ(identical, 1, "contents copied verbatim");

        ASSERT_EQ(frame_ref_get(old_phys), 1u, "old frame decremented to 1");
        ASSERT_EQ(frame_ref_get(new_phys), 1u, "new frame owned once");

        uint32_t cow = 0;
        demand_paging_stats(NULL, NULL, NULL, &cow, NULL);
        ASSERT_EQ(cow, 1u, "counted as a COW fault");
    END_TEST_CASE()

    // COW fault with refcount 1: flip writable in place, no copy
    TEST_CASE(cow_refcount1_flips)
        mm_struct_t *mm = fresh_mm_with_vma(1, VM_READ | VM_WRITE |
                                               VM_PRIVATE | VM_ANONYMOUS);
        ASSERT_NE(mm, NULL, "mm with anon VMA");

        ASSERT_EQ(mm_resolve_fault(mm, TEST_VA, PF_WRITE), PF_HANDLED,
                  "populate via demand-zero");
        uint32_t phys = vmm_dir_get_pte(mm->page_directory, TEST_VA) & ~0xFFFu;
        ASSERT_EQ(frame_ref_get(phys), 1u, "sole owner");
        vmm_dir_mark_cow(mm->page_directory, TEST_VA);

        unsigned int free_before = pmm_get_free_pages();
        pf_result_t r = mm_resolve_fault(mm, TEST_VA,
                                         PF_PRESENT | PF_WRITE);
        ASSERT_EQ(r, PF_HANDLED, "sole-owner COW fault handled");

        uint32_t pte = vmm_dir_get_pte(mm->page_directory, TEST_VA);
        ASSERT_EQ(pte & ~0xFFFu, phys, "same frame kept — no copy");
        ASSERT_NE(pte & PAGE_WRITE, 0u, "flipped writable in place");
        ASSERT_EQ(pte & PAGE_COW, 0u, "COW bit cleared");
        ASSERT_EQ(pmm_get_free_pages(), free_before,
                  "no frame allocated for the in-place flip");
        ASSERT_EQ(frame_ref_get(phys), 1u, "still a single owner");
    END_TEST_CASE()

    // Full fork semantics: mm_clone shares COW, break isolates, teardown frees
    TEST_CASE(clone_cow_fork)
        mm_struct_t *parent = fresh_mm_with_vma(2, VM_READ | VM_WRITE |
                                                   VM_PRIVATE | VM_ANONYMOUS);
        ASSERT_NE(parent, NULL, "parent mm");

        // Populate both pages with distinct patterns
        ASSERT_EQ(mm_resolve_fault(parent, TEST_VA, PF_WRITE), PF_HANDLED,
                  "populate page 0");
        ASSERT_EQ(mm_resolve_fault(parent, TEST_VA + PAGE_SIZE, PF_WRITE),
                  PF_HANDLED, "populate page 1");
        uint32_t p0 = vmm_dir_get_pte(parent->page_directory, TEST_VA) & ~0xFFFu;
        uint32_t p1 = vmm_dir_get_pte(parent->page_directory,
                                      TEST_VA + PAGE_SIZE) & ~0xFFFu;
        ((uint32_t *)(uintptr_t)p0)[0] = 0xDEAD0000u;
        ((uint32_t *)(uintptr_t)p1)[0] = 0xDEAD1111u;

        unsigned int free_before_clone = pmm_get_free_pages();
        mm_struct_t *child = mm_clone(parent);
        ASSERT_NE(child, NULL, "mm_clone succeeds");
        // Copy-page-tables-only: 1 directory + 1 page table, NO frames
        ASSERT_EQ(pmm_get_free_pages(), free_before_clone - 2,
                  "clone copies page tables only, no frame copies");

        // Both sides now read-only + COW on the same frames, refcount 2
        uint32_t ppte = vmm_dir_get_pte(parent->page_directory, TEST_VA);
        uint32_t cpte = vmm_dir_get_pte(child->page_directory, TEST_VA);
        ASSERT_EQ(ppte & ~0xFFFu, p0, "parent still points at the frame");
        ASSERT_EQ(cpte & ~0xFFFu, p0, "child shares the same frame");
        ASSERT_EQ(ppte & PAGE_WRITE, 0u, "parent downgraded to read-only");
        ASSERT_EQ(cpte & PAGE_WRITE, 0u, "child mapped read-only");
        ASSERT_NE(ppte & PAGE_COW, 0u, "parent marked COW");
        ASSERT_NE(cpte & PAGE_COW, 0u, "child marked COW");
        ASSERT_EQ(frame_ref_get(p0), 2u, "frame refcount bumped to 2");
        ASSERT_EQ(child->vma_count, 1u, "VMA list cloned");
        ASSERT_EQ(child->rss, 2u, "child rss counts shared pages");

        // Parent write -> COW break: copies and decrements
        ASSERT_EQ(mm_resolve_fault(parent, TEST_VA, PF_PRESENT | PF_WRITE),
                  PF_HANDLED, "parent COW break");
        uint32_t p0_new = vmm_dir_get_pte(parent->page_directory,
                                          TEST_VA) & ~0xFFFu;
        ASSERT_NE(p0_new, p0, "parent got a private copy");
        ((uint32_t *)(uintptr_t)p0_new)[0] = 0xBEEF0000u;
        ASSERT_EQ(((const uint32_t *)(uintptr_t)p0)[0], 0xDEAD0000u,
                  "child's view of the original frame is untouched");
        ASSERT_EQ(frame_ref_get(p0), 1u, "original frame now child-only");
        ASSERT_EQ(frame_ref_get(p0_new), 1u, "copy owned by parent alone");

        // Child teardown drops its references and frees its tables
        mm_destroy(child);
        ASSERT_EQ(frame_ref_get(p0), 0u, "child reap freed the shared frame");
        ASSERT_EQ(frame_ref_get(p1), 1u, "page 1 still owned by the parent");

        // Parent teardown returns every remaining page
        unsigned int free_after_child = pmm_get_free_pages();
        mm_destroy(parent);
        // parent held: p0_new, p1, 1 page table, 1 directory
        ASSERT_EQ(pmm_get_free_pages(), free_after_child + 4,
                  "parent teardown frees frames, table and directory");
    END_TEST_CASE()

END_TEST_SUITE()
