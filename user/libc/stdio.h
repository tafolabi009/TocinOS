/**
 * TocinOS Shared C Library Header
 * 
 * This header defines the external interface for libc.so
 * User programs should include this header.
 */

#ifndef _TOCIN_LIBC_H
#define _TOCIN_LIBC_H

// Types
typedef unsigned int size_t;
typedef int ssize_t;
typedef int pid_t;

#define NULL ((void*)0)

// Standard file descriptors
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Exit status codes
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

//=============================================================================
// System Call Wrappers
//=============================================================================

void _exit(int status) __attribute__((noreturn));
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int open(const char *path, int flags);
int close(int fd);
pid_t getpid(void);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
void *brk(void *addr);
int spawn(const char *path);
int sleep(unsigned int seconds);

//=============================================================================
// String Functions
//=============================================================================

size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

//=============================================================================
// Memory Functions
//=============================================================================

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

//=============================================================================
// Standard I/O Functions
//=============================================================================

int putchar(int c);
int puts(const char *s);
int getchar(void);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

//=============================================================================
// Conversion Functions
//=============================================================================

int atoi(const char *s);
long atol(const char *s);

//=============================================================================
// Character Functions
//=============================================================================

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int toupper(int c);
int tolower(int c);

//=============================================================================
// Memory Allocation
//=============================================================================

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);

// Memory mapping
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_PRIVATE 0x02
#define MAP_FIXED   0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED  ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, int offset);
int munmap(void *addr, size_t length);

//=============================================================================
// Convenience macros
//=============================================================================

// Inline exit for simple programs
static inline void exit(int status) {
    _exit(status);
}

#endif /* _TOCIN_LIBC_H */
