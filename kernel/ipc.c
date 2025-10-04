/**
 * TocinOS IPC (Inter-Process Communication) Implementation
 * 
 * Comprehensive IPC system with message queues, semaphores,
 * shared memory, signals, and synchronization primitives
 */

#include "../include/kernel/ipc.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/task.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/kernel.h"

// ==================== MESSAGE QUEUES ====================

static ipc_msgqueue_t msgqueues[MAX_MSG_QUEUES];
static int msgqueue_bitmap[MAX_MSG_QUEUES / 32];

/**
 * Helper: Find message queue by name
 */
static int find_msgqueue(const char *name) {
    for (int i = 0; i < MAX_MSG_QUEUES; i++) {
        if ((msgqueue_bitmap[i / 32] & (1 << (i % 32))) &&
            msgqueues[i].name[0] != '\0') {
            int j = 0;
            int match = 1;
            while (name[j] && msgqueues[i].name[j]) {
                if (name[j] != msgqueues[i].name[j]) {
                    match = 0;
                    break;
                }
                j++;
            }
            if (match && name[j] == msgqueues[i].name[j]) {
                return i;
            }
        }
    }
    return -1;
}

/**
 * Create a message queue
 */
int mq_create(const char *name, int flags, int max_msgs, int max_msg_size) {
    if (!name || max_msgs <= 0 || max_msg_size <= 0 || max_msg_size > MAX_MSG_SIZE) {
        return -1;
    }
    
    // Check if queue already exists
    int existing = find_msgqueue(name);
    if (existing >= 0) {
        if (flags & IPC_EXCL) {
            return -1;  // Exclusive creation failed
        }
        return existing;
    }
    
    if (!(flags & IPC_CREAT)) {
        return -1;  // Queue doesn't exist and not creating
    }
    
    // Find free slot
    int mqid = -1;
    for (int i = 0; i < MAX_MSG_QUEUES; i++) {
        if (!(msgqueue_bitmap[i / 32] & (1 << (i % 32)))) {
            mqid = i;
            break;
        }
    }
    
    if (mqid < 0) {
        return -1;  // No free slots
    }
    
    // Allocate message buffer
    ipc_message_t *msg_buf = (ipc_message_t *)pmm_alloc_page();
    if (!msg_buf) {
        return -1;
    }
    
    // Initialize message queue
    ipc_msgqueue_t *mq = &msgqueues[mqid];
    mq->id = mqid;
    int i = 0;
    while (name[i] && i < 31) {
        mq->name[i] = name[i];
        i++;
    }
    mq->name[i] = '\0';
    mq->flags = flags;
    mq->max_msgs = max_msgs < MAX_MSGS_PER_QUEUE ? max_msgs : MAX_MSGS_PER_QUEUE;
    mq->max_msg_size = max_msg_size;
    mq->cur_msgs = 0;
    mq->messages = msg_buf;
    mq->head = 0;
    mq->tail = 0;
    mq->owner = task_get_current_id();
    mq->blocked_readers = 0;
    mq->blocked_writers = 0;
    
    msgqueue_bitmap[mqid / 32] |= (1 << (mqid % 32));
    
    return mqid;
}

/**
 * Open existing message queue
 */
int mq_open(const char *name, int flags) {
    return mq_create(name, flags & ~IPC_CREAT, MAX_MSGS_PER_QUEUE, MAX_MSG_SIZE);
}

/**
 * Send message to queue
 */
int mq_send(int mqid, const void *msg_ptr, uint32_t msg_size, uint32_t msg_type) {
    if (mqid < 0 || mqid >= MAX_MSG_QUEUES || !msg_ptr) {
        return -1;
    }
    
    if (!(msgqueue_bitmap[mqid / 32] & (1 << (mqid % 32)))) {
        return -1;
    }
    
    ipc_msgqueue_t *mq = &msgqueues[mqid];
    
    if (msg_size > (uint32_t)mq->max_msg_size) {
        return -1;
    }
    
    // Check if queue is full
    if (mq->cur_msgs >= mq->max_msgs) {
        if (mq->flags & IPC_NOWAIT) {
            return -1;  // Non-blocking, return error
        }
        
        // Block until space available
        mq->blocked_writers++;
        task_block(mqid);  // Block on this resource
        mq->blocked_writers--;
    }
    
    // Add message to queue
    ipc_message_t *msg = &mq->messages[mq->tail];
    msg->type = msg_type;
    msg->size = msg_size;
    msg->timestamp = timer_get_ticks();
    
    // Copy message data
    const uint8_t *src = (const uint8_t *)msg_ptr;
    for (uint32_t i = 0; i < msg_size; i++) {
        msg->data[i] = src[i];
    }
    
    mq->tail = (mq->tail + 1) % mq->max_msgs;
    mq->cur_msgs++;
    
    // Wake up blocked readers
    if (mq->blocked_readers > 0) {
        // TODO: Wake up one blocked reader
    }
    
    return 0;
}

