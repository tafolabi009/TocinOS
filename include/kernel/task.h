/**
 * TocinOS Advanced Task Scheduler Header
 * 
 * Features:
 * - Multi-core SMP support with per-CPU run queues
 * - Real-time scheduling policies (FIFO, RR, NORMAL)
 * - CPU affinity and load balancing
 * - Priority inheritance for synchronization
 * - Comprehensive scheduler statistics
 * - POSIX-like threading with clone() support
 * - Thread-Local Storage (TLS) support
 * - Futex-based synchronization
 */

#ifndef TASK_H
#define TASK_H

#include <stdint.h>

// clone() flags - for creating threads
#define CLONE_VM        0x00000100   // Share memory space
#define CLONE_FS        0x00000200   // Share filesystem info
#define CLONE_FILES     0x00000400   // Share file descriptor table
#define CLONE_SIGHAND   0x00000800   // Share signal handlers
#define CLONE_THREAD    0x00010000   // Same thread group
#define CLONE_SYSVSEM   0x00040000   // Share SysV semaphore undo
#define CLONE_SETTLS    0x00080000   // Set TLS for child
#define CLONE_PARENT_SETTID  0x00100000   // Set parent's TID
#define CLONE_CHILD_CLEARTID 0x00200000   // Clear TID on exit
#define CLONE_CHILD_SETTID   0x01000000   // Set child's TID

// futex operations
#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_WAIT_PRIVATE  128
#define FUTEX_WAKE_PRIVATE  129

// Thread states (extension of task states)
#define THREAD_JOINABLE     0x01
#define THREAD_DETACHED     0x02
#define THREAD_EXITED       0x04

// Maximum threads per process
#define MAX_THREADS_PER_PROCESS 64

// Task scheduling policies
typedef enum {
    SCHED_NORMAL = 0,      // Standard time-sharing policy
    SCHED_FIFO = 1,        // Real-time FIFO policy
    SCHED_RR = 2,          // Real-time Round-Robin policy
    SCHED_BATCH = 3,       // Batch processing policy
    SCHED_IDLE = 4         // Idle tasks policy
} sched_policy_t;

// Task states
typedef enum {
    TASK_NEW = 0,
    TASK_READY = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3,
    TASK_SLEEPING = 4,
    TASK_ZOMBIE = 5,
    TASK_TERMINATED = 6
} task_state_t;

// CPU affinity mask (up to 64 CPUs)
typedef uint64_t cpu_mask_t;

// Task statistics
typedef struct {
    uint64_t exec_time;        // Total execution time in ticks
    uint64_t wait_time;        // Total wait time in ticks
    uint64_t ctx_switches;     // Number of context switches
    uint64_t preemptions;      // Number of preemptions
    uint64_t migrations;       // Number of CPU migrations
} task_stats_t;

// Task control block structure
typedef struct task {
    uint32_t id;               // Task ID
    char name[32];             // Task name
    
    // CPU state
    uint32_t esp;              // Stack pointer
    uint32_t ebp;              // Base pointer
    uint32_t eip;              // Instruction pointer
    uint32_t eflags;           // CPU flags
    uint32_t cr3;              // Page directory (for memory isolation)
    
    // Scheduling
    sched_policy_t policy;     // Scheduling policy
    int priority;              // Dynamic priority (0-139: 0-99 RT, 100-139 normal)
    int static_priority;       // Static priority (doesn't change)
    int nice;                  // Nice value (-20 to +19)
    task_state_t state;        // Current state
    
    // SMP support
    int cpu;                   // Current CPU
    cpu_mask_t affinity;       // CPU affinity mask
    
    // Time management
    uint64_t time_slice;       // Remaining time slice in ticks
    uint64_t exec_start;       // Time when started executing
    uint64_t sleep_until;      // Wake up time for sleeping tasks
    
    // Synchronization
    int blocked_on;            // Resource ID task is blocked on
    struct task *pi_waiters;   // Priority inheritance waiters list
    int effective_priority;    // Priority with inheritance
    
    // Statistics
    task_stats_t stats;
    
    // Memory
    uint32_t stack_base;       // Stack base address
    uint32_t stack_size;       // Stack size
    
    // ===== Threading Support (Phase 3.2) =====
    
    // Thread identification
    uint32_t tid;              // Thread ID (unique across system)
    uint32_t tgid;             // Thread Group ID (PID of group leader)
    struct task *group_leader; // Thread group leader (main thread)
    struct task *thread_next;  // Next thread in same group
    struct task *thread_prev;  // Previous thread in same group
    int thread_count;          // Number of threads (only valid for group leader)
    
    // Thread flags
    uint32_t clone_flags;      // Flags passed to clone()
    uint32_t thread_flags;     // THREAD_JOINABLE, THREAD_DETACHED, etc.
    int exit_code;             // Exit code for pthread_join
    
    // Thread-Local Storage (TLS)
    uint32_t tls_base;         // Base address of TLS area
    uint32_t tls_size;         // Size of TLS area
    uint16_t tls_gdt_entry;    // GDT entry number for TLS segment
    
    // For clone()/thread creation
    uint32_t user_stack;       // User-space stack pointer
    uint32_t kernel_stack;     // Kernel stack base
    uint32_t *clear_child_tid; // Address to clear on exit (for pthread)
    uint32_t *set_child_tid;   // Address to set TID on creation
    
    // Futex support
    uint32_t *futex_addr;      // Address task is waiting on
    struct task *futex_next;   // Next task in futex wait queue
    struct task *futex_prev;   // Previous task in futex wait queue
    
    // ===== End Threading Support =====
    
    // Linked list pointers
    struct task *next;
    struct task *prev;
} task_t;

