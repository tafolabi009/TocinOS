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
extern int vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
extern void vmm_unmap_page(uint32_t virt);

#define PAGE_PRESENT    0x01
#define PAGE_WRITABLE   0x02
#define PAGE_USER       0x04

/**
 * Test suite for Virtual Memory Manager
 */
TEST_SUITE(vmm_tests)
    
    // Test basic page mapping
    TEST_CASE(mapping_basic)
        uint32_t virt = 0x400000;
        uint32_t phys = 0x100000;
        
        int result = vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITABLE);
        ASSERT_EQ(result, 0, "Should successfully map page");
    END_TEST_CASE()
    
    // Test multiple mappings
    TEST_CASE(mapping_multiple)
        for (int i = 0; i < 10; i++) {
            uint32_t virt = 0x400000 + (i * 0x1000);
            uint32_t phys = 0x100000 + (i * 0x1000);
            
            int result = vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITABLE);
            ASSERT_EQ(result, 0, "Should successfully map page");
        }
    END_TEST_CASE()
    
    // Test unmapping
    TEST_CASE(unmapping)
        uint32_t virt = 0x400000;
        uint32_t phys = 0x100000;
        
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITABLE);
        vmm_unmap_page(virt);
        
        // Should be able to remap
        int result = vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITABLE);
        ASSERT_EQ(result, 0, "Should successfully remap page");
    END_TEST_CASE()
    
    // Test different flags
    TEST_CASE(mapping_flags)
        uint32_t virt = 0x400000;
        uint32_t phys = 0x100000;
        
        // Present only
        int result1 = vmm_map_page(virt, phys, PAGE_PRESENT);
        ASSERT_EQ(result1, 0, "Should map with PRESENT flag");
        
        // Present + Writable
        int result2 = vmm_map_page(virt + 0x1000, phys + 0x1000, 
                                   PAGE_PRESENT | PAGE_WRITABLE);
        ASSERT_EQ(result2, 0, "Should map with PRESENT|WRITABLE flags");
        
        // Present + Writable + User
        int result3 = vmm_map_page(virt + 0x2000, phys + 0x2000, 
                                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        ASSERT_EQ(result3, 0, "Should map with PRESENT|WRITABLE|USER flags");
    END_TEST_CASE()

END_TEST_SUITE()
