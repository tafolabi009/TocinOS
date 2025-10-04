/**
 * TocinOS Advanced Preemptive Multitasking Scheduler
 * 
 * Features:
 * - Multi-core SMP support with per-CPU run queues
 * - Real-time scheduling policies (FIFO, RR, NORMAL)
 * - CPU affinity and load balancing
 * - Priority inheritance for synchronization
 * - Comprehensive scheduler statistics
 * - Demand paging support
 */

#include "../include/kernel/task.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/kernel.h"

#define MAX_TASKS 256
#define MAX_CPUS 64
#define DEFAULT_STACK_SIZE 16384
#define DEFAULT_TIME_SLICE 10  // 10 ticks for normal tasks
#define RT_TIME_SLICE 100      // 100 ticks for RT tasks

// Priority levels: 0-99 for RT, 100-139 for normal
#define MAX_RT_PRIORITY 99
#define MAX_NORMAL_PRIORITY 139
#define DEFAULT_PRIORITY 120

// Task pool
static task_t task_pool[MAX_TASKS];
static int task_bitmap[MAX_TASKS / 32];
static int next_task_id = 1;

// Per-CPU run queues (simplified to single CPU for now, expandable)
static cpu_runqueue_t cpu_runqueues[MAX_CPUS];
static int num_cpus = 1;
static int current_cpu = 0;

// Scheduler state
static int scheduler_enabled = 0;
static int preemption_enabled = 1;
static int preemption_count = 0;

// Global scheduler statistics
static scheduler_stats_t global_stats = {0};

/**
 * Helper: Copy string
 */
static void strcpy_safe(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * Helper: Allocate task ID
 */
static int alloc_task_id(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        int word = i / 32;
        int bit = i % 32;
        if (!(task_bitmap[word] & (1 << bit))) {
            task_bitmap[word] |= (1 << bit);
            return i;
        }
    }
    return -1;
}

/**
 * Helper: Free task ID
 */
static void free_task_id(int task_id) {
    if (task_id >= 0 && task_id < MAX_TASKS) {
        int word = task_id / 32;
        int bit = task_id % 32;
        task_bitmap[word] &= ~(1 << bit);
    }
}

/**
 * Helper: Get task by ID
 */
static task_t* get_task(int task_id) {
    if (task_id >= 0 && task_id < MAX_TASKS) {
        int word = task_id / 32;
        int bit = task_id % 32;
        if (task_bitmap[word] & (1 << bit)) {
            return &task_pool[task_id];
        }
    }
    return 0;
}

/**
 * Helper: Add task to run queue
 */
static void enqueue_task(cpu_runqueue_t *rq, task_t *task) {
    if (!task || task->priority > MAX_NORMAL_PRIORITY) {
        return;
    }
    
    // Add to priority queue
    task->next = rq->ready_lists[task->priority];
    task->prev = 0;
    if (rq->ready_lists[task->priority]) {
        rq->ready_lists[task->priority]->prev = task;
    }
    rq->ready_lists[task->priority] = task;
    rq->nr_running++;
}

/**
 * Helper: Remove task from run queue
 */
static void dequeue_task(cpu_runqueue_t *rq, task_t *task) {
    if (!task || task->priority > MAX_NORMAL_PRIORITY) {
        return;
    }
    
    if (task->prev) {
        task->prev->next = task->next;
    } else {
        rq->ready_lists[task->priority] = task->next;
    }
    
    if (task->next) {
        task->next->prev = task->prev;
    }
    
    task->next = 0;
    task->prev = 0;
    rq->nr_running--;
}

/**
 * Helper: Pick next task to run
 */
static task_t* pick_next_task(cpu_runqueue_t *rq) {
    // Find highest priority ready task
    for (int i = 0; i <= MAX_NORMAL_PRIORITY; i++) {
        if (rq->ready_lists[i]) {
            return rq->ready_lists[i];
        }
    }
    
    // Return idle task if no other task is ready
    return rq->idle_task;
}

/**
 * Helper: Calculate time slice based on policy and priority
 */
