/**
 * TocinOS POSIX Threads Implementation
 * 
 * Uses clone() for thread creation and futex() for synchronization.
 */

#include "pthread.h"

// We avoid including syscall.h to prevent conflicts
// Define our own syscall wrappers here

// ===== System Call Definitions =====

#define SYS_EXIT        0
#define SYS_MMAP        13
#define SYS_MUNMAP      14
#define SYS_CLONE       27
#define SYS_GETTID      28
#define SYS_FUTEX       29
#define SYS_SET_TLS     30
#define SYS_EXIT_GROUP  33
#define SYS_SCHED_YIELD 35

// Clone flags
#define CLONE_VM        0x00000100
#define CLONE_FS        0x00000200
#define CLONE_FILES     0x00000400
#define CLONE_SIGHAND   0x00000800
#define CLONE_THREAD    0x00010000
#define CLONE_SETTLS    0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID   0x01000000

// Futex operations
#define FUTEX_WAIT      0
#define FUTEX_WAKE      1

// Memory mapping flags
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

// ===== Syscall Wrappers (pthread-specific) =====

static inline int pthread_syscall0(int num) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int pthread_syscall1(int num, int a1) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int pthread_syscall2(int num, int a1, int a2) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int pthread_syscall4(int num, int a1, int a2, int a3, int a4) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return ret;
}

static inline int pthread_syscall5(int num, int a1, int a2, int a3, int a4, int a5) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory"
    );
    return ret;
}

// ===== Thread-Local Storage =====

// Per-thread data structure (at top of each thread's stack)
struct pthread_internal {
    pthread_t tid;              // Thread ID
    void *(*start_routine)(void *);  // Entry function
    void *arg;                  // Argument to entry function
    void *retval;               // Return value
    int detached;               // Is thread detached?
    volatile int exited;        // Has thread exited?
    void *tls_values[PTHREAD_KEYS_MAX];  // Thread-specific data
    struct pthread_internal *self;  // Pointer to self (for TLS access)
};

// Key allocation
static volatile int key_bitmap[PTHREAD_KEYS_MAX / 32];
static void (*key_destructors[PTHREAD_KEYS_MAX])(void *);

// Get thread control block
static struct pthread_internal *get_tcb(void) {
    struct pthread_internal *tcb;
    // Read from GS:0 where we store the self pointer
    __asm__ volatile("movl %%gs:0, %0" : "=r"(tcb));
    return tcb;
}

// ===== Helper Functions =====

// Atomic compare-and-swap
static inline int atomic_cas(volatile int *ptr, int old, int new) {
    int result;
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a"(result), "+m"(*ptr)
        : "r"(new), "0"(old)
        : "memory"
    );
    return result == old;
}

// Atomic exchange
static inline int atomic_xchg(volatile int *ptr, int val) {
    __asm__ volatile(
        "xchgl %0, %1"
        : "=r"(val), "+m"(*ptr)
        : "0"(val)
        : "memory"
    );
    return val;
}

// Memory barrier
static inline void barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

// ===== Thread Entry Point =====

// Assembly trampoline for new threads
__attribute__((naked)) 
static void thread_start_trampoline(void) {
    __asm__ volatile(
        // Stack has: tcb pointer at top
        "popl %%eax\n"          // Get tcb pointer
        "movl %%eax, %%gs:0\n"  // Store in TLS
        
        // Get start_routine and arg from tcb
        "movl 4(%%eax), %%ebx\n"   // start_routine
        "movl 8(%%eax), %%ecx\n"   // arg
        
        // Call start_routine(arg)
        "pushl %%ecx\n"
        "call *%%ebx\n"
        "addl $4, %%esp\n"
        
        // Thread returned - call pthread_exit with return value
        "pushl %%eax\n"
        "call pthread_exit\n"
        
        // Never reached
        "1: jmp 1b\n"
        ::: "memory"
    );
}

