/**
 * TocinOS Threading Support
 * 
 * Implements POSIX-like threads with clone(), futex, and TLS support.
 * This is Phase 3.2 of the production OS roadmap.
 * 
 * Features:
 * - clone() syscall for creating threads
 * - Futex-based synchronization primitives
 * - Thread-Local Storage (TLS) with GS segment
 * - Thread group management
 */

#include "../../include/kernel/task.h"
#include "../../include/kernel/syscall.h"
#include "../../include/kernel/memory.h"
#include "../../include/kernel/kernel.h"
#include "../../include/kernel/timer.h"

// External from scheduler.c
#define MAX_TASKS 256
extern task_t* get_task_by_id(int id);  // Forward declaration, we'll add a getter

// Thread ID counter
static uint32_t next_tid = 1;

// Futex wait queue hash table
#define FUTEX_HASH_SIZE 256
#define FUTEX_HASH(addr) (((uint32_t)(addr) >> 2) & (FUTEX_HASH_SIZE - 1))

typedef struct futex_bucket {
    task_t *head;
    task_t *tail;
} futex_bucket_t;

static futex_bucket_t futex_hash[FUTEX_HASH_SIZE];

// GDT entry structure for TLS
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// TLS GDT entries start at entry 6 (after null, code, data, code16, data16, TSS)
#define TLS_GDT_START 6
#define TLS_GDT_COUNT 3   // Up to 3 TLS entries per thread

// Static GDT storage for TLS entries
static struct gdt_entry tls_gdt_entries[TLS_GDT_COUNT];

/**
 * Initialize futex subsystem
 */
void futex_init(void) {
    for (int i = 0; i < FUTEX_HASH_SIZE; i++) {
        futex_hash[i].head = 0;
        futex_hash[i].tail = 0;
    }
    kernel_print("[THREAD] Futex subsystem initialized\n");
}

/**
 * Allocate a new thread ID
 */
static uint32_t alloc_tid(void) {
    return next_tid++;
}

/**
 * Get current thread ID
 */
uint32_t thread_self(void) {
    task_t *current = task_get_current();
    if (current) {
        return current->tid;
    }
    return 0;
}

/**
 * Check if task is thread group leader
 */
int thread_is_group_leader(task_t *task) {
    return task && task->tid == task->tgid;
}

/**
 * Get thread group ID (same as getpid())
 */
int thread_get_tgid(uint32_t tid) {
    // Find task by tid - for now use linear search
    // In production, use a hash table
    task_t *current = task_get_current();
    if (current && current->tid == tid) {
        return current->tgid;
    }
    return -1;
}

/**
 * Count threads in a group
 */
int thread_group_count(task_t *leader) {
    if (!leader || !thread_is_group_leader(leader)) {
        return 0;
    }
    return leader->thread_count;
}

/**
 * Add thread to thread group
 */
static void thread_group_add(task_t *leader, task_t *thread) {
    if (!leader || !thread) return;
    
    // Insert at end of thread list
    task_t *last = leader;
    while (last->thread_next) {
        last = last->thread_next;
    }
    
    last->thread_next = thread;
    thread->thread_prev = last;
    thread->thread_next = 0;
    thread->group_leader = leader;
    thread->tgid = leader->tgid;
    leader->thread_count++;
}

/**
 * Remove thread from thread group
 */
static void thread_group_remove(task_t *thread) {
    if (!thread || !thread->group_leader) return;
    
    task_t *leader = thread->group_leader;
    
    if (thread->thread_prev) {
        thread->thread_prev->thread_next = thread->thread_next;
    }
    if (thread->thread_next) {
        thread->thread_next->thread_prev = thread->thread_prev;
    }
    
    thread->thread_prev = 0;
    thread->thread_next = 0;
    thread->group_leader = 0;
    
    if (leader->thread_count > 0) {
        leader->thread_count--;
    }
}

/**
 * Set up GDT entry for TLS
 * We use a local array since the GDT is assembly-defined
 * In a real implementation, we'd modify the actual GDT
 */
static void set_tls_gdt_entry(int entry, uint32_t base, uint32_t limit) {
    if (entry < 0 || entry >= TLS_GDT_COUNT) {
        return;
    }
    
    tls_gdt_entries[entry].limit_low = limit & 0xFFFF;
    tls_gdt_entries[entry].base_low = base & 0xFFFF;
    tls_gdt_entries[entry].base_middle = (base >> 16) & 0xFF;
    tls_gdt_entries[entry].access = 0xF2;      // Present, ring 3, data segment, writable
    tls_gdt_entries[entry].granularity = 0x40 | ((limit >> 16) & 0x0F);  // 32-bit, byte granularity
    tls_gdt_entries[entry].base_high = (base >> 24) & 0xFF;
}