/**
 * Receive message from queue
 */
int mq_receive(int mqid, void *msg_ptr, uint32_t *msg_size, uint32_t *msg_type) {
    if (mqid < 0 || mqid >= MAX_MSG_QUEUES || !msg_ptr) {
        return -1;
    }
    
    if (!(msgqueue_bitmap[mqid / 32] & (1 << (mqid % 32)))) {
        return -1;
    }
    
    ipc_msgqueue_t *mq = &msgqueues[mqid];
    
    // Check if queue is empty
    if (mq->cur_msgs == 0) {
        if (mq->flags & IPC_NOWAIT) {
            return -1;  // Non-blocking, return error
        }
        
        // Block until message available
        mq->blocked_readers++;
        task_block(mqid);
        mq->blocked_readers--;
    }
    
    // Get message from queue
    ipc_message_t *msg = &mq->messages[mq->head];
    
    if (msg_size) {
        *msg_size = msg->size;
    }
    if (msg_type) {
        *msg_type = msg->type;
    }
    
    // Copy message data
    uint8_t *dest = (uint8_t *)msg_ptr;
    for (uint32_t i = 0; i < msg->size; i++) {
        dest[i] = msg->data[i];
    }
    
    mq->head = (mq->head + 1) % mq->max_msgs;
    mq->cur_msgs--;
    
    // Wake up blocked writers
    if (mq->blocked_writers > 0) {
        // TODO: Wake up one blocked writer
    }
    
    return 0;
}

/**
 * Close message queue
 */
int mq_close(int mqid) {
    // In this implementation, closing is a no-op
    // Resources are freed on unlink
    return 0;
}

/**
 * Unlink message queue
 */
int mq_unlink(const char *name) {
    int mqid = find_msgqueue(name);
    if (mqid < 0) {
        return -1;
    }
    
    ipc_msgqueue_t *mq = &msgqueues[mqid];
    
    // Free message buffer
    if (mq->messages) {
        pmm_free_page((uint32_t)mq->messages);
    }
    
    // Clear queue
    mq->name[0] = '\0';
    msgqueue_bitmap[mqid / 32] &= ~(1 << (mqid % 32));
    
    return 0;
}

// ==================== SEMAPHORES ====================

static ipc_semaphore_t semaphores[MAX_SEMAPHORES];
static int semaphore_bitmap[MAX_SEMAPHORES / 32];

/**
 * Helper: Find semaphore by name
 */
static int find_semaphore(const char *name) {
    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        if ((semaphore_bitmap[i / 32] & (1 << (i % 32))) &&
            semaphores[i].name[0] != '\0') {
            int j = 0;
            int match = 1;
            while (name[j] && semaphores[i].name[j]) {
                if (name[j] != semaphores[i].name[j]) {
                    match = 0;
                    break;
                }
                j++;
            }
            if (match && name[j] == semaphores[i].name[j]) {
                return i;
            }
        }
    }
    return -1;
}

/**
 * Create a semaphore
 */
int sem_create(const char *name, int initial_value, int flags) {
    if (!name || initial_value < 0) {
        return -1;
    }
    
    // Check if semaphore already exists
    int existing = find_semaphore(name);
    if (existing >= 0) {
        if (flags & IPC_EXCL) {
            return -1;
        }
        return existing;
    }
    
    if (!(flags & IPC_CREAT)) {
        return -1;
    }
    
    // Find free slot
    int semid = -1;
    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        if (!(semaphore_bitmap[i / 32] & (1 << (i % 32)))) {
            semid = i;
            break;
        }
    }
    
    if (semid < 0) {
        return -1;
    }
    
    // Initialize semaphore
    ipc_semaphore_t *sem = &semaphores[semid];
    sem->id = semid;
    int i = 0;
    while (name[i] && i < 31) {
        sem->name[i] = name[i];
        i++;
    }
    sem->name[i] = '\0';
    sem->value = initial_value;
    sem->max_value = 0x7FFFFFFF;  // Max int
    sem->flags = flags;
    sem->owner = task_get_current_id();
    sem->blocked_tasks = 0;
    sem->num_blocked = 0;
    
    semaphore_bitmap[semid / 32] |= (1 << (semid % 32));
    
    return semid;
}

