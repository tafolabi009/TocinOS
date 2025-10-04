/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "framework/unittest.h"

// Declare test suites
extern void test_suite_pmm_tests(void);
extern void test_suite_vmm_tests(void);
extern void test_suite_scheduler_tests(void);
extern void test_suite_fat_tests(void);

/**
 * Main test runner
 */
int main(void) {
    // Initialize test framework
    test_init();
    
    // Run all test suites
    test_suite_pmm_tests();
    test_suite_vmm_tests();
    test_suite_scheduler_tests();
    test_suite_fat_tests();
    
    // Print summary
    test_summary();
    
    // Return exit code
    return test_run_all();
}