// Per-CPU run queue
typedef struct {
    task_t *current;           // Currently running task
    task_t *idle_task;         // Idle task for this CPU
    task_t *ready_lists[140];  // Priority-based ready lists (0-139)
    uint32_t nr_running;       // Number of running tasks
    uint64_t load_weight;      // CPU load weight
    uint64_t last_balance;     // Last load balance time
} cpu_runqueue_t;

// Scheduler statistics
typedef struct {
    uint64_t total_ctx_switches;
    uint64_t total_migrations;
    uint64_t total_preemptions;
    uint64_t load_balance_count;
    uint64_t idle_time;
} scheduler_stats_t;

// Scheduler functions
void scheduler_init(void);
void scheduler_start(void);
void scheduler_tick(void);
void scheduler_schedule(void);

// Task management
int task_create(void (*entry_point)(void), const char *name, sched_policy_t policy, int priority);
int task_create_ex(void (*entry_point)(void), const char *name, sched_policy_t policy, 
                   int priority, cpu_mask_t affinity, uint32_t stack_size);
void task_exit(int exit_code);
void task_yield(void);
int task_get_current_id(void);
task_t* task_get_current(void);

// Task state management
void task_block(int resource_id);
void task_unblock(int task_id);
void task_sleep(uint64_t ticks);
void task_wakeup(int task_id);

// Task attributes
int task_set_priority(int task_id, int priority);
int task_get_priority(int task_id);
int task_set_policy(int task_id, sched_policy_t policy);
int task_set_affinity(int task_id, cpu_mask_t affinity);
int task_get_affinity(int task_id, cpu_mask_t *affinity);
int task_set_nice(int task_id, int nice);

// SMP support
void scheduler_init_cpu(int cpu_id);
void scheduler_balance_load(void);
int task_migrate(int task_id, int target_cpu);

// Priority inheritance
void task_pi_boost(int task_id, int priority);
void task_pi_deboost(int task_id);

// Statistics
int task_get_stats(int task_id, task_stats_t *stats);
void scheduler_get_stats(scheduler_stats_t *stats);
void scheduler_print_stats(void);

// Advanced features
void scheduler_enable_preemption(void);
void scheduler_disable_preemption(void);
int scheduler_preemption_enabled(void);

// ===== Threading Support (Phase 3.2) =====

// Thread creation and management
int thread_create(uint32_t flags, void *stack, uint32_t stack_size,
                  uint32_t *parent_tid, uint32_t *child_tid, uint32_t tls);
int thread_clone(uint32_t flags, void *child_stack, uint32_t *parent_tid,
                 uint32_t *child_tid, void *tls);
void thread_exit(int exit_code);
int thread_join(uint32_t tid, int *exit_code);
int thread_detach(uint32_t tid);
uint32_t thread_self(void);

// Thread group management
int thread_get_tgid(uint32_t tid);
int thread_is_group_leader(task_t *task);
int thread_group_count(task_t *leader);

// Thread-Local Storage
int tls_setup(task_t *task, uint32_t tls_base, uint32_t tls_size);
uint32_t tls_get_base(void);
int tls_set_entry(uint32_t entry, uint32_t base, uint32_t limit, uint32_t flags);

// Futex operations
int futex_wait(uint32_t *uaddr, uint32_t val, uint32_t timeout);
int futex_wake(uint32_t *uaddr, uint32_t count);
void futex_init(void);

// ===== End Threading Support =====

#define NICE_TO_PRIORITY(nice) (120 + (nice))
#define PRIORITY_TO_NICE(prio) ((prio) - 120)
#define RT_PRIORITY_TO_PRIORITY(rt_prio) (99 - (rt_prio))

#define CPU_MASK_ALL ((cpu_mask_t)-1)
#define CPU_MASK_NONE ((cpu_mask_t)0)
#define CPU_MASK_CPU(cpu) ((cpu_mask_t)1 << (cpu))

#endif // TASK_H