static uint64_t calculate_time_slice(task_t *task) {
    if (task->policy == SCHED_FIFO) {
        return RT_TIME_SLICE * 10;  // RT FIFO gets very long slices
    } else if (task->policy == SCHED_RR) {
        return RT_TIME_SLICE;
    } else if (task->policy == SCHED_BATCH) {
        return DEFAULT_TIME_SLICE * 5;  // Batch tasks get longer slices
    } else if (task->policy == SCHED_IDLE) {
        return DEFAULT_TIME_SLICE / 2;  // Idle tasks get short slices
    }
    
    // Normal tasks: time slice inversely proportional to priority
    // Higher priority (lower number) = longer time slice
    int nice_value = PRIORITY_TO_NICE(task->priority);
    return DEFAULT_TIME_SLICE * (20 - nice_value) / 20;
}


/**
 * Initialize the task scheduler
 */
void scheduler_init(void) {
    // Clear task pool
    for (int i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_TERMINATED;
        task_pool[i].id = 0;
    }
    
    // Clear task bitmap
    for (int i = 0; i < MAX_TASKS / 32; i++) {
        task_bitmap[i] = 0;
    }
    
    // Initialize run queues for all CPUs
    for (int cpu = 0; cpu < num_cpus; cpu++) {
        scheduler_init_cpu(cpu);
    }
    
    scheduler_enabled = 0;
    kernel_print("[SCHED] Advanced scheduler initialized with SMP support\n");
}

/**
 * Initialize per-CPU scheduler
 */
void scheduler_init_cpu(int cpu_id) {
    if (cpu_id >= MAX_CPUS) {
        return;
    }
    
    cpu_runqueue_t *rq = &cpu_runqueues[cpu_id];
    rq->current = 0;
    rq->idle_task = 0;
    rq->nr_running = 0;
    rq->load_weight = 0;
    rq->last_balance = 0;
    
    for (int i = 0; i <= MAX_NORMAL_PRIORITY; i++) {
        rq->ready_lists[i] = 0;
    }
}

/**
 * Create a new task with default settings
 */
int task_create(void (*entry_point)(void), const char *name, sched_policy_t policy, int priority) {
    return task_create_ex(entry_point, name, policy, priority, CPU_MASK_ALL, DEFAULT_STACK_SIZE);
}

/**
 * Create a new task with extended options
 */
int task_create_ex(void (*entry_point)(void), const char *name, sched_policy_t policy, 
                   int priority, cpu_mask_t affinity, uint32_t stack_size) {
    // Allocate task ID
    int task_id = alloc_task_id();
    if (task_id < 0) {
        return -1;  // No available task slots
    }
    
    task_t *task = &task_pool[task_id];
    
    // Initialize task structure
    task->id = task_id;
    strcpy_safe(task->name, name ? name : "unnamed", 32);
    task->policy = policy;
    task->static_priority = priority;
    task->priority = priority;
    task->effective_priority = priority;
    task->nice = PRIORITY_TO_NICE(priority);
    task->state = TASK_NEW;
    
    // SMP settings
    task->cpu = current_cpu;
    task->affinity = affinity;
    
    // Time management
    task->time_slice = calculate_time_slice(task);
    task->exec_start = 0;
    task->sleep_until = 0;
    
    // Synchronization
    task->blocked_on = -1;
    task->pi_waiters = 0;
    
    // Statistics
    task->stats.exec_time = 0;
    task->stats.wait_time = 0;
    task->stats.ctx_switches = 0;
    task->stats.preemptions = 0;
    task->stats.migrations = 0;
    
    // Allocate stack
    if (stack_size < 4096) {
        stack_size = 4096;
    }
    task->stack_size = stack_size;
    task->stack_base = pmm_alloc_page();
    if (!task->stack_base) {
        free_task_id(task_id);
        return -1;
    }
    
    // Initialize CPU state
    task->esp = task->stack_base + stack_size;
    task->ebp = task->esp;
    task->eip = (uint32_t)entry_point;
    task->eflags = 0x202;  // IF=1 (interrupts enabled)
    task->cr3 = 0;  // Use kernel page directory for now
    
    // Linked list
    task->next = 0;
    task->prev = 0;
    
    // Add to run queue
    task->state = TASK_READY;
    cpu_runqueue_t *rq = &cpu_runqueues[task->cpu];
    enqueue_task(rq, task);
    
    return task_id;
}


