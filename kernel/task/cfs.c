/**
 * TocinOS CFS Scheduler Implementation
 * 
 * Completely Fair Scheduler
 */

#include "../../include/kernel/cfs.h"

// Nice value to weight mapping (nice -20 to +19)
const uint32_t prio_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548, 7620, 6100, 4904, 3906,
    /*  -5 */ 3121, 2501, 1991, 1586, 1277,
    /*   0 */ 1024, 820, 655, 526, 423,
    /*   5 */ 335, 272, 215, 172, 137,
    /*  10 */ 110, 87, 70, 56, 45,
    /*  15 */ 36, 29, 23, 18, 15,
};

// Global CFS state
static cfs_rq_t global_cfs_rq = {0};

/**
 * Initialize red-black tree
 */
static void rb_init(struct rb_root *root) {
    root->node = 0;
}

/**
 * Find leftmost node in tree (minimum vruntime)
 */
static struct rb_node *rb_leftmost(struct rb_root *root) {
    struct rb_node *node = root->node;
    
    if (!node) {
        return 0;
    }
    
    while (node->left) {
        node = node->left;
    }
    
    return node;
}

/**
 * Insert node into red-black tree
 */
static void rb_insert(struct rb_root *root, struct rb_node *node, uint64_t key) {
    struct rb_node **link = &root->node;
    struct rb_node *parent = 0;
    
    // Find insertion point
    while (*link) {
        parent = *link;
        cfs_task_t *task = (cfs_task_t *)((char *)parent - __builtin_offsetof(cfs_task_t, run_node));
        
        if (key < task->vruntime) {
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }
    
    // Insert node
    node->parent = parent;
    node->left = 0;
    node->right = 0;
    node->color = 1; // Red
    *link = node;
    
    // Rebalancing would go here (simplified implementation)
}

/**
 * Remove node from red-black tree
 */
static void rb_erase(struct rb_root *root, struct rb_node *node) {
    (void)root;
    (void)node;
    
    // Placeholder: Would remove node and rebalance tree
}

/**
 * Initialize CFS scheduler
 */
void cfs_init(void) {
    cfs_rq_init(&global_cfs_rq);
}

/**
 * Initialize CFS runqueue
 */
void cfs_rq_init(cfs_rq_t *cfs_rq) {
    rb_init(&cfs_rq->tasks_timeline);
    cfs_rq->rb_leftmost = 0;
    cfs_rq->min_vruntime = 0;
    cfs_rq->load_weight = 0;
    cfs_rq->nr_running = 0;
    cfs_rq->lock = 0;
}

/**
 * Add task to CFS runqueue
 */
void cfs_enqueue_task(cfs_rq_t *cfs_rq, cfs_task_t *task) {
    if (task->on_rq) {
        return;
    }
    
    // Insert into red-black tree
    rb_insert(&cfs_rq->tasks_timeline, &task->run_node, task->vruntime);
    
    // Update leftmost
    cfs_rq->rb_leftmost = rb_leftmost(&cfs_rq->tasks_timeline);
    
    // Update runqueue stats
    cfs_rq->load_weight += task->load_weight;
    cfs_rq->nr_running++;
    task->on_rq = 1;
}

/**
 * Remove task from CFS runqueue
 */
void cfs_dequeue_task(cfs_rq_t *cfs_rq, cfs_task_t *task) {
    if (!task->on_rq) {
        return;
    }
    
    // Remove from red-black tree
    rb_erase(&cfs_rq->tasks_timeline, &task->run_node);
    
    // Update leftmost
    cfs_rq->rb_leftmost = rb_leftmost(&cfs_rq->tasks_timeline);
    
    // Update runqueue stats
    cfs_rq->load_weight -= task->load_weight;
    cfs_rq->nr_running--;
    task->on_rq = 0;
}

/**
 * Pick next task to run
 */
cfs_task_t *cfs_pick_next_task(cfs_rq_t *cfs_rq) {
    struct rb_node *left = cfs_rq->rb_leftmost;
    
    if (!left) {
        return 0;
    }
    
    return (cfs_task_t *)((char *)left - __builtin_offsetof(cfs_task_t, run_node));
}

/**
 * Calculate task virtual runtime
 */
uint64_t cfs_calc_vruntime(cfs_task_t *task, uint64_t delta_exec) {
    uint64_t vruntime_delta;
    
    // Calculate weighted virtual runtime
    // vruntime increases faster for lower priority tasks
    vruntime_delta = (delta_exec * NICE_0_LOAD) / task->load_weight;
    
    return task->vruntime + vruntime_delta;
}

/**
 * Update task runtime
 */
void cfs_update_curr(cfs_rq_t *cfs_rq, cfs_task_t *task, uint64_t delta_exec) {
    // Update execution time
    task->sum_exec_runtime += delta_exec;
    
    // Update virtual runtime
    task->vruntime = cfs_calc_vruntime(task, delta_exec);
    
    // Update minimum vruntime
    if (task->vruntime > cfs_rq->min_vruntime) {
        cfs_rq->min_vruntime = task->vruntime;
    }
}

/**
 * Load balancing across CPUs
 */
void cfs_load_balance(int cpu) {
    (void)cpu;
    
    // Placeholder: Would find busiest CPU and migrate tasks
    // to balance load across all CPUs
}
