/**
 * TocinOS Simple Dynamic Linking Test
 * 
 * This program has a PT_INTERP section to trigger the dynamic linker.
 * It tests the basic infrastructure without requiring external libraries.
 */

// Tell linker we need the dynamic linker
__asm__(".section .interp,\"a\"\n"
        ".string \"/LD.SO\"\n"
        ".previous");

// Minimal syscall wrappers
static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
    );
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
    );
    return ret;
}

#define SYS_EXIT  0
#define SYS_WRITE 1

static void puts(const char *s) {
    int len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (int)s, len);
}

void _start(void) {
    puts("Hello from dynamically loaded program!\n");
    puts("The dynamic linker (ld.so) was invoked to load this program.\n");
    syscall1(SYS_EXIT, 0);
    while(1) __asm__ volatile("hlt");
}
