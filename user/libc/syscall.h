/**
 * TocinOS User-Space Syscall Interface
 * 
 * Low-level syscall wrappers for user programs
 */

#ifndef _LIBC_SYSCALL_H
#define _LIBC_SYSCALL_H

// System call numbers (must match kernel/syscall.h)
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
#define SYS_BRK         11
#define SYS_SBRK        12
#define SYS_MMAP        13
#define SYS_MUNMAP      14
#define SYS_GETCWD      15
#define SYS_CHDIR       16
#define SYS_STAT        17
#define SYS_LSEEK       18
#define SYS_DUP         19
#define SYS_DUP2        20
#define SYS_PIPE        21
#define SYS_OPENDIR     22
#define SYS_READDIR     23
#define SYS_CLOSEDIR    24
#define SYS_WAITPID     25
#define SYS_SPAWN       26

// File open flags
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// Standard file descriptors
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

// Syscall with 0 arguments
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

// Syscall with 1 argument
static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

// Syscall with 2 arguments
static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2)
        : "memory"
    );
    return ret;
}

// Syscall with 3 arguments
static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

// Syscall with 4 arguments
static inline int syscall4(int num, int arg1, int arg2, int arg3, int arg4) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
        : "memory"
    );
    return ret;
}

// Syscall with 5 arguments
static inline int syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5)
        : "memory"
    );
    return ret;
}

#endif /* _LIBC_SYSCALL_H */