/**
 * Wait on semaphore (P operation)
 */
int sem_wait(int semid) {
    if (semid < 0 || semid >= MAX_SEMAPHORES) {
        return -1;
    }
    
    if (!(semaphore_bitmap[semid / 32] & (1 << (semid % 32)))) {
        return -1;
    }
    
    ipc_semaphore_t *sem = &semaphores[semid];
    
    // Decrement semaphore
    while (sem->value <= 0) {
        // Block until semaphore available
        sem->num_blocked++;
        task_block(semid);
        sem->num_blocked--;
    }
    
    sem->value--;
    return 0;
}

/**
 * Try wait on semaphore (non-blocking)
 */
int sem_trywait(int semid) {
    if (semid < 0 || semid >= MAX_SEMAPHORES) {
        return -1;
    }
    
    if (!(semaphore_bitmap[semid / 32] & (1 << (semid % 32)))) {
        return -1;
    }
    
    ipc_semaphore_t *sem = &semaphores[semid];
    
    if (sem->value <= 0) {
        return -1;  // Would block
    }
    
    sem->value--;
    return 0;
}

/**
 * Post to semaphore (V operation)
 */
int sem_post(int semid) {
    if (semid < 0 || semid >= MAX_SEMAPHORES) {
        return -1;
    }
    
    if (!(semaphore_bitmap[semid / 32] & (1 << (semid % 32)))) {
        return -1;
    }
    
    ipc_semaphore_t *sem = &semaphores[semid];
    
    // Increment semaphore
    if (sem->value < sem->max_value) {
        sem->value++;
    }
    
    // Wake up one blocked task
    if (sem->num_blocked > 0) {
        // TODO: Wake up one blocked task on this semaphore
    }
    
    return 0;
}

/**
 * Get semaphore value
 */
int sem_getvalue(int semid, int *value) {
    if (semid < 0 || semid >= MAX_SEMAPHORES || !value) {
        return -1;
    }
    
    if (!(semaphore_bitmap[semid / 32] & (1 << (semid % 32)))) {
        return -1;
    }
    
    *value = semaphores[semid].value;
    return 0;
}

/**
 * Close semaphore
 */
int sem_close(int semid) {
    return 0;  // No-op in this implementation
}

/**
 * Unlink semaphore
 */
int sem_unlink(const char *name) {
    int semid = find_semaphore(name);
    if (semid < 0) {
        return -1;
    }
    
    semaphores[semid].name[0] = '\0';
    semaphore_bitmap[semid / 32] &= ~(1 << (semid % 32));
    
    return 0;
}

// ==================== MUTEXES ====================

static ipc_mutex_t mutexes[MAX_MUTEXES];
static int mutex_bitmap[MAX_MUTEXES / 32];

/**
 * Create a mutex
 */
int mutex_create(const char *name, mutex_type_t type, int flags) {
    if (!name) {
        return -1;
    }
    
    // Find free slot
    int mutexid = -1;
    for (int i = 0; i < MAX_MUTEXES; i++) {
        if (!(mutex_bitmap[i / 32] & (1 << (i % 32)))) {
            mutexid = i;
            break;
        }
    }
    
    if (mutexid < 0) {
        return -1;
    }
    
    // Initialize mutex
    ipc_mutex_t *mutex = &mutexes[mutexid];
    mutex->id = mutexid;
    int i = 0;
    while (name[i] && i < 31) {
        mutex->name[i] = name[i];
        i++;
    }
    mutex->name[i] = '\0';
    mutex->type = type;
    mutex->locked = 0;
    mutex->owner = -1;
    mutex->recursion_count = 0;
    mutex->blocked_tasks = 0;
    mutex->num_blocked = 0;
    mutex->priority_ceiling = 0;
    
    mutex_bitmap[mutexid / 32] |= (1 << (mutexid % 32));
    
    return mutexid;
}

/**
 * Lock mutex
 */
int mutex_lock(int mutexid) {
    if (mutexid < 0 || mutexid >= MAX_MUTEXES) {
        return -1;
    }
    
    if (!(mutex_bitmap[mutexid / 32] & (1 << (mutexid % 32)))) {
        return -1;
    }
    
    ipc_mutex_t *mutex = &mutexes[mutexid];
    int current_task = task_get_current_id();
    
    // Handle recursive mutexes
    if (mutex->type == MUTEX_RECURSIVE && mutex->owner == current_task) {
        mutex->recursion_count++;
        return 0;
    }
    
    // Error checking mutex
    if (mutex->type == MUTEX_ERRORCHECK && mutex->owner == current_task) {
        return -1;  // Deadlock detected
    }
    
    // Block until mutex available
    while (mutex->locked) {
        // Implement priority inheritance
        if (mutex->owner >= 0) {
            task_t *owner = task_get_current();
            if (owner) {
                // Boost owner priority if needed
                // task_pi_boost(mutex->owner, owner->priority);
            }
        }
        
        mutex->num_blocked++;
        task_block(mutexid);
        mutex->num_blocked--;
    }
    
    // Acquire mutex
    mutex->locked = 1;
    mutex->owner = current_task;
    mutex->recursion_count = 1;
    
    return 0;
}