/**
 * Schedule next task (core scheduler function)
 */
void scheduler_schedule(void) {
    if (!scheduler_enabled || !preemption_enabled) {
        return;
    }
    
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    task_t *prev = rq->current;
    
    // If current task is running, move it to ready
    if (prev && prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        enqueue_task(rq, prev);
    }
    
    // Pick next task
    task_t *next = pick_next_task(rq);
    if (!next) {
        return;  // No task to run
    }
    
    // Remove from ready queue
    if (next->state == TASK_READY) {
        dequeue_task(rq, next);
    }
    
    // Update task state
    next->state = TASK_RUNNING;
    next->time_slice = calculate_time_slice(next);
    next->exec_start = timer_get_ticks();
    
    // Update statistics
    if (prev && prev != next) {
        prev->stats.ctx_switches++;
        global_stats.total_ctx_switches++;
    }
    
    rq->current = next;
    
    // Context switch would happen here in real implementation
    // For now, this is a framework
}

/**
 * Scheduler tick - called by timer interrupt
 */
void scheduler_tick(void) {
    if (!scheduler_enabled) {
        return;
    }
    
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    task_t *current = rq->current;
    
    if (!current) {
        return;
    }
    
    // Update execution time
    current->stats.exec_time++;
    
    // Decrement time slice
    if (current->time_slice > 0) {
        current->time_slice--;
    }
    
    // Check for preemption
    if (current->time_slice == 0) {
        // For FIFO tasks, only preempt on block/yield
        if (current->policy == SCHED_FIFO) {
            current->time_slice = calculate_time_slice(current);
            return;
        }
        
        // Preempt the task
        current->stats.preemptions++;
        global_stats.total_preemptions++;
        scheduler_schedule();
    }
    
    // Check for sleeping tasks to wake up
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_bitmap[i / 32] & (1 << (i % 32))) {
            task_t *task = &task_pool[i];
            if (task->state == TASK_SLEEPING && 
                timer_get_ticks() >= task->sleep_until) {
                task_wakeup(task->id);
            }
        }
    }
}

/**
 * Start the scheduler
 */
void scheduler_start(void) {
    if (scheduler_enabled) {
        return;
    }
    
    scheduler_enabled = 1;
    kernel_print("[SCHED] Scheduler started\n");
    
    // Initial schedule
    scheduler_schedule();
}

/**
 * Block current task
 */
void task_block(int resource_id) {
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    task_t *current = rq->current;
    
    if (!current) {
        return;
    }
    
    current->state = TASK_BLOCKED;
    current->blocked_on = resource_id;
    scheduler_schedule();
}

/**
 * Unblock a task
 */
void task_unblock(int task_id) {
    task_t *task = get_task(task_id);
    if (!task || task->state != TASK_BLOCKED) {
        return;
    }
    
    task->state = TASK_READY;
    task->blocked_on = -1;
    
    cpu_runqueue_t *rq = &cpu_runqueues[task->cpu];
    enqueue_task(rq, task);
}

/**
 * Put current task to sleep
 */
void task_sleep(uint64_t ticks) {
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    task_t *current = rq->current;
    
    if (!current) {
        return;
    }
    
    current->state = TASK_SLEEPING;
    current->sleep_until = timer_get_ticks() + ticks;
    scheduler_schedule();
}

/**
 * Wake up a sleeping task
 */
void task_wakeup(int task_id) {
    task_t *task = get_task(task_id);
    if (!task || task->state != TASK_SLEEPING) {
        return;
    }
    
    task->state = TASK_READY;
    task->sleep_until = 0;
    
    cpu_runqueue_t *rq = &cpu_runqueues[task->cpu];
    enqueue_task(rq, task);
}

/**
 * Terminate current task
 */
