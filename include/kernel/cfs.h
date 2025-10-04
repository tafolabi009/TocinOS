/**
 * TocinOS CFS (Completely Fair Scheduler)
 * 
 * Modern scheduler inspired by Linux CFS
 */

#ifndef CFS_H
#define CFS_H

#include "../stdint.h"

// CFS configuration
#define CFS_MIN_GRANULARITY  1000000   // 1ms minimum timeslice (in nanoseconds)
#define CFS_TARGET_LATENCY   6000000   // 6ms target latency (in nanoseconds)
#define NICE_0_LOAD          1024       // Default weight for nice 0

// Red-black tree node (simplified)
struct rb_node {
    struct rb_node *left;
    struct rb_node *right;
    struct rb_node *parent;
    int color;  // 0 = black, 1 = red
};

// Red-black tree root
struct rb_root {
    struct rb_node *node;
};

// CFS task structure
typedef struct cfs_task {
    struct rb_node run_node;        // RB-tree node
    uint64_t vruntime;              // Virtual runtime
    uint64_t exec_start;            // Execution start time
    uint64_t sum_exec_runtime;      // Total execution time
    uint32_t load_weight;           // Task weight (priority)
    int on_rq;                      // On runqueue flag
} cfs_task_t;

// CFS runqueue structure
typedef struct cfs_runqueue {
    struct rb_root tasks_timeline;  // Red-black tree of tasks
    struct rb_node *rb_leftmost;    // Leftmost node (min vruntime)
    
    uint64_t min_vruntime;          // Minimum virtual runtime
    uint64_t load_weight;           // Total load weight
    uint32_t nr_running;            // Number of running tasks
    
    int lock;                       // Simple spinlock
} cfs_rq_t;

// Task group for group scheduling
typedef struct task_group {
    cfs_rq_t **cfs_rq;              // Per-CPU runqueues
    uint64_t shares;                // CPU shares
    struct task_group *parent;      // Parent group
    struct task_group *children;    // Child groups
} task_group_t;

// Priority to weight mapping (nice -20 to +19)
extern const uint32_t prio_to_weight[40];

// Function prototypes

/**
 * Initialize CFS scheduler
 */
void cfs_init(void);

/**
 * Initialize CFS runqueue
 */
void cfs_rq_init(cfs_rq_t *cfs_rq);

/**
 * Add task to CFS runqueue
 */
void cfs_enqueue_task(cfs_rq_t *cfs_rq, cfs_task_t *task);

/**
 * Remove task from CFS runqueue
 */
void cfs_dequeue_task(cfs_rq_t *cfs_rq, cfs_task_t *task);

/**
 * Pick next task to run
 */
cfs_task_t *cfs_pick_next_task(cfs_rq_t *cfs_rq);

/**
 * Update task runtime
 */
void cfs_update_curr(cfs_rq_t *cfs_rq, cfs_task_t *task, uint64_t delta_exec);

/**
 * Calculate task virtual runtime
 */
uint64_t cfs_calc_vruntime(cfs_task_t *task, uint64_t delta_exec);

/**
 * Load balancing across CPUs
 */
void cfs_load_balance(int cpu);

#endif // CFS_H