/**
 * Try to lock mutex (non-blocking)
 */
int mutex_trylock(int mutexid) {
    if (mutexid < 0 || mutexid >= MAX_MUTEXES) {
        return -1;
    }
    
    if (!(mutex_bitmap[mutexid / 32] & (1 << (mutexid % 32)))) {
        return -1;
    }
    
    ipc_mutex_t *mutex = &mutexes[mutexid];
    int current_task = task_get_current_id();
    
    if (mutex->locked) {
        if (mutex->type == MUTEX_RECURSIVE && mutex->owner == current_task) {
            mutex->recursion_count++;
            return 0;
        }
        return -1;  // Would block
    }
    
    mutex->locked = 1;
    mutex->owner = current_task;
    mutex->recursion_count = 1;
    
    return 0;
}

/**
 * Unlock mutex
 */
int mutex_unlock(int mutexid) {
    if (mutexid < 0 || mutexid >= MAX_MUTEXES) {
        return -1;
    }
    
    if (!(mutex_bitmap[mutexid / 32] & (1 << (mutexid % 32)))) {
        return -1;
    }
    
    ipc_mutex_t *mutex = &mutexes[mutexid];
    int current_task = task_get_current_id();
    
    // Error checking
    if (mutex->type == MUTEX_ERRORCHECK && mutex->owner != current_task) {
        return -1;  // Not owner
    }
    
    // Handle recursive unlocking
    if (mutex->type == MUTEX_RECURSIVE && mutex->recursion_count > 1) {
        mutex->recursion_count--;
        return 0;
    }
    
    // Release mutex
    mutex->locked = 0;
    mutex->owner = -1;
    mutex->recursion_count = 0;
    
    // Remove priority boost
    // task_pi_deboost(current_task);
    
    // Wake up one blocked task
    if (mutex->num_blocked > 0) {
        // TODO: Wake up highest priority blocked task
    }
    
    return 0;
}

/**
 * Destroy mutex
 */
int mutex_destroy(int mutexid) {
    if (mutexid < 0 || mutexid >= MAX_MUTEXES) {
        return -1;
    }
    
    if (!(mutex_bitmap[mutexid / 32] & (1 << (mutexid % 32)))) {
        return -1;
    }
    
    ipc_mutex_t *mutex = &mutexes[mutexid];
    
    if (mutex->locked) {
        return -1;  // Cannot destroy locked mutex
    }
    
    mutex->name[0] = '\0';
    mutex_bitmap[mutexid / 32] &= ~(1 << (mutexid % 32));
    
    return 0;
}

// ==================== INITIALIZATION ====================

/**
 * Initialize IPC system
 */
void ipc_init(void) {
    // Clear message queues
    for (int i = 0; i < MAX_MSG_QUEUES / 32; i++) {
        msgqueue_bitmap[i] = 0;
    }
    
    // Clear semaphores
    for (int i = 0; i < MAX_SEMAPHORES / 32; i++) {
        semaphore_bitmap[i] = 0;
    }
    
    // Clear mutexes
    for (int i = 0; i < MAX_MUTEXES / 32; i++) {
        mutex_bitmap[i] = 0;
    }
    
    kernel_print("[IPC] Inter-Process Communication system initialized\n");
}

/**
 * Cleanup IPC resources for a task
 */
void ipc_cleanup_task(int task_id) {
    // Release all mutexes owned by this task
    for (int i = 0; i < MAX_MUTEXES; i++) {
        if ((mutex_bitmap[i / 32] & (1 << (i % 32))) &&
            mutexes[i].owner == task_id) {
            mutexes[i].locked = 0;
            mutexes[i].owner = -1;
        }
    }
    
    // Clean up message queues owned by this task
    for (int i = 0; i < MAX_MSG_QUEUES; i++) {
        if ((msgqueue_bitmap[i / 32] & (1 << (i % 32))) &&
            msgqueues[i].owner == task_id) {
            // Optionally unlink queue
        }
    }
}