/**
 * Set up TLS for a task
 */
int tls_setup(task_t *task, uint32_t tls_base, uint32_t tls_size) {
    extern void serial_printf(const char *fmt, ...);
    
    if (!task) return -1;
    
    task->tls_base = tls_base;
    task->tls_size = tls_size;
    
    // Assign a GDT entry for this thread's TLS
    // For simplicity, use TLS_GDT_START + (tid % TLS_GDT_COUNT)
    // In production, manage GDT entries more carefully
    int gdt_entry = TLS_GDT_START + (task->tid % TLS_GDT_COUNT);
    task->tls_gdt_entry = gdt_entry;
    
    set_tls_gdt_entry(gdt_entry, tls_base, tls_size);
    
    serial_printf("[TLS] Setup TLS for tid=%u base=0x%x size=%u gdt=%d\n",
                  task->tid, tls_base, tls_size, gdt_entry);
    
    return 0;
}

/**
 * Get current thread's TLS base
 */
uint32_t tls_get_base(void) {
    task_t *current = task_get_current();
    if (current) {
        return current->tls_base;
    }
    return 0;
}

/**
 * Load TLS segment for current task (called during context switch)
 */
void tls_load(task_t *task) {
    if (!task || !task->tls_base) return;
    
    // Set up the GDT entry
    set_tls_gdt_entry(task->tls_gdt_entry, task->tls_base, task->tls_size);
    
    // Load GS with the TLS segment selector
    // Selector = (gdt_entry * 8) | 3 (ring 3)
    uint16_t selector = (task->tls_gdt_entry * 8) | 3;
    __asm__ volatile("mov %0, %%gs" : : "r"(selector));
}

/**
 * Clone - create a new thread or process
 * 
 * flags: CLONE_* flags determining what to share
 * child_stack: stack pointer for new thread
 * parent_tid: where to store parent's view of child TID
 * child_tid: where to store child's TID
 * tls: TLS base address for new thread
 * 
 * Returns: TID of new thread to parent, 0 to child, -1 on error
 * 
 * NOTE: Full implementation requires integration with scheduler.
 * This is a framework showing the clone semantics.
 */
int thread_clone(uint32_t flags, void *child_stack, uint32_t *parent_tid,
                 uint32_t *child_tid, void *tls) {
    extern void serial_printf(const char *fmt, ...);
    
    task_t *current = task_get_current();
    if (!current) {
        serial_printf("[CLONE] Error: no current task\n");
        return -1;
    }
    
    serial_printf("[CLONE] flags=0x%x stack=0x%x tls=0x%x\n", 
                  flags, (uint32_t)child_stack, (uint32_t)tls);
    
    // For now, we use the existing task_create_ex function
    // and set up the thread-specific fields afterward
    // 
    // In a full implementation, we would:
    // 1. Allocate a new task from the task pool
    // 2. Copy/share resources based on flags
    // 3. Set up the new thread's stack and registers
    // 4. Add to scheduler run queue
    
    // Create a new task using the scheduler's task creation
    // We pass NULL entry point because we'll set EIP directly
    int new_tid = alloc_tid();
    
    serial_printf("[CLONE] Allocated new TID: %u\n", new_tid);
    
    // For CLONE_THREAD, the child shares the address space
    if (flags & CLONE_THREAD) {
        serial_printf("[CLONE] CLONE_THREAD: sharing address space\n");
    }
    
    // Handle CLONE_SETTLS
    if ((flags & CLONE_SETTLS) && tls) {
        serial_printf("[CLONE] Setting TLS base to 0x%x\n", (uint32_t)tls);
        // TLS will be set up when the thread is scheduled
    }
    
    // Handle CLONE_PARENT_SETTID
    if ((flags & CLONE_PARENT_SETTID) && parent_tid) {
        *parent_tid = new_tid;
    }
    
    // Handle CLONE_CHILD_SETTID
    if ((flags & CLONE_CHILD_SETTID) && child_tid) {
        *child_tid = new_tid;
    }
    
    serial_printf("[CLONE] Thread created: tid=%u\n", new_tid);
    serial_printf("[CLONE] Note: Full threading requires scheduler integration\n");
    
    // In a complete implementation, return new_tid to parent, 0 to child
    // For now, just return the new TID
    return new_tid;
}

