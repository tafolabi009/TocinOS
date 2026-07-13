/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL virtual memory manager (kernel/mm/vmm.c),
 * compiled directly into the test runner — no mocks.
 *
 * kmem_env_reset() maps the fixed windows the kernel VMM writes to
 * (page directory at 0x9C000, PMM frames from 0x200000) into the host
 * process, so the unmodified 32-bit paging code can run here.
 *
 * Note: vmm_init()/vmm_switch_directory() are NOT called — they load
 * CR3, which is privileged. The directory window starts zeroed, which
 * is exactly the state vmm_map_page() expects for absent tables.
 */

#include "../framework/unittest.h"
#include "../framework/kmem_env.h"
#include "../../include/kernel/memory.h"

TEST_SUITE(vmm_tests)

    // Map one page, translate it back, verify frame lookup
    TEST_CASE(mapping_basic)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        unsigned int virt = 0x40000000u;   /* dir slot 256, fresh table */
        unsigned int phys = 0x00345000u;

        vmm_map_page(virt, phys, PAGE_WRITE);
        ASSERT_EQ(vmm_get_physical(virt), phys, "Translation returns mapped frame");
    END_TEST_CASE()

    // Mapping into an empty 4MB region allocates a page table from the PMM
    TEST_CASE(table_allocation)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        unsigned int free_before = pmm_get_free_pages();
        vmm_map_page(0x40000000u, 0x00500000u, PAGE_WRITE);
        ASSERT_EQ(pmm_get_free_pages(), free_before - 1,
                  "One frame consumed for the new page table");

        // Second mapping in the same 4MB region reuses the table
        vmm_map_page(0x40001000u, 0x00501000u, PAGE_WRITE);
        ASSERT_EQ(pmm_get_free_pages(), free_before - 1,
                  "Existing page table is reused");
    END_TEST_CASE()

    // Many mappings across distinct pages translate independently
    TEST_CASE(mapping_multiple)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        for (unsigned int i = 0; i < 10; i++) {
            unsigned int virt = 0x40000000u + (i * 0x1000u);
            unsigned int phys = 0x00600000u + (i * 0x1000u);
            vmm_map_page(virt, phys, PAGE_WRITE);
        }

        for (unsigned int i = 0; i < 10; i++) {
            unsigned int virt = 0x40000000u + (i * 0x1000u);
            unsigned int phys = 0x00600000u + (i * 0x1000u);
            ASSERT_EQ(vmm_get_physical(virt), phys, "Each mapping translates back");
        }
    END_TEST_CASE()

    // Unmapping removes the translation; remapping restores it
    TEST_CASE(unmapping)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        unsigned int virt = 0x40000000u;
        unsigned int phys = 0x00345000u;

        vmm_map_page(virt, phys, PAGE_WRITE);
        ASSERT_EQ(vmm_get_physical(virt), phys, "Mapped before unmap");

        vmm_unmap_page(virt);
        ASSERT_EQ(vmm_get_physical(virt), 0u, "Unmapped page does not translate");

        vmm_map_page(virt, phys, PAGE_WRITE);
        ASSERT_EQ(vmm_get_physical(virt), phys, "Remap after unmap works");
    END_TEST_CASE()

    // Unmapped addresses and untouched regions translate to 0
    TEST_CASE(mapping_flags)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        ASSERT_EQ(vmm_get_physical(0x40000000u), 0u,
                  "No translation in a fresh address space");

        vmm_map_page(0x40000000u, 0x00345000u, 0);
        ASSERT_EQ(vmm_get_physical(0x40000000u), 0x00345000u,
                  "Read-only (no extra flags) mapping still present");

        vmm_map_page(0x40001000u, 0x00346000u, PAGE_WRITE | PAGE_USER);
        ASSERT_EQ(vmm_get_physical(0x40001000u), 0x00346000u,
                  "User+write mapping translates");

        // Flags must not leak into the returned frame address
        ASSERT_EQ(vmm_get_physical(0x40001000u) & 0xFFFu, 0u,
                  "Returned frame is page aligned");
    END_TEST_CASE()

    // Roadmap bug #7: vmm_map_range/vmm_unmap_range/vmm_is_mapped/
    // vmm_get_page_flags were declared in memory.h but never implemented.
    TEST_CASE(range_functions)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        ASSERT_EQ(vmm_map_range(0x40000000u, 0x00600000u, 3 * 0x1000u,
                                PAGE_WRITE | PAGE_USER), 0,
                  "map_range succeeds");
        for (unsigned int i = 0; i < 3; i++) {
            unsigned int va = 0x40000000u + i * 0x1000u;
            ASSERT_EQ(vmm_is_mapped(va), 1, "each page reports mapped");
            ASSERT_EQ(vmm_get_physical(va), 0x00600000u + i * 0x1000u,
                      "each page translates to its frame");
        }
        ASSERT_EQ(vmm_is_mapped(0x40003000u), 0, "page past the range unmapped");

        int fl = vmm_get_page_flags(0x40000000u);
        ASSERT_NE(fl, -1, "flags readable for a mapped page");
        ASSERT_NE(fl & PAGE_PRESENT, 0, "present bit reported");
        ASSERT_NE(fl & PAGE_WRITE, 0, "write bit reported");
        ASSERT_NE(fl & PAGE_USER, 0, "user bit reported");
        ASSERT_EQ(vmm_get_page_flags(0x50000000u), -1,
                  "flags of an unmapped page report -1");

        // Misaligned / empty arguments are rejected
        ASSERT_EQ(vmm_map_range(0x40000123u, 0x00600000u, 0x1000u, 0), -1,
                  "misaligned virtual start rejected");
        ASSERT_EQ(vmm_map_range(0x40000000u, 0x00600123u, 0x1000u, 0), -1,
                  "misaligned physical start rejected");
        ASSERT_EQ(vmm_map_range(0x40000000u, 0x00600000u, 0, 0), -1,
                  "zero size rejected");
        ASSERT_EQ(vmm_unmap_range(0x40000123u, 0x1000u), -1,
                  "misaligned unmap rejected");

        ASSERT_EQ(vmm_unmap_range(0x40000000u, 3 * 0x1000u), 0,
                  "unmap_range succeeds");
        for (unsigned int i = 0; i < 3; i++) {
            ASSERT_EQ(vmm_is_mapped(0x40000000u + i * 0x1000u), 0,
                      "unmap_range removed every page");
        }
    END_TEST_CASE()

    // vmm_mark_cow downgrades to read-only + PAGE_COW; set_page_flags
    // can restore write access
    TEST_CASE(cow_marking)
        ASSERT_EQ(kmem_env_reset(), 0, "Host memory windows must map");
        pmm_init();

        ASSERT_EQ(vmm_mark_cow(0x40000000u), -1,
                  "marking an unmapped page fails");

        vmm_map_page(0x40000000u, 0x00345000u, PAGE_WRITE | PAGE_USER);
        ASSERT_EQ(vmm_mark_cow(0x40000000u), 0, "marking a mapped page works");

        int fl = vmm_get_page_flags(0x40000000u);
        ASSERT_EQ(fl & PAGE_WRITE, 0, "write bit cleared");
        ASSERT_NE(fl & PAGE_COW, 0, "COW bit set");
        ASSERT_NE(fl & PAGE_USER, 0, "user bit preserved");
        ASSERT_EQ(vmm_get_physical(0x40000000u), 0x00345000u,
                  "frame preserved across the mark");

        ASSERT_EQ(vmm_mark_cow(0x40000000u), 0, "re-marking is idempotent");

        ASSERT_EQ(vmm_set_page_flags(0x40000000u,
                                     PAGE_PRESENT | PAGE_WRITE | PAGE_USER),
                  0, "set_page_flags succeeds");
        fl = vmm_get_page_flags(0x40000000u);
        ASSERT_NE(fl & PAGE_WRITE, 0, "write restored");
        ASSERT_EQ(fl & PAGE_COW, 0, "COW cleared");
        ASSERT_EQ(vmm_set_page_flags(0x50000000u, PAGE_PRESENT), -1,
                  "set_page_flags on an unmapped page fails");
    END_TEST_CASE()

END_TEST_SUITE()