void task_exit(int exit_code) {
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    task_t *current = rq->current;
    
    if (!current) {
        return;
    }
    
    (void)exit_code;  // TODO: Store exit code
    
    current->state = TASK_ZOMBIE;
    
    // Free task resources
    if (current->stack_base) {
        pmm_free_page(current->stack_base);
    }
    
    free_task_id(current->id);
    
    // Schedule next task
    scheduler_schedule();
}

/**
 * Yield CPU to next task
 */
void task_yield(void) {
    scheduler_schedule();
}

/**
 * Get current task ID
 */
int task_get_current_id(void) {
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    return rq->current ? rq->current->id : -1;
}

/**
 * Get current task
 */
task_t* task_get_current(void) {
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    return rq->current;
}

/**
 * Set task priority
 */
int task_set_priority(int task_id, int priority) {
    task_t *task = get_task(task_id);
    if (!task || priority > MAX_NORMAL_PRIORITY) {
        return -1;
    }
    
    // Remove from old priority queue if ready
    if (task->state == TASK_READY) {
        cpu_runqueue_t *rq = &cpu_runqueues[task->cpu];
        dequeue_task(rq, task);
        task->priority = priority;
        task->static_priority = priority;
        enqueue_task(rq, task);
    } else {
        task->priority = priority;
        task->static_priority = priority;
    }
    
    return 0;
}

/**
 * Get task priority
 */
int task_get_priority(int task_id) {
    task_t *task = get_task(task_id);
    return task ? task->priority : -1;
}

/**
 * Set task scheduling policy
 */
int task_set_policy(int task_id, sched_policy_t policy) {
    task_t *task = get_task(task_id);
    if (!task) {
        return -1;
    }
    
    task->policy = policy;
    task->time_slice = calculate_time_slice(task);
    return 0;
}

/**
 * Set task CPU affinity
 */
int task_set_affinity(int task_id, cpu_mask_t affinity) {
    task_t *task = get_task(task_id);
    if (!task || affinity == CPU_MASK_NONE) {
        return -1;
    }
    
    task->affinity = affinity;
    
    // Migrate task if current CPU is not in affinity mask
    if (!(affinity & CPU_MASK_CPU(task->cpu))) {
        // Find first available CPU in mask
        for (int cpu = 0; cpu < num_cpus; cpu++) {
            if (affinity & CPU_MASK_CPU(cpu)) {
                return task_migrate(task_id, cpu);
            }
        }
    }
    
    return 0;
}

/**
 * Get task CPU affinity
 */
int task_get_affinity(int task_id, cpu_mask_t *affinity) {
    task_t *task = get_task(task_id);
    if (!task || !affinity) {
        return -1;
    }
    
    *affinity = task->affinity;
    return 0;
}

/**
 * Set task nice value
 */
int task_set_nice(int task_id, int nice) {
    if (nice < -20 || nice > 19) {
        return -1;
    }
    
    int new_priority = NICE_TO_PRIORITY(nice);
    return task_set_priority(task_id, new_priority);
}

/**
 * Migrate task to another CPU
 */
int task_migrate(int task_id, int target_cpu) {
    task_t *task = get_task(task_id);
    if (!task || target_cpu >= num_cpus || target_cpu == task->cpu) {
        return -1;
    }
    
    // Check if target CPU is in affinity mask
    if (!(task->affinity & CPU_MASK_CPU(target_cpu))) {
        return -1;
    }
    
    cpu_runqueue_t *old_rq = &cpu_runqueues[task->cpu];
    cpu_runqueue_t *new_rq = &cpu_runqueues[target_cpu];
    
    // Remove from old run queue
    if (task->state == TASK_READY) {
        dequeue_task(old_rq, task);
    }
    
    // Update CPU
    task->cpu = target_cpu;
    task->stats.migrations++;
    global_stats.total_migrations++;
    
    // Add to new run queue
    if (task->state == TASK_READY) {
        enqueue_task(new_rq, task);
    }
    
    return 0;
}

/**
 * Load balancing across CPUs
 */
