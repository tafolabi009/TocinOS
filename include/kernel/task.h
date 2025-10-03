/**
 * TocinOS Task Scheduler Header
 */

#ifndef TASK_H
#define TASK_H

// Scheduler functions
void scheduler_init(void);
void scheduler_start(void);
void scheduler_schedule(void);

// Task management
unsigned int task_create(void (*entry_point)(void), unsigned int priority);
void task_block(void);
void task_unblock(unsigned int task_id);
void task_exit(void);
unsigned int task_get_current_id(void);
void task_yield(void);

#endif // TASK_H
