/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Host-side stubs for the externals of the REAL scheduler
 * (kernel/task/scheduler.c), which is compiled unmodified into the
 * unit-test runner.
 *
 * scheduler.c needs exactly four external symbols:
 *   - pmm_alloc_page / pmm_free_page: provided by the REAL
 *     kernel/mm/pmm.c, which is also compiled into the runner
 *     (a pure bitmap allocator - it never dereferences the physical
 *     addresses it hands out, so task stack "allocation" is safe on
 *     the host even though the addresses are not mapped).
 *   - timer_get_ticks: replaced by a controllable fake clock below,
 *     so tests can drive time-slice preemption and sleep/wakeup
 *     deterministically.
 *   - kernel_print: swallowed; the VGA text buffer it writes to on
 *     real hardware does not exist in a host process.
 *
 * Nothing in scheduler.c executes a context switch (the switch point
 * is a documented no-op in scheduler_schedule), so the whole
 * bookkeeping layer runs natively here.
 */

#include <stdint.h>

#include "sched_stubs.h"

static uint32_t fake_ticks = 0;

/* Matches uint32_t timer_get_ticks(void) from include/kernel/timer.h */
uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

void test_timer_set_ticks(uint32_t ticks) {
    fake_ticks = ticks;
}

void test_timer_advance(uint32_t delta) {
    fake_ticks += delta;
}

/* Matches void kernel_print(const char *str) from include/kernel/kernel.h */
void kernel_print(const char *str) {
    (void)str; /* keep test output clean */
}
