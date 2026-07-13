/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Host-side memory environment for unit-testing the real kernel
 * memory managers (kernel/mm/pmm.c, kernel/mm/vmm.c).
 */

#ifndef KMEM_ENV_H
#define KMEM_ENV_H

/**
 * Map (and zero) the fixed "physical" windows the kernel MM code
 * touches, into the host test process:
 *   - the page-directory area at 0x9C000
 *   - the page-frame area from 0x200000 (first frame PMM hands out)
 *
 * Call at the start of every VMM test. Returns 0 on success.
 */
int kmem_env_reset(void);

/** Buddy allocator kernel base (BUDDY_REGION_START in memory.h). */
#define BUDDY_WINDOW_BASE 0x1000000UL /* 16MB */

/**
 * Map (and zero) `bytes` of writable memory at BUDDY_WINDOW_BASE so slab
 * tests can dereference the pages buddy_alloc() returns. Call at the
 * start of every slab test. Returns 0 on success.
 */
int kmem_env_map_buddy_window(unsigned long bytes);

#endif // KMEM_ENV_H
