/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Mock bitmap for page allocation (simplified)
#define MAX_PAGES 1024
static uint32_t page_bitmap[MAX_PAGES / 32] = {0};
static uint32_t next_page = 1;

/**
 * Mock PMM: Allocate a page
 */
uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < MAX_PAGES; i++) {
        uint32_t word = i / 32;
        uint32_t bit = i % 32;
        
        if (!(page_bitmap[word] & (1 << bit))) {
            page_bitmap[word] |= (1 << bit);
            return (i + 1) * 0x1000; // Return physical address
        }
    }
    return 0; // Out of memory
}

/**
 * Mock PMM: Free a page
 */
void pmm_free_page(uint32_t addr) {
    uint32_t page_index = (addr / 0x1000) - 1;
    if (page_index < MAX_PAGES) {
        uint32_t word = page_index / 32;
        uint32_t bit = page_index % 32;
        page_bitmap[word] &= ~(1 << bit);
    }
}

/**
 * Mock PMM: Initialize
 */
void pmm_init(void) {
    memset(page_bitmap, 0, sizeof(page_bitmap));
    next_page = 1;
}

/**
 * Mock VMM: Map a page
 */
int vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    (void)virt;
    (void)phys;
    (void)flags;
    return 0; // Success
}

/**
 * Mock VMM: Unmap a page
 */
void vmm_unmap_page(uint32_t virt) {
    (void)virt;
}

/**
 * Mock scheduler: Simple task structure
 */
typedef struct {
    int id;
    int state;
    int priority;
} mock_task_t;

static mock_task_t mock_tasks[10];
static int mock_task_count = 0;

/**
 * Mock: Create task
 */
int task_create(const char *name, void (*entry)(void), int priority) {
    (void)name;
    (void)entry;
    
    if (mock_task_count < 10) {
        mock_tasks[mock_task_count].id = mock_task_count;
        mock_tasks[mock_task_count].state = 1; // RUNNING
        mock_tasks[mock_task_count].priority = priority;
        return mock_task_count++;
    }
    return -1;
}

/**
 * Mock: Get task count
 */
int scheduler_get_task_count(void) {
    return mock_task_count;
}

/**
 * Mock: Reset scheduler
 */
void scheduler_reset(void) {
    mock_task_count = 0;
    memset(mock_tasks, 0, sizeof(mock_tasks));
}
