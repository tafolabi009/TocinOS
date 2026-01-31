/**
 * TocinOS Standard C Library Header
 * 
 * Basic C library functions for user programs
 */

#ifndef _LIBC_H
#define _LIBC_H

#include "syscall.h"

// Types
typedef unsigned int size_t;
typedef int ssize_t;
typedef int pid_t;

// NULL pointer
#define NULL ((void*)0)

// Directory entry structure (must match kernel FAT dir entry - 32 bytes)
typedef struct {
    char name[11];              // 8.3 filename
    unsigned char attr;         // File attributes
    unsigned char reserved;     // Reserved
    unsigned char creation_time_tenth;
    unsigned short creation_time;
    unsigned short creation_date;
    unsigned short last_access_date;
    unsigned short cluster_high;    // First cluster high word
    unsigned short last_write_time;
    unsigned short last_write_date;
    unsigned short cluster_low;     // First cluster low word
    unsigned int size;              // File size
} __attribute__((packed)) dirent_t;

// Directory stream
typedef struct {
    int fd;                 // Directory file descriptor
    int index;              // Current entry index
    dirent_t entries[64];   // Cached entries
    int count;              // Number of entries
} DIR;

// File stat structure
typedef struct {
    unsigned int size;
    unsigned char attr;
    unsigned short date;
    unsigned short time;
} stat_t;

//=============================================================================
// System Call Wrappers
//=============================================================================

// Exit program with status code
static inline void exit(int status) {
    syscall1(SYS_EXIT, status);
    while(1) __asm__ volatile("hlt"); // Never returns
}

// Write to file descriptor
static inline ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

// Read from file descriptor
static inline ssize_t read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

// Open a file
static inline int open(const char *path, int flags) {
    return syscall2(SYS_OPEN, (int)path, flags);
}

// Close a file descriptor
static inline int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

// Get process ID
static inline pid_t getpid(void) {
    return syscall0(SYS_GETPID);
}

// Fork current process
static inline pid_t fork(void) {
    return syscall0(SYS_FORK);
}

// Execute a program
static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall3(SYS_EXEC, (int)path, (int)argv, (int)envp);
}

// Wait for child process
static inline pid_t wait(int *status) {
    return syscall3(SYS_WAIT, -1, (int)status, 0);
}

// Spawn and run a program (simplified fork+exec+wait)
// Returns: exit code of spawned program, or -1 on error
static inline int spawn(const char *path) {
    return syscall3(SYS_SPAWN, (int)path, 0, 0);
}

// Wait for specific child
static inline pid_t waitpid(pid_t pid, int *status, int options) {
    return syscall3(SYS_WAITPID, pid, (int)status, options);
}

// Sleep for ticks
static inline int sleep(unsigned int seconds) {
    return syscall1(SYS_SLEEP, seconds * 100); // Convert to ticks
}

// Open directory
static inline DIR *opendir(const char *path) {
    int count = syscall2(SYS_OPENDIR, (int)path, 0);
    if (count < 0) return NULL;
    
    // Allocate DIR structure (static for now, no malloc)
    static DIR dir_static;
    dir_static.index = 0;
    dir_static.count = count;
    
    // Read directory entries
    syscall3(SYS_READDIR, (int)path, (int)dir_static.entries, 64);
    
    return &dir_static;
}

// Read directory entry
static inline dirent_t *readdir(DIR *dir) {
    if (!dir || dir->index >= dir->count) return NULL;
    return &dir->entries[dir->index++];
}

// Close directory
static inline int closedir(DIR *dir) {
    if (!dir) return -1;
    dir->index = 0;
    dir->count = 0;
    return 0;
}

//=============================================================================
// String Functions
//=============================================================================

// String length
static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

// String copy
static inline char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

// String copy with length limit
static inline char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

// String compare
static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// String compare with length limit
static inline int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

// String concatenate
static inline char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

// Find character in string
static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

//=============================================================================
// Memory Functions
//=============================================================================

// Memory copy
static inline void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

// Memory set
static inline void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

