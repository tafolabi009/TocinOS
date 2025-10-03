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

// Maximum number of system calls
#define SYSCALL_MAX     32

// System call handler type
typedef int (*syscall_handler_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

// Initialize system call interface
void syscall_init(void);

// Register a system call handler
void syscall_register(uint32_t num, syscall_handler_t handler);

// System call implementations
int sys_exit(uint32_t code);
int sys_write(uint32_t fd, uint32_t buf, uint32_t count);
int sys_read(uint32_t fd, uint32_t buf, uint32_t count);
int sys_gettime(void);
int sys_sleep(uint32_t ticks);

#endif // SYSCALL_H
