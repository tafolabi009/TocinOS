/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL scheduler (kernel/task/scheduler.c),
 * compiled directly into the test runner - no mocks.
 *
 * Host environment:
 *   - Task stacks come from the REAL buddy allocator (kernel/mm/buddy.c),
 *     which only does descriptor-table bookkeeping, so the unmapped
 *     "physical" stack addresses are never dereferenced here. The REAL
 *     PMM (kernel/mm/pmm.c) is also linked and re-initialized per test.
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

/* Re-initialize PMM + buddy + scheduler + fake clock before each test.
 * The buddy allocator backs task stacks (contiguous stack_size-byte
 * blocks); re-initializing it also reclaims stacks from prior tests. */
static void sched_fresh(void) {
    pmm_init();
    buddy_init(BUDDY_REGION_START, BUDDY_FALLBACK_END);
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

        // Each task consumed a full 16 KiB (4-page) stack from the buddy;
        // the PMM's single-page pool is not used for stacks anymore.
        ASSERT_EQ(pmm_get_used_pages(), 512u, "PMM untouched by task stacks");
        ASSERT_EQ(buddy_get_free_page_count(), buddy_get_managed_pages() - 12u,
                  "3 stacks x 4 pages allocated from the buddy");
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
        // host), so the CPU goes idle. rq->current must NOT keep pointing
        // at the blocked task (roadmap bug #5, fixed).
        task_block(7);
        ASSERT_EQ(task_get_current_id(), -1, "No current task while blocked");
        ASSERT_TRUE(task_get_current() == 0, "CPU idles with a NULL current");
        ASSERT_EQ(task_get_by_id(a)->state, TASK_BLOCKED, "Task is BLOCKED");

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

        // Regression (roadmap bug #3): enqueue_task() used to insert at
        // the list HEAD, so the preempted task was immediately re-picked
        // and the other same-priority task starved. With FIFO tail-append
        // the slice expiry hands the CPU to the peer.
        ASSERT_NE(task_get_current_id(), first,
                  "Slice expiry rotates to the other same-priority task");
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

    // task_exit leaves a readable zombie; task_reap releases slot + stack
    TEST_CASE(exit_and_id_reuse)
        sched_fresh();

        int a = task_create(dummy_entry, "doomed", SCHED_NORMAL, 100);
        int b = task_create(dummy_entry, "survivor", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "Doomed task is running");

        uint32_t free_before = buddy_get_free_page_count();
        task_exit(7);
        ASSERT_EQ(task_get_current_id(), b, "Survivor scheduled after exit");
        ASSERT_EQ(task_get_by_id(a)->state, TASK_ZOMBIE,
                  "Exited task stays a zombie until reaped");
        ASSERT_EQ(buddy_get_free_page_count(), free_before,
                  "Zombie keeps its stack until reaped");

        int code = -1;
        ASSERT_EQ(task_reap(a, &code), 0, "Zombie reaps cleanly");
        ASSERT_EQ(code, 7, "Reap hands back the exit code");
        ASSERT_EQ(buddy_get_free_page_count(), free_before + 4u,
                  "Reap returns the 4 stack pages to the buddy");
        ASSERT_EQ(task_get_priority(a), -1, "Reaped task ID no longer valid");

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

        // Deboost while 'a' is RUNNING (not sitting in a ready list).
        // The READY-task deboost path (roadmap bug #2, fixed) has its own
        // regression test: pi_deboost_requeues_ready_task.
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

    // Regression (roadmap bug #1): the stack top must lie within the
    // allocated stack region. task_create_ex used to allocate ONE 4 KiB
    // PMM page while pointing esp stack_size (16 KiB) bytes above it.
    TEST_CASE(stack_top_within_allocation)
        sched_fresh();

        uint32_t managed = buddy_get_managed_pages();

        int t = task_create(dummy_entry, "deft", SCHED_NORMAL, 120);
        task_t *task = task_get_by_id(t);
        ASSERT_TRUE(task != 0, "Task visible via task_get_by_id");
        ASSERT_EQ(task->stack_size, 16384u, "Default stack is 16 KiB");
        ASSERT_EQ(buddy_get_free_page_count(), managed - 4u,
                  "16 KiB (4 pages) really allocated for the stack");
        ASSERT_EQ(buddy_owns((void *)(long)task->stack_base), 1,
                  "Stack comes from the buddy region");
        ASSERT_EQ(task->esp, task->stack_base + task->stack_size,
                  "esp starts at the top of the stack");
        ASSERT_TRUE(task->esp > task->stack_base &&
                    task->esp <= task->stack_base + task->stack_size,
                    "Stack top lies within the allocated region");
        ASSERT_EQ(task->ebp, task->esp, "ebp starts at the stack top");

        // Odd sizes round UP to the next power-of-two block, and
        // stack_size reflects what was really allocated
        int t2 = task_create_ex(dummy_entry, "odd", SCHED_NORMAL, 120,
                                CPU_MASK_ALL, 5000);
        task_t *task2 = task_get_by_id(t2);
        ASSERT_EQ(task2->stack_size, 8192u, "5000 B request rounds to 8 KiB");
        ASSERT_EQ(buddy_get_free_page_count(), managed - 4u - 2u,
                  "Two more pages allocated for the 8 KiB stack");
        ASSERT_EQ(task2->esp, task2->stack_base + task2->stack_size,
                  "esp matches the rounded allocation");

        // Tiny requests are clamped to one page
        int t3 = task_create_ex(dummy_entry, "tiny", SCHED_NORMAL, 120,
                                CPU_MASK_ALL, 64);
        task_t *task3 = task_get_by_id(t3);
        ASSERT_EQ(task3->stack_size, 4096u, "Minimum stack is one page");
        ASSERT_EQ(task3->esp, task3->stack_base + 4096u,
                  "esp sits at the top of the single page");
    END_TEST_CASE()

    // Regression (roadmap bug #2): deboosting a READY task must requeue
    // it. The old code rewrote the priority in place, so the task stayed
    // linked in the OLD priority's list while claiming the new priority -
    // scheduling then picked the wrong task and corrupted list heads.
    TEST_CASE(pi_deboost_requeues_ready_task)
        sched_fresh();

        int a = task_create(dummy_entry, "holder", SCHED_NORMAL, 120);
        int b = task_create(dummy_entry, "waiter", SCHED_NORMAL, 110);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), b, "Higher-priority task runs");

        task_pi_boost(a, 100);         /* a is READY: moves to list 100 */
        ASSERT_EQ(task_get_priority(a), 100, "Boost applied to READY task");
        task_pi_deboost(a);            /* still READY: must move back */
        ASSERT_EQ(task_get_priority(a), 120, "Deboost restores static prio");

        task_yield();
        ASSERT_EQ(task_get_current_id(), b,
                  "Deboosted task no longer outranks the runner");

        task_block(1);                 /* b blocks: a must be schedulable */
        ASSERT_EQ(task_get_current_id(), a,
                  "Deboosted task reachable through its 120 ready list");

        task_unblock(b);
        task_yield();
        ASSERT_EQ(task_get_current_id(), b, "Ready lists stayed consistent");
    END_TEST_CASE()

    // Regression (roadmap bug #3): two same-priority tasks must alternate
    // across successive schedules (FIFO tail-append), instead of the
    // head-inserted LIFO re-pick that starved every peer.
    TEST_CASE(same_priority_round_robin)
        sched_fresh();

        int a = task_create(dummy_entry, "ping", SCHED_NORMAL, 120);
        int b = task_create(dummy_entry, "pong", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "FIFO: first-created runs first");

        task_yield();
        ASSERT_EQ(task_get_current_id(), b, "Yield rotates to the peer");
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "Rotation comes back around");
        task_yield();
        ASSERT_EQ(task_get_current_id(), b, "Rotation is stable");

        // A third same-priority task joins at the TAIL of the rotation
        int c = task_create(dummy_entry, "pang", SCHED_NORMAL, 120);
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "Earlier arrivals drain first");
        task_yield();
        ASSERT_EQ(task_get_current_id(), c, "Newcomer gets its turn");
        task_yield();
        ASSERT_EQ(task_get_current_id(), b, "Three-way rotation holds");
    END_TEST_CASE()

    // Regression (roadmap bug #5): task_exit must store the exit code and
    // keep it readable while the task is a zombie; task_reap returns it.
    TEST_CASE(exit_code_readable_until_reap)
        sched_fresh();

        int a = task_create(dummy_entry, "coder", SCHED_NORMAL, 100);
        int b = task_create(dummy_entry, "other", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "Exiting task is current");

        int code = 0;
        ASSERT_EQ(task_get_exit_code(a, &code), -1,
                  "No exit code while the task is alive");

        task_exit(-42);
        ASSERT_EQ(task_get_current_id(), b, "CPU moved on after exit");
        ASSERT_EQ(task_get_exit_code(a, &code), 0, "Zombie exposes exit code");
        ASSERT_EQ(code, -42, "Stored code matches task_exit argument");

        code = 0;
        ASSERT_EQ(task_get_exit_code(a, &code), 0, "Code stays readable");
        ASSERT_EQ(code, -42, "Value unchanged on re-read");

        ASSERT_EQ(task_reap(b, &code), -1, "Running task cannot be reaped");
        ASSERT_EQ(task_reap(a, &code), 0, "Zombie reaps");
        ASSERT_EQ(code, -42, "Reap hands back the exit code");
        ASSERT_EQ(task_get_exit_code(a, &code), -1,
                  "Reaped slot no longer readable");
        ASSERT_EQ(task_reap(a, &code), -1, "Double reap rejected");
    END_TEST_CASE()

    // Regression (roadmap bug #5): when the sole runnable task blocks or
    // exits, the CPU must go idle (current = NULL) instead of leaving the
    // non-runnable task installed as rq->current.
    TEST_CASE(blocked_sole_task_idles_cpu)
        sched_fresh();

        int a = task_create(dummy_entry, "loner", SCHED_NORMAL, 120);
        scheduler_start();
        ASSERT_EQ(task_get_current_id(), a, "Sole task runs");

        task_block(3);
        ASSERT_EQ(task_get_current_id(), -1, "CPU idles: no current task");
        ASSERT_EQ(task_get_by_id(a)->state, TASK_BLOCKED, "Task is blocked");
        ASSERT_EQ(task_get_by_id(a)->blocked_on, 3, "Blocked on resource 3");

        scheduler_tick();              /* ticking an idle CPU is a no-op */
        ASSERT_EQ(task_get_current_id(), -1, "Idle survives a tick");

        task_unblock(a);
        task_yield();
        ASSERT_EQ(task_get_current_id(), a, "Unblocked task runs again");
        ASSERT_EQ(task_get_by_id(a)->state, TASK_RUNNING, "Back to RUNNING");

        // Exit the sole task: the same nothing-runnable path via a zombie
        task_exit(0);
        ASSERT_EQ(task_get_current_id(), -1, "CPU idles after sole exit");
        ASSERT_EQ(task_get_by_id(a)->state, TASK_ZOMBIE, "Task is a zombie");
    END_TEST_CASE()

END_TEST_SUITE()