/**
 * Thread exit - called when a thread terminates
 */
void thread_exit(int exit_code) {
    extern void serial_printf(const char *fmt, ...);
    
    task_t *current = task_get_current();
    if (!current) {
        return;
    }
    
    serial_printf("[THREAD] Thread tid=%u exiting with code %d\n", 
                  current->tid, exit_code);
    
    current->exit_code = exit_code;
    current->thread_flags |= THREAD_EXITED;
    
    // Clear child TID if requested
    if (current->clear_child_tid) {
        *current->clear_child_tid = 0;
        // Wake any thread waiting on this address
        futex_wake(current->clear_child_tid, 1);
    }
    
    // Remove from thread group
    if (!(current->thread_flags & THREAD_DETACHED)) {
        // Mark as zombie so parent can join
        current->state = TASK_ZOMBIE;
    } else {
        // Detached thread - remove from group and clean up
        thread_group_remove(current);
        current->state = TASK_TERMINATED;
    }
    
    // If this is the group leader and last thread, exit the process
    if (thread_is_group_leader(current) && current->thread_count == 0) {
        // TODO: Clean up process resources
        current->state = TASK_TERMINATED;
    }
    
    // Yield CPU
    scheduler_schedule();
}

/**
 * Wait for a thread to exit
 * 
 * NOTE: This is a simplified implementation. Full implementation
 * requires proper thread lookup and blocking.
 */
int thread_join(uint32_t tid, int *exit_code) {
    extern void serial_printf(const char *fmt, ...);
    
    serial_printf("[THREAD] thread_join called for tid=%u\n", tid);
    
    // In a full implementation, we would:
    // 1. Look up the thread by TID in a hash table
    // 2. Check if it's joinable (not detached)
    // 3. If not exited, block the current thread using futex
    // 4. When target exits, wake us and return its exit code
    
    // For now, return a placeholder
    if (exit_code) {
        *exit_code = 0;
    }
    
    serial_printf("[THREAD] thread_join: stub implementation\n");
    return 0;
}

/**
 * Detach a thread
 * 
 * NOTE: Simplified implementation - full version needs thread lookup.
 */
int thread_detach(uint32_t tid) {
    extern void serial_printf(const char *fmt, ...);
    
    serial_printf("[THREAD] thread_detach called for tid=%u\n", tid);
    serial_printf("[THREAD] thread_detach: stub implementation\n");
    
    // In a full implementation, we would:
    // 1. Look up the thread by TID
    // 2. Set THREAD_DETACHED flag
    // 3. If thread already exited, clean it up
    
    return 0;
}

/**
 * Futex wait - block until *uaddr != val or timeout
 */
int futex_wait(uint32_t *uaddr, uint32_t val, uint32_t timeout) {
    extern void serial_printf(const char *fmt, ...);
    
    task_t *current = task_get_current();
    if (!current || !uaddr) {
        return -1;
    }
    
    // Atomic check: if *uaddr != val, return immediately
    if (*uaddr != val) {
        return -1;  // EAGAIN equivalent
    }
    
    serial_printf("[FUTEX] tid=%u waiting on 0x%x (val=%u)\n", 
                  current->tid, (uint32_t)uaddr, val);
    
    // Add to futex wait queue
    int hash = FUTEX_HASH(uaddr);
    futex_bucket_t *bucket = &futex_hash[hash];
    
    current->futex_addr = uaddr;
    current->futex_next = 0;
    current->futex_prev = bucket->tail;
    
    if (bucket->tail) {
        bucket->tail->futex_next = current;
    } else {
        bucket->head = current;
    }
    bucket->tail = current;
    
    // Block the thread
    current->state = TASK_BLOCKED;
    current->blocked_on = (int)(uint32_t)uaddr;
    
    if (timeout > 0) {
        current->sleep_until = timer_get_ticks() + timeout;
    }
    
    // Yield CPU
    scheduler_schedule();
    
    // Woken up - remove from queue if still there
    if (current->futex_addr) {
        // Timed out or spurious wakeup
        if (current->futex_prev) {
            current->futex_prev->futex_next = current->futex_next;
        } else {
            bucket->head = current->futex_next;
        }
        if (current->futex_next) {
            current->futex_next->futex_prev = current->futex_prev;
        } else {
            bucket->tail = current->futex_prev;
        }
        current->futex_addr = 0;
        current->futex_next = 0;
        current->futex_prev = 0;
    }
    
    serial_printf("[FUTEX] tid=%u woke from 0x%x\n", current->tid, (uint32_t)uaddr);
    
    return 0;
}

