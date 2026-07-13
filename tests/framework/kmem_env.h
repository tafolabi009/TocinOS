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

#endif // KMEM_ENV_H
