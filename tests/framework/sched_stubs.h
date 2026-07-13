/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Test-side controls for the host stubs backing the REAL scheduler
 * (kernel/task/scheduler.c). The scheduler reads time exclusively
 * through timer_get_ticks(); these hooks let tests move that clock
 * deterministically.
 */

#ifndef SCHED_STUBS_H
#define SCHED_STUBS_H

#include <stdint.h>

/* Set the fake tick counter returned by timer_get_ticks(). */
void test_timer_set_ticks(uint32_t ticks);

/* Advance the fake tick counter by delta. */
void test_timer_advance(uint32_t delta);

#endif /* SCHED_STUBS_H */
