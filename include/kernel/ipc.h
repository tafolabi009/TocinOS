/**
 * TocinOS IPC (Inter-Process Communication) System
 * 
 * Advanced IPC mechanisms including:
 * - Message Queues (POSIX-style)
 * - Semaphores (counting and binary)
 * - Shared Memory Segments
 * - POSIX Signals
 * - Mutexes with Priority Inheritance
 * - Condition Variables
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>

// ==================== MESSAGE QUEUES ====================

#define MAX_MSG_QUEUES 64
#define MAX_MSG_SIZE 8192
#define MAX_MSGS_PER_QUEUE 32

typedef struct {
    uint32_t type;           // Message type (for filtering)
    uint32_t size;           // Message size in bytes
    uint64_t timestamp;      // Message timestamp
    uint8_t data[MAX_MSG_SIZE];  // Message data
} ipc_message_t;

typedef struct {
    int id;                  // Queue ID
    char name[32];           // Queue name
    int flags;               // Creation flags
    int max_msgs;            // Maximum messages
    int max_msg_size;        // Maximum message size
    int cur_msgs;            // Current message count
    ipc_message_t *messages; // Message buffer
    int head;                // Queue head
    int tail;                // Queue tail
    int owner;               // Owner task ID
    int blocked_readers;     // Tasks blocked on read
    int blocked_writers;     // Tasks blocked on write
} ipc_msgqueue_t;

// Message queue flags
#define IPC_CREAT      0x001  // Create if doesn't exist
#define IPC_EXCL       0x002  // Fail if exists
#define IPC_NOWAIT     0x004  // Non-blocking operation

// Message queue operations
int mq_create(const char *name, int flags, int max_msgs, int max_msg_size);
int mq_open(const char *name, int flags);
int mq_close(int mqid);
int mq_unlink(const char *name);
int mq_send(int mqid, const void *msg_ptr, uint32_t msg_size, uint32_t msg_type);
int mq_receive(int mqid, void *msg_ptr, uint32_t *msg_size, uint32_t *msg_type);
int mq_timedreceive(int mqid, void *msg_ptr, uint32_t *msg_size, uint32_t *msg_type, uint64_t timeout);
int mq_getattr(int mqid, int *cur_msgs, int *max_msgs);

// ==================== SEMAPHORES ====================

#define MAX_SEMAPHORES 128

typedef struct {
    int id;                  // Semaphore ID
    char name[32];           // Semaphore name
    int value;               // Semaphore value
    int max_value;           // Maximum value (for bounds checking)
    int flags;               // Creation flags
    int owner;               // Owner task ID
    int *blocked_tasks;      // Array of blocked task IDs
    int num_blocked;         // Number of blocked tasks
} ipc_semaphore_t;

// Semaphore operations
int sem_create(const char *name, int initial_value, int flags);
int sem_open(const char *name, int flags);
int sem_close(int semid);
int sem_unlink(const char *name);
int sem_wait(int semid);           // P() operation - decrement (blocking)
int sem_trywait(int semid);        // Non-blocking wait
int sem_timedwait(int semid, uint64_t timeout);
int sem_post(int semid);           // V() operation - increment
int sem_getvalue(int semid, int *value);

// ==================== MUTEXES ====================

#define MAX_MUTEXES 128

typedef enum {
    MUTEX_NORMAL = 0,        // Standard mutex
    MUTEX_RECURSIVE = 1,     // Recursive locking allowed
    MUTEX_ERRORCHECK = 2     // Error checking
} mutex_type_t;

typedef struct {
    int id;                  // Mutex ID
    char name[32];           // Mutex name
    mutex_type_t type;       // Mutex type
    int locked;              // Lock state (0 = unlocked, 1 = locked)
    int owner;               // Owner task ID
    int recursion_count;     // For recursive mutexes
    int *blocked_tasks;      // Array of blocked task IDs
    int num_blocked;         // Number of blocked tasks
    int priority_ceiling;    // For priority inheritance
} ipc_mutex_t;

// Mutex operations
int mutex_create(const char *name, mutex_type_t type, int flags);
int mutex_destroy(int mutexid);
int mutex_lock(int mutexid);
int mutex_trylock(int mutexid);
int mutex_timedlock(int mutexid, uint64_t timeout);
int mutex_unlock(int mutexid);

// ==================== CONDITION VARIABLES ====================

#define MAX_CONDVARS 64

typedef struct {
    int id;                  // Condition variable ID
    char name[32];           // Condition variable name
    int *waiting_tasks;      // Tasks waiting on condition
    int num_waiting;         // Number of waiting tasks
} ipc_condvar_t;

// Condition variable operations
int condvar_create(const char *name, int flags);
int condvar_destroy(int cvid);
int condvar_wait(int cvid, int mutexid);
int condvar_timedwait(int cvid, int mutexid, uint64_t timeout);
int condvar_signal(int cvid);        // Wake one waiting task
int condvar_broadcast(int cvid);     // Wake all waiting tasks

// ==================== SHARED MEMORY ====================

#define MAX_SHMEM_SEGMENTS 32
#define MAX_SHMEM_SIZE (1024 * 1024)  // 1MB max per segment

typedef struct {
    int id;                  // Shared memory ID
    char name[32];           // Segment name
    uint32_t size;           // Segment size in bytes
    void *addr;              // Kernel virtual address
    int flags;               // Creation flags
    int owner;               // Owner task ID
    int ref_count;           // Number of attached processes
    int *attached_tasks;     // List of attached task IDs
} ipc_shm_segment_t;

// Shared memory flags
#define SHM_RDONLY     0x001  // Read-only attachment

// Shared memory operations
int shm_create(const char *name, uint32_t size, int flags);
int shm_open(const char *name, int flags);
void* shm_attach(int shmid, int flags);
int shm_detach(const void *addr);
int shm_unlink(const char *name);
int shm_getinfo(int shmid, uint32_t *size, int *ref_count);

// ==================== SIGNALS ====================

#define MAX_SIGNALS 32

// Standard POSIX signals
#define SIGHUP     1   // Hangup
#define SIGINT     2   // Interrupt
#define SIGQUIT    3   // Quit
#define SIGILL     4   // Illegal instruction
#define SIGTRAP    5   // Trace trap
#define SIGABRT    6   // Abort
#define SIGBUS     7   // Bus error
#define SIGFPE     8   // Floating point exception
#define SIGKILL    9   // Kill (cannot be caught)
#define SIGUSR1    10  // User-defined signal 1
#define SIGSEGV    11  // Segmentation violation
#define SIGUSR2    12  // User-defined signal 2
#define SIGPIPE    13  // Broken pipe
#define SIGALRM    14  // Alarm clock
#define SIGTERM    15  // Termination
#define SIGCHLD    17  // Child status changed
#define SIGCONT    18  // Continue
#define SIGSTOP    19  // Stop (cannot be caught)
#define SIGTSTP    20  // Keyboard stop

typedef void (*signal_handler_t)(int signum);

// Signal actions
#define SIG_DFL ((signal_handler_t)0)  // Default action
#define SIG_IGN ((signal_handler_t)1)  // Ignore signal

// Signal operations
int signal_send(int task_id, int signum);
signal_handler_t signal_set(int signum, signal_handler_t handler);
int signal_block(int signum);
int signal_unblock(int signum);
int signal_pending(int signum);
int signal_wait(int signum);

// ==================== PIPES ====================

#define MAX_PIPES 64
#define PIPE_BUF_SIZE 4096

typedef struct {
    int id;                  // Pipe ID
    uint8_t buffer[PIPE_BUF_SIZE];
    int read_pos;            // Read position
    int write_pos;           // Write position
    int count;               // Bytes in buffer
    int reader_count;        // Number of readers
    int writer_count;        // Number of writers
    int blocked_readers;     // Tasks blocked on read
    int blocked_writers;     // Tasks blocked on write
} ipc_pipe_t;

// Pipe operations
int pipe_create(int pipefd[2]);
int pipe_close(int fd);
int pipe_read(int fd, void *buf, uint32_t count);
int pipe_write(int fd, const void *buf, uint32_t count);

// ==================== EVENT NOTIFICATION ====================

#define MAX_EVENTS 128
#define MAX_EVENT_LISTENERS 16

typedef enum {
    EVENT_ONESHOT = 0x001,   // Auto-disable after trigger
    EVENT_LEVEL = 0x002,     // Level-triggered
    EVENT_EDGE = 0x004       // Edge-triggered
} event_flags_t;

typedef struct {
    int id;                  // Event ID
    char name[32];           // Event name
    int flags;               // Event flags
    int signaled;            // Event state
    int *listeners;          // Tasks waiting on event
    int num_listeners;       // Number of listeners
    uint64_t data;           // Associated data
} ipc_event_t;

// Event operations
int event_create(const char *name, int flags);
int event_destroy(int eventid);
int event_wait(int eventid, uint64_t timeout);
int event_signal(int eventid, uint64_t data);
int event_reset(int eventid);

// ==================== IPC INITIALIZATION ====================

void ipc_init(void);
void ipc_cleanup_task(int task_id);  // Called when task exits

#endif // IPC_H
