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

// External mock functions
extern int task_create(const char *name, void (*entry)(void), int priority);
extern int scheduler_get_task_count(void);
extern void scheduler_reset(void);

// Mock task entry point
void dummy_task(void) {
    // Do nothing
}

/**
 * Test suite for Scheduler
 */
TEST_SUITE(scheduler_tests)
    
    // Test task creation
    TEST_CASE(task_creation)
        scheduler_reset();
        
        int tid = task_create("test_task", dummy_task, 10);
        ASSERT_NE(tid, -1, "Should create task successfully");
        ASSERT_EQ(scheduler_get_task_count(), 1, "Should have 1 task");
    END_TEST_CASE()
    
    // Test multiple task creation
    TEST_CASE(multiple_tasks)
        scheduler_reset();
        
        for (int i = 0; i < 5; i++) {
            int tid = task_create("test_task", dummy_task, i);
            ASSERT_NE(tid, -1, "Should create task successfully");
        }
        
        ASSERT_EQ(scheduler_get_task_count(), 5, "Should have 5 tasks");
    END_TEST_CASE()
    
    // Test task limit
    TEST_CASE(task_limit)
        scheduler_reset();
        
        int count = 0;
        for (int i = 0; i < 20; i++) {
            int tid = task_create("test_task", dummy_task, 10);
            if (tid != -1) {
                count++;
            }
        }
        
        ASSERT_GT(count, 0, "Should create at least one task");
        ASSERT_LT(count, 20, "Should have a task limit");
    END_TEST_CASE()
    
    // Test different priorities
    TEST_CASE(task_priorities)
        scheduler_reset();
        
        int low_prio = task_create("low", dummy_task, 1);
        int mid_prio = task_create("mid", dummy_task, 5);
        int high_prio = task_create("high", dummy_task, 10);
        
        ASSERT_NE(low_prio, -1, "Should create low priority task");
        ASSERT_NE(mid_prio, -1, "Should create mid priority task");
        ASSERT_NE(high_prio, -1, "Should create high priority task");
        
        ASSERT_EQ(scheduler_get_task_count(), 3, "Should have 3 tasks");
    END_TEST_CASE()

END_TEST_SUITE()
