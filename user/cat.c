/**
 * TocinOS cat command
 * 
 * Displays file contents
 * Usage: cat [filename]
 */

#include "libc/libc.h"

// For now, we'll read a hardcoded file since args aren't passed yet
void _start(void) {
    const char *filename = "/TEST.TXT";
    char buffer[512];
    
    printf("cat: Opening %s\n", filename);
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open file\n");
        exit(1);
    }
    
    // Read and display file contents
    int bytes;
    while ((bytes = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        write(STDOUT_FILENO, buffer, bytes);
    }
    
    close(fd);
    exit(0);
}
