/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UNITTEST_H
#define UNITTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test result tracking
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    const char *current_suite;
    const char *current_test;
} test_results_t;

extern test_results_t test_results;

// Color codes for output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"

// Test suite macro
#define TEST_SUITE(name) \
    void test_suite_##name(void); \
    void test_suite_##name(void) { \
        test_results.current_suite = #name; \
        printf("\n" COLOR_BLUE "=== Test Suite: %s ===" COLOR_RESET "\n", #name);

#define END_TEST_SUITE() \
    }

// Test case macro
#define TEST_CASE(name) \
    do { \
        test_results.current_test = #name; \
        test_results.total_tests++; \
        int test_failed = 0; \
        printf("  [TEST] %s ... ", #name);

#define END_TEST_CASE() \
        if (!test_failed) { \
            test_results.passed_tests++; \
            printf(COLOR_GREEN "PASS" COLOR_RESET "\n"); \
        } \
    } while(0);

// Assertion macros
#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            test_failed = 1; \
            test_results.failed_tests++; \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, msg); \
            printf("    Expected: %ld, Got: %ld\n", (long)(b), (long)(a)); \
        } \
    } while(0)

#define ASSERT_NE(a, b, msg) \
    do { \
        if ((a) == (b)) { \
            test_failed = 1; \
            test_results.failed_tests++; \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, msg); \
            printf("    Values should not be equal: %ld\n", (long)(a)); \
        } \
    } while(0)

#define ASSERT_GT(a, b, msg) \
    do { \
        if ((a) <= (b)) { \
            test_failed = 1; \
            test_results.failed_tests++; \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, msg); \
            printf("    Expected %ld > %ld\n", (long)(a), (long)(b)); \
        } \
    } while(0)

#define ASSERT_LT(a, b, msg) \
    do { \
        if ((a) >= (b)) { \
            test_failed = 1; \
            test_results.failed_tests++; \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, msg); \
            printf("    Expected %ld < %ld\n", (long)(a), (long)(b)); \
        } \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            test_failed = 1; \
            test_results.failed_tests++; \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, msg); \
            printf("    Condition is false\n"); \
        } \
    } while(0)

#define ASSERT_FALSE(cond, msg) \
    do { \
        if (cond) { \
            test_failed = 1; \
            test_results.failed_tests++; \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, msg); \
            printf("    Condition is true\n"); \
        } \
    } while(0)

// Test runner functions
void test_init(void);
void test_summary(void);
int test_run_all(void);

#endif // UNITTEST_H