// ===== Thread Functions =====

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)attr;  // TODO: Use attributes
    
    // Allocate stack (64KB default)
    unsigned long stack_size = PTHREAD_STACK_DEFAULT;
    void *stack = (void *)pthread_syscall5(SYS_MMAP, 0, stack_size,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1);
    if ((long)stack < 0) {
        return EAGAIN;
    }
    
    // Put pthread_internal at top of stack (stack grows down)
    char *stack_top = (char *)stack + stack_size;
    struct pthread_internal *tcb = (struct pthread_internal *)(stack_top - sizeof(struct pthread_internal));
    
    // Initialize tcb
    tcb->start_routine = start_routine;
    tcb->arg = arg;
    tcb->retval = 0;
    tcb->detached = 0;
    tcb->exited = 0;
    tcb->self = tcb;
    
    // Clear TLS values
    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
        tcb->tls_values[i] = 0;
    }
    
    // Set up stack for thread_start_trampoline
    unsigned long *sp = (unsigned long *)tcb;
    *--sp = (unsigned long)tcb;  // Push tcb pointer for trampoline
    
    // Clone flags for thread creation
    unsigned long flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                          CLONE_THREAD | CLONE_SETTLS | CLONE_PARENT_SETTID |
                          CLONE_CHILD_CLEARTID;
    
    // Create the thread
    // clone(flags, stack, &parent_tid, &child_tid, tls)
    long tid = pthread_syscall5(SYS_CLONE, flags, (long)sp, 
                        (long)&tcb->tid, (long)&tcb->tid, (long)tcb);
    
    if (tid < 0) {
        pthread_syscall2(SYS_MUNMAP, (long)stack, stack_size);
        return EAGAIN;
    }
    
    // Note: In a real implementation, the child would start executing
    // thread_start_trampoline here. For now, since our clone is simplified,
    // we just record the TID.
    tcb->tid = tid;
    
    *thread = (pthread_t)tcb;
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    struct pthread_internal *tcb = (struct pthread_internal *)thread;
    
    if (!tcb || tcb->detached) {
        return EINVAL;
    }
    
    // Wait for thread to exit using futex
    while (!tcb->exited) {
        pthread_syscall4(SYS_FUTEX, (long)&tcb->exited, FUTEX_WAIT, 0, 0);
    }
    
    if (retval) {
        *retval = tcb->retval;
    }
    
    // Free stack (tcb is at top of stack)
    unsigned long stack_base = (unsigned long)tcb - PTHREAD_STACK_DEFAULT + sizeof(struct pthread_internal);
    pthread_syscall2(SYS_MUNMAP, stack_base, PTHREAD_STACK_DEFAULT);
    
    return 0;
}

int pthread_detach(pthread_t thread) {
    struct pthread_internal *tcb = (struct pthread_internal *)thread;
    
    if (!tcb) {
        return EINVAL;
    }
    
    tcb->detached = 1;
    
    // If already exited, clean up now
    if (tcb->exited) {
        unsigned long stack_base = (unsigned long)tcb - PTHREAD_STACK_DEFAULT + sizeof(struct pthread_internal);
        pthread_syscall2(SYS_MUNMAP, stack_base, PTHREAD_STACK_DEFAULT);
    }
    
    return 0;
}

void pthread_exit(void *retval) {
    struct pthread_internal *tcb = get_tcb();
    
    if (tcb) {
        // Call TLS destructors
        for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
            if (tcb->tls_values[i] && key_destructors[i]) {
                key_destructors[i](tcb->tls_values[i]);
            }
        }
        
        tcb->retval = retval;
        tcb->exited = 1;
        
        // Wake any waiting joiner
        pthread_syscall4(SYS_FUTEX, (long)&tcb->exited, FUTEX_WAKE, 1, 0);
        
        // If detached, would clean up here
    }
    
    // Exit this thread only (not the process)
    pthread_syscall1(0, 0);  // SYS_EXIT
    
    // Never reached
    for (;;) {}
}

