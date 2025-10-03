/**
 * TocinOS Preemptive Multitasking Scheduler
 * 
 * Priority-based task scheduler with round-robin for same priority tasks
 */

#include "../include/kernel/task.h"
#include "../include/kernel/memory.h"

#define MAX_TASKS 64
#define TASK_STACK_SIZE 8192
#define PRIORITY_LEVELS 8

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct task {
    unsigned int id;
    unsigned int esp;           // Stack pointer
    unsigned int ebp;           // Base pointer
    unsigned int eip;           // Instruction pointer
    unsigned int priority;      // 0 = highest, 7 = lowest
    task_state_t state;
    struct task *next;
} task_t;

static task_t tasks[MAX_TASKS];
static task_t *current_task = 0;
static task_t *ready_queue[PRIORITY_LEVELS] = {0};
static unsigned int next_task_id = 0;
static unsigned int scheduler_enabled = 0;

/**
 * Initialize the task scheduler
 */
void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_TERMINATED;
        tasks[i].next = 0;
    }
    
    for (int i = 0; i < PRIORITY_LEVELS; i++) {
        ready_queue[i] = 0;
    }
    
    scheduler_enabled = 0;
}

/**
 * Create a new task
 */
unsigned int task_create(void (*entry_point)(void), unsigned int priority) {
    if (next_task_id >= MAX_TASKS) {
        return 0; // No available task slots
    }
    
    if (priority >= PRIORITY_LEVELS) {
        priority = PRIORITY_LEVELS - 1;
    }
    
    task_t *task = &tasks[next_task_id];
    task->id = next_task_id++;
    task->priority = priority;
    task->state = TASK_READY;
    
    // Allocate stack
    unsigned int stack = pmm_alloc_page();
    task->esp = stack + TASK_STACK_SIZE;
    task->ebp = task->esp;
    task->eip = (unsigned int)entry_point;
    
    // Add to ready queue
    task->next = ready_queue[priority];
    ready_queue[priority] = task;
    
    return task->id;
}

/**
 * Find next ready task
 */
static task_t *scheduler_get_next_task(void) {
    // Find highest priority task
    for (int priority = 0; priority < PRIORITY_LEVELS; priority++) {
        if (ready_queue[priority] != 0) {
            task_t *task = ready_queue[priority];
            ready_queue[priority] = task->next;
            
            // Add to end of queue (round-robin)
            if (ready_queue[priority] == 0) {
                ready_queue[priority] = task;
            } else {
                task_t *tail = ready_queue[priority];
                while (tail->next != 0) {
                    tail = tail->next;
                }
                tail->next = task;
            }
            task->next = 0;
            
            return task;
        }
    }
    return 0;
}

/**
 * Schedule next task
 */
void scheduler_schedule(void) {
    if (!scheduler_enabled) {
        return;
    }
    
    // Save current task state
    if (current_task != 0 && current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }
    
    // Get next task
    task_t *next_task = scheduler_get_next_task();
    if (next_task == 0) {
        return; // No tasks to run
    }
    
    next_task->state = TASK_RUNNING;
    current_task = next_task;
    
    // Context switch (simplified - would need proper register saving/restoration)
    // This is a placeholder for the actual context switch implementation
}

/**
 * Start the scheduler
 */
void scheduler_start(void) {
    scheduler_enabled = 1;
    
    if (ready_queue[0] != 0 || ready_queue[1] != 0) {
        scheduler_schedule();
    }
}

/**
 * Block current task
 */
void task_block(void) {
    if (current_task != 0) {
        current_task->state = TASK_BLOCKED;
        scheduler_schedule();
    }
}

/**
 * Unblock a task
 */
void task_unblock(unsigned int task_id) {
    if (task_id < MAX_TASKS) {
        task_t *task = &tasks[task_id];
        if (task->state == TASK_BLOCKED) {
            task->state = TASK_READY;
            // Add back to ready queue
            task->next = ready_queue[task->priority];
            ready_queue[task->priority] = task;
        }
    }
}

/**
 * Terminate current task
 */
void task_exit(void) {
    if (current_task != 0) {
        current_task->state = TASK_TERMINATED;
        
        // Free task stack
        pmm_free_page(current_task->esp - TASK_STACK_SIZE);
        
        scheduler_schedule();
    }
}

/**
 * Get current task ID
 */
unsigned int task_get_current_id(void) {
    return current_task ? current_task->id : 0;
}

/**
 * Yield CPU to next task
 */
void task_yield(void) {
    scheduler_schedule();
}