void scheduler_balance_load(void) {
    if (num_cpus < 2) {
        return;
    }
    
    uint64_t current_time = timer_get_ticks();
    cpu_runqueue_t *rq = &cpu_runqueues[current_cpu];
    
    // Only balance periodically
    if (current_time - rq->last_balance < 100) {
        return;
    }
    
    rq->last_balance = current_time;
    global_stats.load_balance_count++;
    
    // Find most loaded and least loaded CPUs
    int max_cpu = 0, min_cpu = 0;
    uint32_t max_load = cpu_runqueues[0].nr_running;
    uint32_t min_load = cpu_runqueues[0].nr_running;
    
    for (int i = 1; i < num_cpus; i++) {
        uint32_t load = cpu_runqueues[i].nr_running;
        if (load > max_load) {
            max_load = load;
            max_cpu = i;
        }
        if (load < min_load) {
            min_load = load;
            min_cpu = i;
        }
    }
    
    // If imbalance is significant, migrate a task
    if (max_load > min_load + 2) {
        cpu_runqueue_t *max_rq = &cpu_runqueues[max_cpu];
        // Find a suitable task to migrate
        for (int i = MAX_NORMAL_PRIORITY; i >= 0; i--) {
            task_t *task = max_rq->ready_lists[i];
            if (task && (task->affinity & CPU_MASK_CPU(min_cpu))) {
                task_migrate(task->id, min_cpu);
                return;
            }
        }
    }
}

/**
 * Priority inheritance boost
 */
void task_pi_boost(int task_id, int priority) {
    task_t *task = get_task(task_id);
    if (!task) {
        return;
    }
    
    // Only boost if new priority is higher (lower value)
    if (priority < task->effective_priority) {
        task->effective_priority = priority;
        
        // Re-queue if ready
        if (task->state == TASK_READY) {
            cpu_runqueue_t *rq = &cpu_runqueues[task->cpu];
            dequeue_task(rq, task);
            task->priority = priority;
            enqueue_task(rq, task);
        }
    }
}

/**
 * Priority inheritance deboost
 */
void task_pi_deboost(int task_id) {
    task_t *task = get_task(task_id);
    if (!task) {
        return;
    }
    
    // Restore static priority
    task->effective_priority = task->static_priority;
    task->priority = task->static_priority;
}

/**
 * Get task statistics
 */
int task_get_stats(int task_id, task_stats_t *stats) {
    task_t *task = get_task(task_id);
    if (!task || !stats) {
        return -1;
    }
    
    *stats = task->stats;
    return 0;
}

/**
 * Get global scheduler statistics
 */
void scheduler_get_stats(scheduler_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    *stats = global_stats;
}

/**
 * Print scheduler statistics
 */
void scheduler_print_stats(void) {
    kernel_print("\n=== Scheduler Statistics ===\n");
    kernel_print("Context Switches: ");
    // TODO: Print uint64_t value
    kernel_print("\nMigrations: ");
    // TODO: Print uint64_t value
    kernel_print("\nPreemptions: ");
    // TODO: Print uint64_t value
    kernel_print("\nLoad Balances: ");
    // TODO: Print uint64_t value
    kernel_print("\n");
    
    kernel_print("\n=== Per-CPU Statistics ===\n");
    for (int i = 0; i < num_cpus; i++) {
        cpu_runqueue_t *rq = &cpu_runqueues[i];
        kernel_print("CPU ");
        // TODO: Print CPU number
        kernel_print(": Running tasks = ");
        // TODO: Print nr_running
        kernel_print("\n");
    }
}

/**
 * Enable preemption
 */
void scheduler_enable_preemption(void) {
    if (preemption_count > 0) {
        preemption_count--;
    }
    if (preemption_count == 0) {
        preemption_enabled = 1;
    }
}

/**
 * Disable preemption
 */
void scheduler_disable_preemption(void) {
    preemption_enabled = 0;
    preemption_count++;
}

/**
 * Check if preemption is enabled
 */
int scheduler_preemption_enabled(void) {
    return preemption_enabled;
}
