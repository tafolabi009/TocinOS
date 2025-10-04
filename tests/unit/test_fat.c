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
#include <string.h>

// Mock FAT structures
typedef struct {
    char name[11];
    uint32_t size;
    uint32_t first_cluster;
} fat_entry_t;

static fat_entry_t mock_entries[10];
static int mock_entry_count = 0;

/**
 * Mock: Create file entry
 */
int fat_create_file(const char *name, uint32_t size) {
    if (mock_entry_count >= 10) {
        return -1;
    }
    
    strncpy(mock_entries[mock_entry_count].name, name, 11);
    mock_entries[mock_entry_count].size = size;
    mock_entries[mock_entry_count].first_cluster = mock_entry_count + 2;
    mock_entry_count++;
    
    return 0;
}

/**
 * Mock: Find file
 */
int fat_find_file(const char *name) {
    for (int i = 0; i < mock_entry_count; i++) {
        if (strncmp(mock_entries[i].name, name, 11) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Mock: Get file size
 */
uint32_t fat_get_file_size(int index) {
    if (index >= 0 && index < mock_entry_count) {
        return mock_entries[index].size;
    }
    return 0;
}

/**
 * Mock: Reset FAT
 */
void fat_reset(void) {
    mock_entry_count = 0;
    memset(mock_entries, 0, sizeof(mock_entries));
}

/**
 * Test suite for FAT Filesystem
 */
TEST_SUITE(fat_tests)
    
    // Test file creation
    TEST_CASE(file_creation)
        fat_reset();
        
        int result = fat_create_file("TEST.TXT", 1024);
        ASSERT_EQ(result, 0, "Should create file successfully");
    END_TEST_CASE()
    
    // Test file lookup
    TEST_CASE(file_lookup)
        fat_reset();
        
        fat_create_file("TEST.TXT", 1024);
        
        int index = fat_find_file("TEST.TXT");
        ASSERT_NE(index, -1, "Should find created file");
        
        int not_found = fat_find_file("NOTHERE.TXT");
        ASSERT_EQ(not_found, -1, "Should not find non-existent file");
    END_TEST_CASE()
    
    // Test file size
    TEST_CASE(file_size)
        fat_reset();
        
        fat_create_file("SMALL.TXT", 100);
        fat_create_file("LARGE.TXT", 10000);
        
        int small_idx = fat_find_file("SMALL.TXT");
        int large_idx = fat_find_file("LARGE.TXT");
        
        ASSERT_EQ(fat_get_file_size(small_idx), 100, "Should get correct size for small file");
        ASSERT_EQ(fat_get_file_size(large_idx), 10000, "Should get correct size for large file");
    END_TEST_CASE()
    
    // Test multiple files
    TEST_CASE(multiple_files)
        fat_reset();
        
        for (int i = 0; i < 5; i++) {
            char name[12];
            snprintf(name, 12, "FILE%d.TXT", i);
            int result = fat_create_file(name, i * 100);
            ASSERT_EQ(result, 0, "Should create file");
        }
        
        // Verify all files
        for (int i = 0; i < 5; i++) {
            char name[12];
            snprintf(name, 12, "FILE%d.TXT", i);
            int idx = fat_find_file(name);
            ASSERT_NE(idx, -1, "Should find file");
            ASSERT_EQ(fat_get_file_size(idx), (uint32_t)(i * 100), "Should have correct size");
        }
    END_TEST_CASE()

END_TEST_SUITE()
