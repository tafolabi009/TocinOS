/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../framework/unittest.h"
#include <stdint.h>

// External mock functions
extern uint32_t pmm_alloc_page(void);
extern void pmm_free_page(uint32_t addr);
extern void pmm_init(void);

/**
 * Test suite for Physical Memory Manager
 */
TEST_SUITE(pmm_tests)
    
    // Test basic allocation
    TEST_CASE(allocation_basic)
        pmm_init();
        
        uint32_t page1 = pmm_alloc_page();
        ASSERT_NE(page1, 0, "Should allocate valid page");
        
        uint32_t page2 = pmm_alloc_page();
        ASSERT_NE(page2, 0, "Should allocate valid page");
        ASSERT_NE(page1, page2, "Should allocate different pages");
        
        pmm_free_page(page1);
        uint32_t page3 = pmm_alloc_page();
        ASSERT_EQ(page3, page1, "Should reuse freed page");
    END_TEST_CASE()
    
    // Test allocation and freeing
    TEST_CASE(allocation_free)
        pmm_init();
        
        uint32_t pages[10];
        for (int i = 0; i < 10; i++) {
            pages[i] = pmm_alloc_page();
            ASSERT_NE(pages[i], 0, "Should allocate page");
        }
        
        // Free all pages
        for (int i = 0; i < 10; i++) {
            pmm_free_page(pages[i]);
        }
        
        // Should be able to allocate again
        uint32_t new_page = pmm_alloc_page();
        ASSERT_NE(new_page, 0, "Should allocate after freeing");
    END_TEST_CASE()
    
    // Test out of memory
    TEST_CASE(allocation_oom)
        pmm_init();
        
        uint32_t pages[1024];
        int count = 0;
        
        // Allocate all available pages
        while (count < 1024) {
            pages[count] = pmm_alloc_page();
            if (pages[count] == 0)
                break;
            count++;
        }
        
        ASSERT_GT(count, 0, "Should allocate at least one page");
        
        // Try to allocate one more (should fail)
        uint32_t fail_page = pmm_alloc_page();
        ASSERT_EQ(fail_page, 0, "Should return 0 when out of memory");
        
        // Free all pages
        for (int i = 0; i < count; i++) {
            pmm_free_page(pages[i]);
        }
    END_TEST_CASE()
    
    // Test page alignment
    TEST_CASE(allocation_alignment)
        pmm_init();
        
        for (int i = 0; i < 10; i++) {
            uint32_t page = pmm_alloc_page();
            ASSERT_EQ(page % 0x1000, 0, "Page should be 4KB aligned");
            pmm_free_page(page);
        }
    END_TEST_CASE()

END_TEST_SUITE()
