/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Host-side memory environment for unit-testing the real kernel
 * memory managers. The kernel VMM writes page tables at fixed
 * physical addresses (directory at 0x9C000, frames from PMM starting
 * at 0x200000). We MAP_FIXED anonymous memory over those ranges so
 * the unmodified kernel code can run inside the test process.
 * Re-mapping on every reset also zeroes the windows, giving each
 * test a clean slate.
 */

#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>

#include "kmem_env.h"

#define DIR_WINDOW_BASE   0x9C000UL
#define DIR_WINDOW_SIZE   0x4000UL    /* page directory + first table */
#define FRAME_WINDOW_BASE 0x200000UL  /* first frame the PMM returns */
#define FRAME_WINDOW_SIZE 0x100000UL  /* 1 MiB of allocatable frames */

static int map_fixed(unsigned long base, unsigned long size) {
    void *p = mmap((void *)base, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        perror("kmem_env: mmap MAP_FIXED failed");
        return -1;
    }
    return 0;
}

int kmem_env_reset(void) {
    if (map_fixed(DIR_WINDOW_BASE, DIR_WINDOW_SIZE)) return -1;
    if (map_fixed(FRAME_WINDOW_BASE, FRAME_WINDOW_SIZE)) return -1;
    return 0;
}