pthread_t pthread_self(void) {
    struct pthread_internal *tcb = get_tcb();
    return tcb ? (pthread_t)tcb : 0;
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

int pthread_yield(void) {
    pthread_syscall0(SYS_SCHED_YIELD);
    return 0;
}

// ===== Mutex Functions =====

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    mutex->lock = 0;
    mutex->waiters = 0;
    mutex->owner = 0;
    mutex->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (mutex->lock) {
        return EBUSY;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    // Fast path: try to acquire lock
    if (atomic_cas(&mutex->lock, 0, 1)) {
        mutex->owner = pthread_self();
        return 0;
    }
    
    // Slow path: contention - use futex
    while (1) {
        // Mark that we're waiting
        int expected = 1;
        if (atomic_cas(&mutex->lock, 1, 2)) {
            // Lock is contended
            expected = 2;
        }
        
        // Try to acquire
        if (atomic_cas(&mutex->lock, 0, 2)) {
            mutex->owner = pthread_self();
            return 0;
        }
        
        // Wait on futex
        __sync_fetch_and_add(&mutex->waiters, 1);
        pthread_syscall4(SYS_FUTEX, (long)&mutex->lock, FUTEX_WAIT, expected, 0);
        __sync_fetch_and_sub(&mutex->waiters, 1);
    }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (atomic_cas(&mutex->lock, 0, 1)) {
        mutex->owner = pthread_self();
        return 0;
    }
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (mutex->owner != pthread_self()) {
        return EINVAL;
    }
    
    mutex->owner = 0;
    
    // Fast path: no waiters
    if (atomic_xchg(&mutex->lock, 0) == 1) {
        return 0;
    }
    
    // Slow path: wake a waiter
    pthread_syscall4(SYS_FUTEX, (long)&mutex->lock, FUTEX_WAKE, 1, 0);
    
    return 0;
}

// ===== Condition Variable Functions =====

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    cond->seq = 0;
    cond->waiters = 0;
    cond->mutex = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    if (cond->waiters) {
        return EBUSY;
    }
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int seq = cond->seq;
    cond->mutex = mutex;
    
    __sync_fetch_and_add(&cond->waiters, 1);
    
    // Release mutex before sleeping
    pthread_mutex_unlock(mutex);
    
    // Wait for signal
    pthread_syscall4(SYS_FUTEX, (long)&cond->seq, FUTEX_WAIT, seq, 0);
    
    __sync_fetch_and_sub(&cond->waiters, 1);
    
    // Reacquire mutex
    pthread_mutex_lock(mutex);
    
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    (void)abstime;  // TODO: Implement timeout
    return pthread_cond_wait(cond, mutex);
}

int pthread_cond_signal(pthread_cond_t *cond) {
    if (cond->waiters == 0) {
        return 0;
    }
    
    __sync_fetch_and_add(&cond->seq, 1);
    pthread_syscall4(SYS_FUTEX, (long)&cond->seq, FUTEX_WAKE, 1, 0);
    
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    if (cond->waiters == 0) {
        return 0;
    }
    
    __sync_fetch_and_add(&cond->seq, 1);
    pthread_syscall4(SYS_FUTEX, (long)&cond->seq, FUTEX_WAKE, cond->waiters, 0);
    
    return 0;
}

// ===== Thread-Local Storage =====

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    // Find free key
    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
        int word = i / 32;
        int bit = i % 32;
        
        if (!(key_bitmap[word] & (1 << bit))) {
            // Try to allocate
            int old = key_bitmap[word];
            if (atomic_cas((volatile int *)&key_bitmap[word], old, old | (1 << bit))) {
                key_destructors[i] = destructor;
                *key = i;
                return 0;
            }
        }
    }
    
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) {
        return EINVAL;
    }
    
    int word = key / 32;
    int bit = key % 32;
    
    key_destructors[key] = 0;
    __sync_fetch_and_and((volatile int *)&key_bitmap[word], ~(1 << bit));
    
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) {
        return 0;
    }
    
    struct pthread_internal *tcb = get_tcb();
    return tcb ? tcb->tls_values[key] : 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= PTHREAD_KEYS_MAX) {
        return EINVAL;
    }
    
    struct pthread_internal *tcb = get_tcb();
    if (!tcb) {
        return EINVAL;
    }
    
    tcb->tls_values[key] = (void *)value;
    return 0;
}

// ===== Once Functions =====

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (once_control->done) {
        return 0;
    }
    
    pthread_mutex_lock(&once_control->mutex);
    
    if (!once_control->done) {
        init_routine();
        once_control->done = 1;
    }
    
    pthread_mutex_unlock(&once_control->mutex);
    
    return 0;
}

// ===== Attribute Functions =====

int pthread_attr_init(pthread_attr_t *attr) {
    *attr = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
    *attr = detachstate;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
    *detachstate = *attr;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    (void)attr;
    (void)stacksize;
    // TODO: Store stack size in attr
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
    (void)attr;
    *stacksize = PTHREAD_STACK_DEFAULT;
    return 0;
}
