/**
 * TocinOS Shared C Library Implementation
 * 
 * This is the implementation of the shared libc for dynamic linking.
 * Functions are compiled into a shared object (libc.so).
 */

#include "syscall.h"

// Types
typedef unsigned int size_t;
typedef int ssize_t;
typedef int pid_t;

#define NULL ((void*)0)

//=============================================================================
// System Call Wrappers
//=============================================================================

void _exit(int status) {
    syscall1(SYS_EXIT, status);
    while(1) __asm__ volatile("hlt");
}

ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

ssize_t read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

int open(const char *path, int flags) {
    return syscall2(SYS_OPEN, (int)path, flags);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

pid_t getpid(void) {
    return syscall0(SYS_GETPID);
}

pid_t fork(void) {
    return syscall0(SYS_FORK);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall3(SYS_EXEC, (int)path, (int)argv, (int)envp);
}

pid_t wait(int *status) {
    return syscall3(SYS_WAIT, -1, (int)status, 0);
}

pid_t waitpid(pid_t pid, int *status, int options) {
    return syscall3(SYS_WAITPID, pid, (int)status, options);
}

void *brk(void *addr) {
    return (void *)syscall1(SYS_BRK, (int)addr);
}

int spawn(const char *path) {
    return syscall3(SYS_SPAWN, (int)path, 0, 0);
}

int sleep(unsigned int seconds) {
    return syscall1(SYS_SLEEP, seconds * 100);
}

// mmap flags
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_PRIVATE 0x02
#define MAP_FIXED   0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED  ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, int offset) {
    (void)offset;  // Offset passed via fd for simplicity
    int ret = syscall5(SYS_MMAP, (int)addr, (int)length, prot, flags, fd);
    if (ret < 0) return MAP_FAILED;
    return (void *)ret;
}

int munmap(void *addr, size_t length) {
    return syscall2(SYS_MUNMAP, (int)addr, (int)length);
}

//=============================================================================
// String Functions
//=============================================================================

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- && (*d++ = *src++));
    *d = '\0';
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return (char *)haystack;
    
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

//=============================================================================
// Memory Functions
//=============================================================================

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

//=============================================================================
// Standard I/O Functions
//=============================================================================

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int puts(const char *s) {
    write(1, s, strlen(s));
    write(1, "\n", 1);
    return 1;
}

int getchar(void) {
    char c;
    if (read(0, &c, 1) != 1) return -1;
    return (unsigned char)c;
}

static void print_num(unsigned int n, int base, int pad, char pad_char) {
    char buf[32];
    int i = 0;
    
    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            int digit = n % base;
            buf[i++] = (digit < 10) ? '0' + digit : 'a' + digit - 10;
            n /= base;
        }
    }
    
    while (i < pad) {
        buf[i++] = pad_char;
    }
    
    while (i > 0) {
        putchar(buf[--i]);
    }
}

int printf(const char *fmt, ...) {
    int *args = (int *)&fmt + 1;
    int count = 0;
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            
            // Parse width
            int width = 0;
            char pad_char = ' ';
            if (*fmt == '0') {
                pad_char = '0';
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int val = *args++;
                    if (val < 0) {
                        putchar('-');
                        count++;
                        val = -val;
                    }
                    print_num((unsigned)val, 10, width, pad_char);
                    count++;
                    break;
                }
                case 'u':
                    print_num((unsigned)*args++, 10, width, pad_char);
                    count++;
                    break;
                case 'x':
                    print_num((unsigned)*args++, 16, width, pad_char);
                    count++;
                    break;
                case 'p':
                    puts("0x");
                    print_num((unsigned)*args++, 16, 8, '0');
                    count += 10;
                    break;
                case 's': {
                    const char *s = (const char *)*args++;
                    if (s) {
                        write(1, s, strlen(s));
                    } else {
                        puts("(null)");
                    }
                    count++;
                    break;
                }
                case 'c':
                    putchar(*args++);
                    count++;
                    break;
                case '%':
                    putchar('%');
                    count++;
                    break;
                default:
                    putchar('%');
                    putchar(*fmt);
                    count += 2;
            }
        } else {
            putchar(*fmt);
            count++;
        }
        fmt++;
    }
    
    return count;
}

//=============================================================================
// Conversion Functions
//=============================================================================

int atoi(const char *s) {
    int result = 0;
    int sign = 1;
    
    while (*s == ' ' || *s == '\t') s++;
    
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    
    return sign * result;
}

long atol(const char *s) {
    return (long)atoi(s);
}

//=============================================================================
// Character Functions
//=============================================================================

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int toupper(int c) {
    if (islower(c)) return c - 'a' + 'A';
    return c;
}

int tolower(int c) {
    if (isupper(c)) return c - 'A' + 'a';
    return c;
}

//=============================================================================
// Simple Memory Allocator (Bump Allocator)
//=============================================================================

static void *heap_end = NULL;
static void *heap_start = NULL;

void *malloc(size_t size) {
    if (!heap_start) {
        heap_start = brk(0);
        heap_end = heap_start;
    }
    
    // Align to 8 bytes
    size = (size + 7) & ~7;
    
    void *result = heap_end;
    heap_end = (char *)heap_end + size;
    
    if (brk(heap_end) != heap_end) {
        return NULL;
    }
    
    return result;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void free(void *ptr) {
    // Simple bump allocator - no-op for free
    (void)ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    // Simple implementation - always allocate new
    void *new_ptr = malloc(size);
    if (new_ptr) {
        // Copy old data (unknown size, be conservative)
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
}

//=============================================================================
// Program Entry - C Runtime
//=============================================================================

extern int main(int argc, char *argv[]);

void _start(void) {
    // Get argc/argv from stack
    int *sp;
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    
    int argc = *sp++;
    char **argv = (char **)sp;
    
    // Call main
    int ret = main(argc, argv);
    
    // Exit with return code
    _exit(ret);
}
