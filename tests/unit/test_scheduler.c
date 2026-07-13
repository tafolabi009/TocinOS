/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL scheduler (kernel/task/scheduler.c),
 * compiled directly into the test runner - no mocks.
 *
 * Host environment:
 *   - Task stacks come from the REAL PMM (kernel/mm/pmm.c). The PMM
 *     only does bitmap bookkeeping, so the unmapped "physical" stack
 *     addresses are never dereferenced here.
 *   - timer_get_ticks()/kernel_print() are host stubs
 *     (tests/framework/sched_stubs.c); the fake clock is driven
 *     explicitly by each test.
 *   - scheduler_schedule() contains no context-switch code (the
 *     switch point is a documented no-op), so scheduling decisions,
 *     queues, priorities and state transitions all run natively.
 *     Functions that would need real context switches / interrupts
 *     on hardware (thread_*, futex_*, tls_*) live in other source
 *     files and are not linked or tested here.
 *
 * State handling: scheduler.c has static state with no dedicated
 * reset hook, but scheduler_init() re-clears the task pool, ID
 * bitmap and CPU-0 run queue, so every test starts with
 * sched_fresh(). Global stats (scheduler_get_stats) survive re-init
 * and are asserted via deltas only.
 */

#include "../framework/unittest.h"
#include "../framework/sched_stubs.h"
#include "../../include/kernel/task.h"
#include "../../include/kernel/memory.h"

static void dummy_entry(void) {
    /* never executed on the host - tasks are never context-switched */
}

/* Re-initialize PMM + scheduler + fake clock before each test. */
static void sched_fresh(void) {
    pmm_init();
    scheduler_init();
    test_timer_set_ticks(0);
}