/**
 * Futex wake - wake up to 'count' waiters on uaddr
 */
int futex_wake(uint32_t *uaddr, uint32_t count) {
    extern void serial_printf(const char *fmt, ...);
    
    if (!uaddr || count == 0) {
        return 0;
    }
    
    int hash = FUTEX_HASH(uaddr);
    futex_bucket_t *bucket = &futex_hash[hash];
    
    int woken = 0;
    task_t *waiter = bucket->head;
    
    while (waiter && woken < (int)count) {
        task_t *next = waiter->futex_next;
        
        if (waiter->futex_addr == uaddr) {
            serial_printf("[FUTEX] Waking tid=%u from 0x%x\n", 
                          waiter->tid, (uint32_t)uaddr);
            
            // Remove from queue
            if (waiter->futex_prev) {
                waiter->futex_prev->futex_next = waiter->futex_next;
            } else {
                bucket->head = waiter->futex_next;
            }
            if (waiter->futex_next) {
                waiter->futex_next->futex_prev = waiter->futex_prev;
            } else {
                bucket->tail = waiter->futex_prev;
            }
            
            waiter->futex_addr = 0;
            waiter->futex_next = 0;
            waiter->futex_prev = 0;
            
            // Wake the thread
            waiter->state = TASK_READY;
            waiter->blocked_on = 0;
            
            woken++;
        }
        
        waiter = next;
    }
    
    return woken;
}

// ===== System Call Wrappers =====

/**
 * sys_clone - clone() system call
 */
int sys_clone(uint32_t flags, uint32_t stack, uint32_t parent_tid, 
              uint32_t child_tid, uint32_t tls) {
    return thread_clone(flags, (void *)stack, (uint32_t *)parent_tid,
                        (uint32_t *)child_tid, (void *)tls);
}

/**
 * sys_gettid - get thread ID
 */
int sys_gettid(void) {
    return (int)thread_self();
}

/**
 * sys_futex - futex system call
 */
int sys_futex(uint32_t uaddr, uint32_t op, uint32_t val, 
              uint32_t timeout, uint32_t uaddr2) {
    (void)uaddr2;  // Not used yet
    
    switch (op & 0x7F) {  // Mask off FUTEX_PRIVATE
    case FUTEX_WAIT:
        return futex_wait((uint32_t *)uaddr, val, timeout);
    case FUTEX_WAKE:
        return futex_wake((uint32_t *)uaddr, val);
    default:
        return -1;  // ENOSYS
    }
}

/**
 * sys_set_tls - set TLS base address
 */
int sys_set_tls(uint32_t tls_base) {
    task_t *current = task_get_current();
    if (!current) {
        return -1;
    }
    
    return tls_setup(current, tls_base, PAGE_SIZE);
}

/**
 * sys_get_tls - get TLS base address
 */
int sys_get_tls(void) {
    return (int)tls_get_base();
}

/**
 * sys_tkill - send signal to specific thread
 */
int sys_tkill(uint32_t tid, uint32_t sig) {
    // TODO: Implement signals
    (void)tid;
    (void)sig;
    return -1;  // ENOSYS
}

/**
 * sys_exit_group - exit all threads in group
 */
int sys_exit_group(uint32_t exit_code) {
    extern void serial_printf(const char *fmt, ...);
    
    task_t *current = task_get_current();
    if (!current) {
        return -1;
    }
    
    task_t *leader = current->group_leader ? current->group_leader : current;
    
    serial_printf("[THREAD] exit_group called by tid=%u, terminating tgid=%u\n",
                  current->tid, leader->tgid);
    
    // Mark all threads in group as terminated
    task_t *thread = leader;
    while (thread) {
        task_t *next = thread->thread_next;
        thread->exit_code = exit_code;
        thread->state = TASK_TERMINATED;
        thread = next;
    }
    
    // Never returns
    scheduler_schedule();
    
    return 0;
}

/**
 * Initialize threading subsystem
 */
void thread_init(void) {
    // Initialize futex system
    futex_init();
    
    // Initialize next TID from 1
    next_tid = 1;
    
    kernel_print("[THREAD] Threading subsystem initialized\n");
}
