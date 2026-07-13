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

END_TEST_SUITE()