TEST_SUITE(scheduler_tests)

    // Task creation populates the pool; attributes are readable back
    TEST_CASE(create_and_lookup)
        sched_fresh();

        int t0 = task_create(dummy_entry, "alpha", SCHED_NORMAL, 120);
        int t1 = task_create(dummy_entry, "beta", SCHED_NORMAL, 110);
        int t2 = task_create(dummy_entry, NULL, SCHED_FIFO, 50);

        ASSERT_EQ(t0, 0, "First task takes pool slot 0");
        ASSERT_EQ(t1, 1, "Second task takes pool slot 1");
        ASSERT_EQ(t2, 2, "NULL name is accepted (stored as 'unnamed')");

        ASSERT_EQ(task_get_priority(t0), 120, "Priority stored on create");
        ASSERT_EQ(task_get_priority(t1), 110, "Priority stored on create");
        ASSERT_EQ(task_get_priority(t2), 50, "RT priority stored on create");

        ASSERT_EQ(task_get_priority(200), -1, "Unallocated slot has no priority");
        ASSERT_EQ(task_get_priority(-1), -1, "Negative ID rejected");
        ASSERT_EQ(task_get_priority(100000), -1, "Out-of-range ID rejected");

        task_stats_t st;
        ASSERT_EQ(task_get_stats(t0, &st), 0, "Stats readable for live task");
        ASSERT_EQ(st.exec_time, 0u, "Fresh task has no exec time");
        ASSERT_EQ(st.ctx_switches, 0u, "Fresh task has no context switches");
        ASSERT_EQ(task_get_stats(t0, 0), -1, "NULL stats pointer rejected");

        cpu_mask_t mask = 0;
        ASSERT_EQ(task_get_affinity(t0, &mask), 0, "Affinity readable");
        ASSERT_EQ(mask, CPU_MASK_ALL, "Default affinity is all CPUs");

        // Each task consumed one physical page for its stack
        ASSERT_EQ(pmm_get_used_pages(), 512u + 3u, "3 stack pages allocated");
    END_TEST_CASE()

    // The pool holds exactly MAX_TASKS (256) tasks
    TEST_CASE(task_pool_exhaustion)
        sched_fresh();

        int created = 0;
        for (int i = 0; i < 300; i++) {
            if (task_create(dummy_entry, "filler", SCHED_NORMAL, 120) >= 0) {
                created++;
            }
        }

        ASSERT_EQ(created, 256, "Pool holds exactly MAX_TASKS tasks");
        ASSERT_EQ(task_create(dummy_entry, "extra", SCHED_NORMAL, 120), -1,
                  "Creation fails once the pool is full");
        // Note: there is no API to destroy a non-current task, so the
        // pool can only be drained via task_exit() of the running task.
    END_TEST_CASE()

    // With no tasks there is no current task; the first schedule adopts one
    TEST_CASE(start_with_no_tasks)
        sched_fresh();

        scheduler_start();
        ASSERT_EQ(task_get_current_id(), -1, "No current task on empty scheduler");
        ASSERT_TRUE(task_get_current() == 0, "Current task pointer is NULL");

        int a = task_create(dummy_entry, "first", SCHED_NORMAL, 120);
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "First task picked on next schedule");
        ASSERT_EQ(task_get_current()->state, TASK_RUNNING, "Picked task is RUNNING");

        // Block the only task: nothing else is runnable (no idle task on
        // host), so scheduler_schedule() leaves rq->current pointing at
        // the blocked task - documented current behaviour.
        task_block(7);
        ASSERT_EQ(task_get_current()->state, TASK_BLOCKED, "Task is BLOCKED");

        task_unblock(a);
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "Unblocked task runs again");
        ASSERT_EQ(task_get_current()->state, TASK_RUNNING, "Back to RUNNING");
    END_TEST_CASE()

    // The highest-priority ready task (lowest number) always wins the CPU
    TEST_CASE(priority_scheduling)
        sched_fresh();

        int low = task_create(dummy_entry, "low", SCHED_NORMAL, 120);
        int high = task_create(dummy_entry, "high", SCHED_NORMAL, 100);
        int mid = task_create(dummy_entry, "mid", SCHED_NORMAL, 110);

        scheduler_start();
        ASSERT_EQ(task_get_current_id(), high, "Highest priority task runs first");
        ASSERT_EQ(strcmp(task_get_current()->name, "high"), 0, "Task name stored");

        task_block(1);                 /* high blocks -> mid takes over */
        ASSERT_EQ(task_get_current_id(), mid, "Next-best task runs after block");

        task_block(2);                 /* mid blocks -> low takes over */
        ASSERT_EQ(task_get_current_id(), low, "Lowest priority runs last");

        task_unblock(high);            /* high becomes ready again */
        task_yield();
        ASSERT_EQ(task_get_current_id(), high, "Unblocked high-prio task preempts");

        task_unblock(mid);
        task_yield();
        ASSERT_EQ(task_get_current_id(), high,
                  "Highest-priority task keeps the CPU across yields");
    END_TEST_CASE()

    // Disabling preemption freezes scheduling decisions
    TEST_CASE(preemption_gate)
        sched_fresh();

        int a = task_create(dummy_entry, "a", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "Only task is running");

        int b = task_create(dummy_entry, "b", SCHED_NORMAL, 100);

        scheduler_disable_preemption();
        ASSERT_EQ(scheduler_preemption_enabled(), 0, "Preemption reported off");
        task_yield();
        ASSERT_EQ(task_get_current_id(), a,
                  "No switch while preemption is disabled");

        scheduler_enable_preemption();
        ASSERT_EQ(scheduler_preemption_enabled(), 1, "Preemption reported on");
        task_yield();
        ASSERT_EQ(task_get_current_id(), b,
                  "Higher-priority task wins once preemption is back on");
    END_TEST_CASE()

    // scheduler_tick() burns the time slice and preempts at zero
    TEST_CASE(tick_time_slice_preemption)
        sched_fresh();

        task_create(dummy_entry, "a", SCHED_NORMAL, 120);
        task_create(dummy_entry, "b", SCHED_NORMAL, 120);
        scheduler_start();

        int first = task_get_current_id();
        ASSERT_NE(first, -1, "A task is running");

        scheduler_stats_t before, after;
        scheduler_get_stats(&before);

        // Priority 120 (nice 0) => time slice of 10 ticks
        for (int i = 0; i < 9; i++) {
            scheduler_tick();
        }
        task_stats_t st;
        ASSERT_EQ(task_get_stats(first, &st), 0, "Stats available");
        ASSERT_EQ(st.exec_time, 9u, "Ticks accounted to the running task");
        ASSERT_EQ(st.preemptions, 0u, "No preemption before slice expires");

        scheduler_tick();              /* 10th tick -> slice hits 0 */
        ASSERT_EQ(task_get_stats(first, &st), 0, "Stats available");
        ASSERT_EQ(st.exec_time, 10u, "Tenth tick accounted");
        ASSERT_EQ(st.preemptions, 1u, "Slice expiry counted as preemption");

        scheduler_get_stats(&after);
        ASSERT_EQ(after.total_preemptions - before.total_preemptions, 1u,
                  "Global preemption counter advanced");

        // Note (kernel quirk, not fixed here): enqueue_task() inserts at
        // the head of the priority list, so the preempted task is
        // immediately re-picked and same-priority round-robin never
        // rotates to the other ready task.
        ASSERT_EQ(task_get_current_id(), first,
                  "LIFO ready-list keeps the same task running");
    END_TEST_CASE()

    // task_sleep parks a task until the tick clock passes sleep_until
    TEST_CASE(sleep_and_wakeup)
        sched_fresh();
        test_timer_set_ticks(1000);

        int a = task_create(dummy_entry, "sleeper", SCHED_NORMAL, 100);
        int b = task_create(dummy_entry, "worker", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "High-priority task starts running");

        task_sleep(50);                /* sleep until tick 1050 */
        ASSERT_EQ(task_get_current_id(), b, "CPU falls to the other task");

        scheduler_tick();              /* tick at 1000: too early to wake */
        task_yield();
        ASSERT_EQ(task_get_current_id(), b, "Sleeper not woken early");

        test_timer_advance(50);        /* now at 1050 */
        scheduler_tick();              /* wake-up scan fires */
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "Sleeper woke and preempted");

        task_wakeup(b);                /* b is READY, not SLEEPING: no-op */
        task_unblock(b);               /* b is READY, not BLOCKED: no-op */
        ASSERT_EQ(task_get_current_id(), a, "Bogus wake/unblock are no-ops");
        ASSERT_EQ(task_get_priority(b), 120, "Untouched task keeps its priority");
    END_TEST_CASE()

    // task_exit releases the slot and its stack page; IDs are reused
    TEST_CASE(exit_and_id_reuse)
        sched_fresh();

        int a = task_create(dummy_entry, "doomed", SCHED_NORMAL, 100);
        int b = task_create(dummy_entry, "survivor", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "Doomed task is running");

        unsigned int free_before = pmm_get_free_pages();
        task_exit(0);
        ASSERT_EQ(task_get_current_id(), b, "Survivor scheduled after exit");
        ASSERT_EQ(pmm_get_free_pages(), free_before + 1,
                  "Exited task's stack page returned to the PMM");
        ASSERT_EQ(task_get_priority(a), -1, "Exited task ID no longer valid");

        int c = task_create(dummy_entry, "recycled", SCHED_NORMAL, 130);
        ASSERT_EQ(c, a, "Freed task ID is reused");
        ASSERT_EQ(task_get_priority(c), 130, "Recycled slot has fresh attributes");
    END_TEST_CASE()

    // Priority / nice / policy setters validate their inputs
    TEST_CASE(priority_api_validation)
        sched_fresh();

        int a = task_create(dummy_entry, "tunable", SCHED_NORMAL, 120);

        ASSERT_EQ(task_set_priority(a, 90), 0, "Priority change accepted");
        ASSERT_EQ(task_get_priority(a), 90, "Ready task requeued at new priority");

        ASSERT_EQ(task_set_priority(a, 200), -1, "Priority above 139 rejected");
        ASSERT_EQ(task_get_priority(a), 90, "Rejected change leaves priority");
        ASSERT_EQ(task_set_priority(9999, 100), -1, "Unknown task rejected");

        ASSERT_EQ(task_set_nice(a, 0), 0, "Nice 0 accepted");
        ASSERT_EQ(task_get_priority(a), 120, "Nice 0 maps to priority 120");
        ASSERT_EQ(task_set_nice(a, 19), 0, "Nice 19 accepted");
        ASSERT_EQ(task_get_priority(a), 139, "Nice 19 maps to priority 139");
        ASSERT_EQ(task_set_nice(a, -20), 0, "Nice -20 accepted");
        ASSERT_EQ(task_get_priority(a), 100, "Nice -20 maps to priority 100");
        ASSERT_EQ(task_set_nice(a, 20), -1, "Nice above 19 rejected");
        ASSERT_EQ(task_set_nice(a, -21), -1, "Nice below -20 rejected");

        ASSERT_EQ(task_set_policy(a, SCHED_FIFO), 0, "Policy change accepted");
        ASSERT_EQ(task_set_policy(4242, SCHED_RR), -1, "Unknown task rejected");
    END_TEST_CASE()

    // Priority inheritance boosts scheduling; deboost restores it
    TEST_CASE(pi_boost_deboost)
        sched_fresh();

        int a = task_create(dummy_entry, "holder", SCHED_NORMAL, 120);
        int b = task_create(dummy_entry, "runner", SCHED_NORMAL, 110);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), b, "Higher-priority task runs");

        task_pi_boost(a, 100);         /* boost the READY task above b */
        ASSERT_EQ(task_get_priority(a), 100, "Boost raises effective priority");
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "Boosted task wins the CPU");

        // Deboost while 'a' is RUNNING (i.e. not sitting in a ready
        // list). Deboosting a READY task is avoided on purpose:
        // task_pi_deboost() restores the priority WITHOUT requeueing,
        // which desynchronises the task from the list it is linked
        // into - kernel bug reported with this milestone, not fixed.
        task_pi_deboost(a);
        ASSERT_EQ(task_get_priority(a), 120, "Deboost restores static priority");
        task_yield();
        ASSERT_EQ(task_get_current_id(), b, "Original winner runs again");

        task_pi_boost(b, 130);         /* 130 is LOWER priority: must be a no-op */
        ASSERT_EQ(task_get_priority(b), 110, "Boost never lowers priority");
        task_pi_boost(31337, 50);      /* unknown task: must not crash */
        ASSERT_EQ(task_get_current_id(), b, "Scheduler unaffected by bogus boost");
    END_TEST_CASE()

    // Single-CPU build: migration fails cleanly, affinity is validated
    TEST_CASE(migration_and_affinity)
        sched_fresh();

        int a = task_create(dummy_entry, "pinned", SCHED_NORMAL, 120);

        ASSERT_EQ(task_migrate(a, 1), -1, "CPU 1 does not exist (num_cpus == 1)");
        ASSERT_EQ(task_migrate(a, 0), -1, "Migration to the same CPU rejected");
        ASSERT_EQ(task_migrate(777, 0), -1, "Unknown task rejected");

        ASSERT_EQ(task_set_affinity(a, CPU_MASK_NONE), -1, "Empty mask rejected");
        ASSERT_EQ(task_set_affinity(a, CPU_MASK_CPU(0)), 0, "Pin to CPU 0 accepted");

        cpu_mask_t mask = 0;
        ASSERT_EQ(task_get_affinity(a, &mask), 0, "Affinity readable");
        ASSERT_EQ(mask, CPU_MASK_CPU(0), "Stored mask matches");
        ASSERT_EQ(task_get_affinity(a, 0), -1, "NULL output pointer rejected");

        scheduler_balance_load();      /* single CPU: must be a clean no-op */
        ASSERT_EQ(task_get_priority(a), 120, "Load balancing left the task alone");
    END_TEST_CASE()

END_TEST_SUITE()
