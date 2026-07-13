/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL VMA layer (kernel/mm/vma.c), compiled directly
 * into the test runner — no mocks. Covers the M2 acceptance list:
 * region add/remove/lookup/overlap handling, plus the VMA-backed
 * mmap/munmap (no eager frames, refcounted frees, VMA splitting).
 *
 * kmem_env_reset() maps the fixed windows the kernel MM code writes to
 * (page directory at 0x9C000, PMM frames from 0x200000) into the host
 * process, so page directories/tables built by mm_create()/mm_clone()
 * are real memory here.
 */

#include "../framework/unittest.h"
#include "../framework/kmem_env.h"
#include "../../include/kernel/memory.h"

/* Test-only hooks exported by the mm sources. */
extern void vma_pools_reset(void);
extern void demand_paging_stats_reset(void);

static mm_struct_t *fresh_mm(void) {
    kmem_env_reset();
    pmm_init();
    vma_pools_reset();
    frame_ref_init();
    demand_paging_stats_reset();
    current_mm = NULL;
    return mm_create();
}

TEST_SUITE(vma_tests)

    // Sorted insert, lookup, and overlap rejection
    TEST_CASE(insert_lookup_overlap)
        mm_struct_t *mm = fresh_mm();
        ASSERT_NE(mm, NULL, "mm_create must succeed");

        vma_t *a = vma_create(0x40000000u, 0x40003000u, VM_READ | VM_WRITE);
        vma_t *b = vma_create(0x40008000u, 0x4000A000u, VM_READ);
        vma_t *c = vma_create(0x40004000u, 0x40006000u, VM_READ | VM_EXEC);
        ASSERT_NE(a, NULL, "vma_create a");
        ASSERT_NE(b, NULL, "vma_create b");
        ASSERT_NE(c, NULL, "vma_create c");

        // Insert out of order; list must come out address-sorted
        ASSERT_EQ(vma_insert(mm, b), 0, "insert b");
        ASSERT_EQ(vma_insert(mm, a), 0, "insert a");
        ASSERT_EQ(vma_insert(mm, c), 0, "insert c");
        ASSERT_EQ(mm->vma_count, 3u, "three VMAs tracked");
        ASSERT_EQ(mm->vmas, a, "list head is lowest range");
        ASSERT_EQ(mm->vmas->vm_next, c, "middle range second");
        ASSERT_EQ(mm->vmas->vm_next->vm_next, b, "highest range last");
        ASSERT_EQ(mm->total_vm,
                  0x3000u + 0x2000u + 0x2000u, "total_vm sums spans");

        // Lookup: inside, at boundaries, outside
        ASSERT_EQ(vma_find(mm, 0x40000000u), a, "find at start");
        ASSERT_EQ(vma_find(mm, 0x40002FFFu), a, "find at last byte");
        ASSERT_EQ(vma_find(mm, 0x40003000u), NULL, "end is exclusive");
        ASSERT_EQ(vma_find(mm, 0x40005123u), c, "find middle region");
        ASSERT_EQ(vma_find(mm, 0x30000000u), NULL, "find below all");
        ASSERT_EQ(vma_find_nearest(mm, 0x40006800u), b,
                  "nearest returns next region above");

        // Overlapping inserts must be rejected
        vma_t *ov1 = vma_create(0x40002000u, 0x40004000u, VM_READ);
        vma_t *ov2 = vma_create(0x40005000u, 0x40009000u, VM_READ);
        ASSERT_EQ(vma_insert(mm, ov1), -1, "overlap with predecessor rejected");
        ASSERT_EQ(vma_insert(mm, ov2), -1, "overlap spanning two rejected");
        ASSERT_EQ(mm->vma_count, 3u, "rejected inserts change nothing");
        vma_destroy(ov1);
        vma_destroy(ov2);
    END_TEST_CASE()

    // Remove keeps the list linked and the accounting exact
    TEST_CASE(remove_bookkeeping)
        mm_struct_t *mm = fresh_mm();
        ASSERT_NE(mm, NULL, "mm_create must succeed");

        vma_t *a = vma_create(0x40000000u, 0x40001000u, VM_READ);
        vma_t *b = vma_create(0x40001000u, 0x40002000u, VM_WRITE);
        vma_t *c = vma_create(0x40002000u, 0x40003000u, VM_READ);
        vma_insert(mm, a);
        vma_insert(mm, b);
        vma_insert(mm, c);

        ASSERT_EQ(vma_remove(mm, b), 0, "remove middle");
        ASSERT_EQ(mm->vma_count, 2u, "count drops");
        ASSERT_EQ(mm->total_vm, 0x2000u, "total_vm drops by span");
        ASSERT_EQ(mm->vmas, a, "head intact");
        ASSERT_EQ(a->vm_next, c, "list relinked around removal");
        ASSERT_EQ(c->vm_prev, a, "back pointer relinked");
        vma_destroy(b);

        ASSERT_EQ(vma_remove(mm, a), 0, "remove head");
        ASSERT_EQ(mm->vmas, c, "new head after head removal");
        ASSERT_EQ(c->vm_prev, NULL, "new head has no predecessor");
        vma_destroy(a);
    END_TEST_CASE()

    // Adjacent same-flag regions merge; different flags do not
    TEST_CASE(merge_adjacent)
        mm_struct_t *mm = fresh_mm();
        ASSERT_NE(mm, NULL, "mm_create must succeed");

        vma_t *a = vma_create(0x40000000u, 0x40001000u, VM_READ | VM_WRITE);
        vma_t *b = vma_create(0x40001000u, 0x40002000u, VM_READ | VM_WRITE);
        vma_t *c = vma_create(0x40002000u, 0x40003000u, VM_READ);
        vma_insert(mm, a);
        vma_insert(mm, b);
        vma_insert(mm, c);

        ASSERT_EQ(vma_merge(mm, b), 1, "merges with identical predecessor");
        ASSERT_EQ(mm->vma_count, 2u, "one VMA absorbed");
        ASSERT_EQ(a->vm_end, 0x40002000u, "predecessor grew");
        ASSERT_EQ(vma_merge(mm, a), 0, "different flags do not merge");
        ASSERT_EQ(a->vm_next, c, "read-only neighbour untouched");
    END_TEST_CASE()

    // mmap creates the VMA with NO eager frames; munmap removes it
    TEST_CASE(mmap_lazy_munmap)
        mm_struct_t *mm = fresh_mm();
        ASSERT_NE(mm, NULL, "mm_create must succeed");
        current_mm = mm;

        unsigned int free_before = pmm_get_free_pages();

        void *p = sys_mmap((void *)0, 4 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_NE(p, MAP_FAILED, "anonymous mmap succeeds");
        ASSERT_EQ((uint32_t)(uintptr_t)p, 0x30000000u,
                  "mapped at the mm's mmap base");
        ASSERT_EQ(pmm_get_free_pages(), free_before,
                  "no frames allocated eagerly");
        ASSERT_EQ(vmm_dir_get_pte(mm->page_directory, 0x30000000u), 0u,
                  "no PTE before the first fault");
        ASSERT_NE(vma_find(mm, 0x30000000u), NULL, "VMA exists");

        // fd-backed mapping refused (page cache dormant in M2)
        void *f = sys_mmap((void *)0, PAGE_SIZE, PROT_READ, MAP_PRIVATE, 3, 0);
        ASSERT_EQ(f, MAP_FAILED, "file-backed mmap is refused");

        ASSERT_EQ(sys_munmap(p, 4 * PAGE_SIZE), 0, "munmap succeeds");
        ASSERT_EQ(vma_find(mm, 0x30000000u), NULL, "VMA removed");
        ASSERT_EQ(mm->vma_count, 0u, "no VMAs left");
        current_mm = NULL;
    END_TEST_CASE()

    // munmap of a middle range splits the VMA and frees only its frames
    TEST_CASE(munmap_split_and_free)
        mm_struct_t *mm = fresh_mm();
        ASSERT_NE(mm, NULL, "mm_create must succeed");
        current_mm = mm;

        void *p = sys_mmap((void *)0, 4 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_NE(p, MAP_FAILED, "anonymous mmap succeeds");
        uint32_t base = (uint32_t)(uintptr_t)p;

        // Fault all four pages in (demand-zero)
        unsigned int free_before_pop = pmm_get_free_pages();
        for (int i = 0; i < 4; i++) {
            ASSERT_EQ(mm_resolve_fault(mm, base + i * PAGE_SIZE, PF_WRITE),
                      PF_HANDLED, "demand-zero populates page");
        }
        // 4 frames + 1 page table were consumed
        ASSERT_EQ(pmm_get_free_pages(), free_before_pop - 5,
                  "four frames and one table consumed");
        ASSERT_EQ(mm->rss, 4u, "rss counts resident pages");

        // Punch out pages 1..2 -> [0,1) and [3,4) remain
        ASSERT_EQ(sys_munmap((void *)(base + PAGE_SIZE), 2 * PAGE_SIZE), 0,
                  "middle munmap succeeds");
        ASSERT_EQ(mm->vma_count, 2u, "VMA split into two");
        ASSERT_NE(vma_find(mm, base), NULL, "head piece remains");
        ASSERT_EQ(vma_find(mm, base + PAGE_SIZE), NULL, "hole unmapped");
        ASSERT_EQ(vma_find(mm, base + 2 * PAGE_SIZE), NULL, "hole unmapped");
        ASSERT_NE(vma_find(mm, base + 3 * PAGE_SIZE), NULL, "tail piece remains");
        ASSERT_EQ(pmm_get_free_pages(), free_before_pop - 3,
                  "two frames returned, table + 2 frames still held");
        ASSERT_EQ(mm->rss, 2u, "rss drops with the freed pages");
        ASSERT_EQ(mm->total_vm, 2u * PAGE_SIZE, "total_vm tracks the split");

        // Destroying the mm releases everything it still holds
        unsigned int free_after_setup = pmm_get_free_pages();
        mm_destroy(mm);
        current_mm = NULL;
        // 2 frames + 1 page table + 1 page directory come back
        ASSERT_EQ(pmm_get_free_pages(), free_after_setup + 4,
                  "mm_destroy frees frames, table and directory");
    END_TEST_CASE()

    // MAP_FIXED replaces an existing mapping at that address
    TEST_CASE(mmap_fixed_replace)
        mm_struct_t *mm = fresh_mm();
        ASSERT_NE(mm, NULL, "mm_create must succeed");
        current_mm = mm;

        void *p = sys_mmap((void *)0x35000000u, 2 * PAGE_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        ASSERT_EQ((uint32_t)(uintptr_t)p, 0x35000000u, "fixed address honored");

        void *q = sys_mmap((void *)0x35000000u, PAGE_SIZE, PROT_READ,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        ASSERT_EQ((uint32_t)(uintptr_t)q, 0x35000000u, "refix same address");
        vma_t *v = vma_find(mm, 0x35000000u);
        ASSERT_NE(v, NULL, "replacement VMA exists");
        ASSERT_EQ(v->vm_flags & VM_WRITE, 0u, "replacement carries new prot");
        ASSERT_NE(vma_find(mm, 0x35001000u), NULL,
                  "tail of the old mapping survives as its own VMA");
        current_mm = NULL;
    END_TEST_CASE()

END_TEST_SUITE()
