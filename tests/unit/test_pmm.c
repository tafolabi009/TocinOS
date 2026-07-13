/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL physical memory manager (kernel/mm/pmm.c),
 * compiled directly into the test runner — no mocks.
 *
 * Real PMM semantics under test:
 *   - 128 MiB managed => 32768 pages of 4 KiB
 *   - pmm_init() reserves the first 2 MiB (512 pages) for BIOS/kernel
 *   - pmm_alloc_page() returns a byte address, 0 on out-of-memory
 */

#include "../framework/unittest.h"
#include "../../include/kernel/memory.h"

#define TOTAL_PAGES    32768u          /* 128 MiB / 4 KiB */
#define RESERVED_PAGES 512u            /* first 2 MiB */
#define FIRST_FREE     0x200000u       /* first allocatable address */

TEST_SUITE(pmm_tests)

    // Initialization reserves low memory and reports correct counters
    TEST_CASE(init_state)
        pmm_init();

        ASSERT_EQ(pmm_get_total_pages(), TOTAL_PAGES, "128MB should be 32768 pages");
        ASSERT_EQ(pmm_get_used_pages(), RESERVED_PAGES, "First 2MB reserved at init");
        ASSERT_EQ(pmm_get_free_pages(), TOTAL_PAGES - RESERVED_PAGES,
                  "Free = total - reserved");
    END_TEST_CASE()

    // Basic allocation: distinct pages, above reserved area, freed pages reused
    TEST_CASE(allocation_basic)
        pmm_init();

        unsigned int page1 = pmm_alloc_page();
        ASSERT_EQ(page1, FIRST_FREE, "First allocation starts after reserved 2MB");

        unsigned int page2 = pmm_alloc_page();
        ASSERT_NE(page2, 0u, "Should allocate valid page");
        ASSERT_NE(page1, page2, "Should allocate different pages");

        pmm_free_page(page1);
        unsigned int page3 = pmm_alloc_page();
        ASSERT_EQ(page3, page1, "Should reuse freed page");
    END_TEST_CASE()

    // Allocation/free bookkeeping stays consistent
    TEST_CASE(allocation_free)
        pmm_init();
        unsigned int free_before = pmm_get_free_pages();

        unsigned int pages[10];
        for (int i = 0; i < 10; i++) {
            pages[i] = pmm_alloc_page();
            ASSERT_NE(pages[i], 0u, "Should allocate page");
        }
        ASSERT_EQ(pmm_get_free_pages(), free_before - 10, "Counter tracks allocations");

        for (int i = 0; i < 10; i++) {
            pmm_free_page(pages[i]);
        }
        ASSERT_EQ(pmm_get_free_pages(), free_before, "Counter tracks frees");

        // Double-free must not corrupt the counter
        pmm_free_page(pages[0]);
        ASSERT_EQ(pmm_get_free_pages(), free_before, "Double free is a no-op");
    END_TEST_CASE()

    // Exhausting physical memory returns 0, and everything can be freed
    TEST_CASE(allocation_oom)
        pmm_init();

        unsigned int expect = pmm_get_free_pages();
        unsigned int count = 0;
        unsigned int last = 0;

        // Addresses are handed out in ascending order, so we don't need
        // to store them to free them again afterwards.
        for (;;) {
            unsigned int page = pmm_alloc_page();
            if (page == 0)
                break;
            last = page;
            count++;
        }

        ASSERT_EQ(count, expect, "Should hand out exactly the free page count");
        ASSERT_EQ(last, (TOTAL_PAGES - 1) * 0x1000u, "Last page is top of memory");
        ASSERT_EQ(pmm_alloc_page(), 0u, "Exhausted allocator returns 0");
        ASSERT_EQ(pmm_get_free_pages(), 0u, "No pages left");

        for (unsigned int p = RESERVED_PAGES; p < TOTAL_PAGES; p++) {
            pmm_free_page(p * 0x1000u);
        }
        ASSERT_EQ(pmm_get_free_pages(), expect, "All pages freed again");
    END_TEST_CASE()

    // Returned addresses are page aligned
    TEST_CASE(allocation_alignment)
        pmm_init();

        for (int i = 0; i < 10; i++) {
            unsigned int page = pmm_alloc_page();
            ASSERT_EQ(page % 0x1000u, 0u, "Page should be 4KB aligned");
            pmm_free_page(page);
        }
    END_TEST_CASE()

END_TEST_SUITE()
