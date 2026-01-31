/**
 * System Call Interface for TocinOS
 * 
 * Provides user space to kernel space transition
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// System call numbers
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_OPEN        3
#define SYS_CLOSE       4
#define SYS_GETPID      5
#define SYS_FORK        6
#define SYS_EXEC        7
#define SYS_WAIT        8
#define SYS_SLEEP       9
#define SYS_GETTIME     10
#define SYS_BRK         11   // Heap memory allocation
#define SYS_SBRK        12   // Increment heap
#define SYS_MMAP        13   // Memory mapping
#define SYS_MUNMAP      14   // Memory unmapping
#define SYS_GETCWD      15   // Get current working directory
#define SYS_CHDIR       16   // Change directory
#define SYS_STAT        17   // Get file status
#define SYS_LSEEK       18   // Seek in file
#define SYS_DUP         19   // Duplicate file descriptor
#define SYS_DUP2        20   // Duplicate file descriptor to specific fd
#define SYS_PIPE        21   // Create pipe
#define SYS_OPENDIR     22   // Open directory for listing
#define SYS_READDIR     23   // Read directory entries
#define SYS_CLOSEDIR    24   // Close directory
#define SYS_WAITPID     25   // Wait for specific process
#define SYS_SPAWN       26   // Spawn and run a program (simplified exec)

// Threading syscalls (Phase 3.2)
#define SYS_CLONE       27   // Create thread/process (like Linux clone)
#define SYS_GETTID      28   // Get thread ID
#define SYS_FUTEX       29   // Fast userspace mutex
#define SYS_SET_TLS     30   // Set thread-local storage base
#define SYS_GET_TLS     31   // Get thread-local storage base
#define SYS_TKILL       32   // Send signal to specific thread
#define SYS_EXIT_GROUP  33   // Exit all threads in group

// Maximum number of system calls
#define SYSCALL_MAX     64

// System call handler type
typedef int (*syscall_handler_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

// Initialize system call interface
void syscall_init(void);

// Register a system call handler
void syscall_register(uint32_t num, syscall_handler_t handler);

// Core system call implementations
int sys_exit(uint32_t code);
int sys_write(uint32_t fd, uint32_t buf, uint32_t count);
int sys_read(uint32_t fd, uint32_t buf, uint32_t count);
int sys_open(uint32_t path, uint32_t flags, uint32_t mode);
int sys_close(uint32_t fd);
int sys_getpid(void);
int sys_fork(void);
int sys_exec(uint32_t path, uint32_t argv, uint32_t envp);
int sys_wait(uint32_t pid, uint32_t status, uint32_t options);
int sys_waitpid(uint32_t pid, uint32_t status, uint32_t options);
int sys_sleep(uint32_t ticks);
int sys_gettime(void);

// Memory system calls
int sys_brk(uint32_t addr);
int sys_sbrk(uint32_t increment);
int sys_mmap(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, uint32_t fd);
int sys_munmap(uint32_t addr, uint32_t length);

// File system calls
int sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence);
int sys_getcwd(uint32_t buf, uint32_t size);
int sys_chdir(uint32_t path);

// Directory system calls
int sys_opendir(uint32_t path, uint32_t reserved);
int sys_readdir(uint32_t path, uint32_t entries, uint32_t max_entries);
int sys_closedir(uint32_t dir);

// Program execution
int sys_spawn(uint32_t path, uint32_t argv, uint32_t envp);

// Threading system calls (Phase 3.2)
int sys_clone(uint32_t flags, uint32_t stack, uint32_t parent_tid, uint32_t child_tid, uint32_t tls);
int sys_gettid(void);
int sys_futex(uint32_t uaddr, uint32_t op, uint32_t val, uint32_t timeout, uint32_t uaddr2);
int sys_set_tls(uint32_t tls_base);
int sys_get_tls(void);
int sys_tkill(uint32_t tid, uint32_t sig);
int sys_exit_group(uint32_t exit_code);

#endif // SYSCALL_H
