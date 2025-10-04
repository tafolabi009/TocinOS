/**
 * TocinOS Huge Pages Support
 * 
 * 2MB and 1GB huge page support
 */

#ifndef HUGEPAGE_H
#define HUGEPAGE_H

#include "../stdint.h"

// Huge page sizes
#define HUGE_PAGE_SIZE_2MB  (2 * 1024 * 1024)
#define HUGE_PAGE_SIZE_1GB  (1024 * 1024 * 1024)

// Huge page orders (for buddy allocator)
#define HUGE_PAGE_ORDER_2MB  9   // 2^9 * 4KB = 2MB
#define HUGE_PAGE_ORDER_1GB  18  // 2^18 * 4KB = 1GB

// Huge page list node
struct hugepage_list {
    struct hugepage_list *next;
    struct hugepage_list *prev;
};

// Huge page pool
typedef struct huge_page_pool {
    struct hugepage_list free_list_2mb; // Free 2MB pages
    struct hugepage_list free_list_1gb; // Free 1GB pages
    uint64_t nr_pages_2mb;              // Number of 2MB pages
    uint64_t nr_pages_1gb;              // Number of 1GB pages
    int lock;                           // Spinlock
} huge_page_pool_t;

// Function prototypes

/**
 * Initialize huge page subsystem
 */
void hugepage_init(void);

/**
 * Allocate huge page
 */
void *alloc_huge_page(int order);

/**
 * Free huge page
 */
void free_huge_page(void *addr, int order);

/**
 * Enable Transparent Huge Pages (THP)
 */
void thp_enable(void);

/**
 * Scan and promote small pages to huge pages
 */
void thp_scan_and_promote(void);

/**
 * Get huge page statistics
 */
void hugepage_get_stats(uint64_t *nr_2mb, uint64_t *nr_1gb);

/**
 * Check if huge pages are supported
 */
int hugepage_supported(void);

#endif // HUGEPAGE_H
