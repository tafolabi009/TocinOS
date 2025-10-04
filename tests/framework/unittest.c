/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "unittest.h"

// Global test results
test_results_t test_results = {0};

/**
 * Initialize test framework
 */
void test_init(void) {
    test_results.total_tests = 0;
    test_results.passed_tests = 0;
    test_results.failed_tests = 0;
    test_results.current_suite = NULL;
    test_results.current_test = NULL;
    
    printf("\n");
    printf("================================================\n");
    printf("  TocinOS Unit Test Framework\n");
    printf("================================================\n");
}

/**
 * Print test summary
 */
void test_summary(void) {
    printf("\n");
    printf("================================================\n");
    printf("  Test Summary\n");
    printf("================================================\n");
    printf("Total Tests:  %d\n", test_results.total_tests);
    printf("Passed:       " COLOR_GREEN "%d" COLOR_RESET "\n", test_results.passed_tests);
    printf("Failed:       " COLOR_RED "%d" COLOR_RESET "\n", test_results.failed_tests);
    
    if (test_results.failed_tests == 0) {
        printf("\n" COLOR_GREEN "All tests passed!" COLOR_RESET "\n\n");
    } else {
        printf("\n" COLOR_RED "Some tests failed!" COLOR_RESET "\n\n");
    }
}

/**
 * Run all tests and return exit code
 */
int test_run_all(void) {
    return (test_results.failed_tests == 0) ? 0 : 1;
}