// Memory compare
static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char*)s1;
    const unsigned char *p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

//=============================================================================
// Character Functions
//=============================================================================

static inline int isdigit(int c) {
    return c >= '0' && c <= '9';
}

static inline int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static inline int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static inline int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

//=============================================================================
// Number Conversion
//=============================================================================

// Integer to string
static inline char *itoa(int value, char *str, int base) {
    char *p = str;
    char *p1, *p2;
    unsigned int v;
    int negative = 0;
    
    if (value < 0 && base == 10) {
        negative = 1;
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }
    
    // Generate digits in reverse order
    do {
        int digit = v % base;
        *p++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        v /= base;
    } while (v > 0);
    
    if (negative) *p++ = '-';
    *p = '\0';
    
    // Reverse the string
    p1 = str;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
    
    return str;
}

// String to integer
static inline int atoi(const char *str) {
    int result = 0;
    int sign = 1;
    
    while (isspace(*str)) str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

//=============================================================================
// Printing Functions
//=============================================================================

// Print string to stdout
static inline int puts(const char *s) {
    int len = strlen(s);
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
    return len + 1;
}

// Print single character
static inline int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

// Simple printf implementation
static inline int printf(const char *format, ...) {
    char buffer[1024];
    char *buf = buffer;
    const char *fmt = format;
    int *args = (int*)(&format + 1);  // Point to first vararg
    int arg_index = 0;
    
    while (*fmt) {
        if (*fmt != '%') {
            *buf++ = *fmt++;
            continue;
        }
        
        fmt++; // Skip '%'
        
        // Handle format specifiers
        switch (*fmt) {
            case 's': {
                const char *s = (const char*)args[arg_index++];
                if (!s) s = "(null)";
                while (*s) *buf++ = *s++;
                break;
            }
            case 'd':
            case 'i': {
                int val = args[arg_index++];
                char tmp[16];
                itoa(val, tmp, 10);
                char *t = tmp;
                while (*t) *buf++ = *t++;
                break;
            }
            case 'u': {
                unsigned int val = (unsigned int)args[arg_index++];
                char tmp[16];
                char *t = tmp + 15;
                *t = '\0';
                do {
                    *--t = '0' + (val % 10);
                    val /= 10;
                } while (val);
                while (*t) *buf++ = *t++;
                break;
            }
            case 'x':
            case 'X': {
                unsigned int val = (unsigned int)args[arg_index++];
                char tmp[16];
                itoa(val, tmp, 16);
                char *t = tmp;
                while (*t) *buf++ = *t++;
                break;
            }
            case 'c': {
                *buf++ = (char)args[arg_index++];
                break;
            }
            case 'p': {
                unsigned int val = (unsigned int)args[arg_index++];
                *buf++ = '0';
                *buf++ = 'x';
                char tmp[16];
                itoa(val, tmp, 16);
                char *t = tmp;
                while (*t) *buf++ = *t++;
                break;
            }
            case '%': {
                *buf++ = '%';
                break;
            }
            default:
                *buf++ = '%';
                *buf++ = *fmt;
                break;
        }
        fmt++;
    }
    
    *buf = '\0';
    int len = buf - buffer;
    write(STDOUT_FILENO, buffer, len);
    return len;
}

//=============================================================================
// Input Functions
//=============================================================================

// Read a line from stdin
static inline char *gets(char *s) {
    char *p = s;
    int c;
    
    while (1) {
        if (read(STDIN_FILENO, &c, 1) <= 0) break;
        if (c == '\n' || c == '\r') break;
        *p++ = c;
    }
    *p = '\0';
    return s;
}

// Read line with size limit
static inline char *fgets(char *s, int size, int fd) {
    char *p = s;
    int c;
    int count = 0;
    
    while (count < size - 1) {
        if (read(fd, &c, 1) <= 0) break;
        *p++ = c;
        count++;
        if (c == '\n') break;
    }
    *p = '\0';
    return (count > 0) ? s : NULL;
}

#endif /* _LIBC_H */
