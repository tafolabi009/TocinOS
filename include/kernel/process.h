/**
 * TocinOS Process Management
 * 
 * Provides Unix-like process management on top of the task scheduler.
 * Implements fork(), exec(), wait() semantics.
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "../stdint.h"
#include "task.h"

// Maximum number of processes
#define MAX_PROCESSES       256

// Maximum open files per process
#define MAX_OPEN_FILES      16

// Maximum arguments for exec
#define MAX_EXEC_ARGS       32

// Process flags
#define PROC_FLAG_KERNEL    0x01    // Kernel process
#define PROC_FLAG_USER      0x02    // User process
#define PROC_FLAG_ZOMBIE    0x04    // Zombie (exited but not waited on)
#define PROC_FLAG_ORPHAN    0x08    // Orphaned (parent died)

// Wait options
#define WNOHANG             0x01    // Don't block if no child exited
#define WUNTRACED           0x02    // Also report stopped children

// Process states (maps to task states but with process semantics)
typedef enum {
    PROC_CREATED = 0,      // Just created
    PROC_READY,            // Ready to run
    PROC_RUNNING,          // Currently executing
    PROC_SLEEPING,         // Sleeping/blocked
    PROC_STOPPED,          // Stopped (e.g., by signal)
    PROC_ZOMBIE,           // Exited, waiting for wait()
    PROC_DEAD              // Fully terminated
} process_state_t;

// Process control block
typedef struct process {
    // Identity
    uint32_t pid;              // Process ID
    uint32_t ppid;             // Parent process ID
    uint32_t pgid;             // Process group ID
    uint32_t uid;              // User ID
    uint32_t gid;              // Group ID
    
    // State
    process_state_t state;
    int exit_code;             // Exit status
    uint32_t flags;
    
    // Memory management
    uint32_t page_directory;   // Physical address of page directory
    struct mm_struct *mm;      // VMA list + page tables (kernel/mm/vma.c);
                               // NULL for kernel processes without one
    uint32_t heap_start;       // Start of heap
    uint32_t heap_end;         // Current end of heap (brk)
    uint32_t stack_top;        // Top of user stack
    uint32_t stack_bottom;     // Bottom of user stack
    
    // Execution
    uint32_t entry_point;      // Entry point address
    uint32_t kernel_stack;     // Kernel stack for this process
    
    // File descriptors
    int files[MAX_OPEN_FILES]; // VFS file descriptors
    
    // Working directory
    char cwd[256];             // Current working directory path
    
    // Linked to task
    task_t *task;              // Underlying kernel task
    
    // Process tree
    struct process *parent;    // Parent process
    struct process *children;  // First child
    struct process *sibling;   // Next sibling
    
    // Waiting
    struct process *waiting_for; // Process we're waiting for
    
    // Name
    char name[64];             // Process name (from exec path)
} process_t;

// Process table entry
typedef struct {
    int in_use;
    process_t process;
} process_entry_t;

// Process management API
void process_init(void);
process_t *process_get_current(void);
process_t *process_get_by_pid(uint32_t pid);
uint32_t process_get_current_pid(void);

// Fork - create a copy of the current process
// Returns: 0 to child, child PID to parent, -1 on error
int32_t process_fork(void);

// Exec - replace current process image with new program
// Returns: -1 on error, doesn't return on success
int process_exec(const char *path, const char *argv[], const char *envp[]);

// Wait - wait for child process to exit
// Returns: PID of exited child, -1 on error
int32_t process_wait(int32_t pid, int *status, int options);

// Waitpid - same as wait (Linux compatibility)
int32_t process_waitpid(int32_t pid, int *status, int options);

// Exit - terminate current process
void process_exit(int exit_code);

// Create a new process (kernel-level, not user-visible)
process_t *process_create(const char *name, void (*entry)(void), uint32_t flags);

// Terminate a specific process
int process_kill(uint32_t pid, int signal);

// Process information
int process_getpid(void);
int process_getppid(void);

// Memory management for process
int process_brk(uint32_t addr);
void *process_sbrk(int32_t increment);

// File descriptor management
int process_dup_fd(int oldfd);
int process_dup2_fd(int oldfd, int newfd);
int process_close_fd(int fd);

// Working directory
int process_chdir(const char *path);
int process_getcwd(char *buf, uint32_t size);

// Process hierarchy helpers
void process_adopt_orphans(process_t *dead_parent);
void process_reap_zombies(void);

#endif // PROCESS_H
