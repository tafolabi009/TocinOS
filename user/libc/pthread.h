/**
 * TocinOS POSIX Threads (pthread) Library
 * 
 * Implements POSIX-compatible threading for userspace applications.
 * Uses the clone() and futex() system calls.
 * 
 * Features:
 * - pthread_create/join/detach/exit
 * - pthread_mutex with futex
 * - pthread_cond for condition variables
 * - Thread-Local Storage via pthread_key
 */

#ifndef PTHREAD_H
#define PTHREAD_H

// Basic types for freestanding environment
typedef unsigned int uint32_t;
typedef unsigned long size_t;
typedef long time_t;

// Time specification for timed operations
struct timespec {
    time_t tv_sec;    // seconds
    long tv_nsec;     // nanoseconds
};
// Thread types
typedef unsigned long pthread_t;
typedef uint32_t pthread_attr_t;

// Mutex types
typedef struct {
    volatile int lock;       // 0 = unlocked, 1 = locked
    volatile int waiters;    // Number of waiting threads
    pthread_t owner;         // Owner thread ID (for recursive mutexes)
    int type;                // Mutex type
} pthread_mutex_t;

typedef uint32_t pthread_mutexattr_t;

// Condition variable types
typedef struct {
    volatile int seq;        // Sequence number for wakeups
    volatile int waiters;    // Number of waiting threads
    pthread_mutex_t *mutex;  // Associated mutex
} pthread_cond_t;

typedef uint32_t pthread_condattr_t;

// Once control
typedef struct {
    volatile int done;
    pthread_mutex_t mutex;
} pthread_once_t;

// Thread-specific data key
typedef unsigned int pthread_key_t;

// Constants
#define PTHREAD_CREATE_JOINABLE   0
#define PTHREAD_CREATE_DETACHED   1

#define PTHREAD_MUTEX_NORMAL      0
#define PTHREAD_MUTEX_RECURSIVE   1
#define PTHREAD_MUTEX_ERRORCHECK  2
#define PTHREAD_MUTEX_DEFAULT     PTHREAD_MUTEX_NORMAL

#define PTHREAD_COND_INITIALIZER  { 0, 0, 0 }
#define PTHREAD_MUTEX_INITIALIZER { 0, 0, 0, PTHREAD_MUTEX_NORMAL }
#define PTHREAD_ONCE_INIT         { 0, PTHREAD_MUTEX_INITIALIZER }

// Stack size defaults
#define PTHREAD_STACK_MIN         4096
#define PTHREAD_STACK_DEFAULT     (64 * 1024)

// Maximum keys for thread-local storage
#define PTHREAD_KEYS_MAX          128

// Error codes
#define EAGAIN      11
#define EINVAL      22
#define EDEADLK     35
#define EBUSY       16
#define ETIMEDOUT   110

// ===== Thread Functions =====

/**
 * Create a new thread
 * 
 * thread: where to store the new thread ID
 * attr: thread attributes (can be NULL for defaults)
 * start_routine: function to execute
 * arg: argument to pass to start_routine
 * 
 * Returns 0 on success, error code on failure
 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);

/**
 * Wait for a thread to terminate
 * 
 * thread: thread to wait for
 * retval: where to store the thread's return value
 * 
 * Returns 0 on success, error code on failure
 */
int pthread_join(pthread_t thread, void **retval);

/**
 * Detach a thread (make it not joinable)
 * 
 * thread: thread to detach
 * 
 * Returns 0 on success, error code on failure
 */
int pthread_detach(pthread_t thread);

/**
 * Terminate the calling thread
 * 
 * retval: return value to pass to joining thread
 */
void pthread_exit(void *retval) __attribute__((noreturn));

/**
 * Get the calling thread's ID
 */
pthread_t pthread_self(void);

/**
 * Compare thread IDs
 * 
 * Returns non-zero if equal, 0 if different
 */
int pthread_equal(pthread_t t1, pthread_t t2);

/**
 * Yield the processor to other threads
 */
int pthread_yield(void);

// ===== Mutex Functions =====

/**
 * Initialize a mutex
 */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);

/**
 * Destroy a mutex
 */
int pthread_mutex_destroy(pthread_mutex_t *mutex);

/**
 * Lock a mutex
 */
int pthread_mutex_lock(pthread_mutex_t *mutex);

/**
 * Try to lock a mutex without blocking
 */
int pthread_mutex_trylock(pthread_mutex_t *mutex);

/**
 * Unlock a mutex
 */
int pthread_mutex_unlock(pthread_mutex_t *mutex);

// ===== Condition Variable Functions =====

/**
 * Initialize a condition variable
 */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);

/**
 * Destroy a condition variable
 */
int pthread_cond_destroy(pthread_cond_t *cond);

/**
 * Wait on a condition variable
 */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);

/**
 * Wait on a condition variable with timeout
 */
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);

/**
 * Wake one thread waiting on a condition variable
 */
int pthread_cond_signal(pthread_cond_t *cond);

/**
 * Wake all threads waiting on a condition variable
 */
int pthread_cond_broadcast(pthread_cond_t *cond);

// ===== Thread-Local Storage Functions =====

/**
 * Create a thread-specific data key
 */
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));

/**
 * Delete a thread-specific data key
 */
int pthread_key_delete(pthread_key_t key);

/**
 * Get thread-specific data
 */
void *pthread_getspecific(pthread_key_t key);

/**
 * Set thread-specific data
 */
int pthread_setspecific(pthread_key_t key, const void *value);

// ===== Once Functions =====

/**
 * Call init_routine exactly once
 */
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

// ===== Attribute Functions =====

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);

#endif // PTHREAD_H
