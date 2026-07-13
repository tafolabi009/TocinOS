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

// NOTE: PMM and VMM are no longer mocked — the real kernel/mm/pmm.c and
// kernel/mm/vmm.c are compiled into the test runner (see Makefile
// test-unit and tests/framework/kmem_env.c). The mocks below remain for
// subsystems that still need a host-side seam (scheduler); replacing
// them with the real code is tracked in docs/ROADMAP.md milestone M0.

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
