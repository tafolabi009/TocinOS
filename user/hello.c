/**
 * TocinOS User-Space Test Program
 * 
 * Simple "Hello World" program to test user mode execution
 */

#include "libc/libc.h"

void _start(void) {
    printf("Hello from userspace!\n");
    exit(0);
}
