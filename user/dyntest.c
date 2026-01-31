/**
 * TocinOS Dynamic Linking Test Program
 * 
 * Tests dynamic linking with shared libc
 */

// Use the external libc.so header
#include "libc/stdio.h"

int main(int argc, char *argv[]) {
    printf("Hello from dynamically linked program!\n");
    printf("argc = %d\n", argc);
    
    // Test some libc functions
    char *s = "Testing";
    printf("strlen(\"%s\") = %d\n", s, (int)strlen(s));
    
    // Test mmap
    void *ptr = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr != MAP_FAILED) {
        printf("mmap succeeded at %p\n", ptr);
        // Write something
        char *p = (char *)ptr;
        p[0] = 'H';
        p[1] = 'i';
        p[2] = '\0';
        printf("Wrote to mmap: %s\n", p);
        munmap(ptr, 4096);
    } else {
        printf("mmap failed\n");
    }
    
    return 42;
}
