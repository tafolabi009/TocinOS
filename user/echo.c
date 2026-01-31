/**
 * TocinOS echo command
 * 
 * Prints arguments to stdout
 * Usage: echo [text...]
 */

#include "libc/libc.h"

void _start(void) {
    // For now, just print a fixed message since we don't have args yet
    const char *msg = "echo: Hello from TocinOS!\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    exit(0);
}
