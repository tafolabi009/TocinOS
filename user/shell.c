/**
 * TocinOS Interactive Shell
 * 
 * A simple command-line shell for TocinOS
 */

#include "libc/libc.h"

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

// Built-in commands
static int builtin_help(void);
static int builtin_exit(void);
static int builtin_clear(void);

// Command structure
typedef struct {
    const char *name;
    int (*func)(void);
    const char *help;
} command_t;

static command_t builtins[] = {
    {"help", builtin_help, "Show this help message"},
    {"exit", builtin_exit, "Exit the shell"},
    {"clear", builtin_clear, "Clear the screen"},
    {0, 0, 0}  // Terminator
};

// Show help
static int builtin_help(void) {
    printf("\nTocinOS Shell - Available commands:\n");
    printf("-----------------------------------\n");
    
    for (int i = 0; builtins[i].name; i++) {
        printf("  %s - %s\n", builtins[i].name, builtins[i].help);
    }
    
    printf("\nExternal programs:\n");
    printf("  ls    - List directory contents\n");
    printf("  cat   - Display file contents\n");
    printf("  echo  - Print text\n");
    printf("  hello - Say hello\n");
    printf("\n");
    return 0;
}

// Exit shell
static int builtin_exit(void) {
    printf("Goodbye!\n");
    exit(0);
    return 0;  // Never reached
}

// Clear screen (just print newlines for now)
static int builtin_clear(void) {
    for (int i = 0; i < 25; i++) {
        printf("\n");
    }
    return 0;
}

// Try to execute a built-in command
static int try_builtin(const char *cmd) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return builtins[i].func();
        }
    }
    return -1;  // Not a builtin
}

// Read a line from stdin with editing
static int readline(char *buf, int maxlen) {
    int pos = 0;
    
    while (pos < maxlen - 1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) {
            break;
        }
        
        if (c == '\n' || c == '\r') {
            buf[pos] = '\0';
            return pos;
        }
        
        if (c == '\b' || c == 127) {  // Backspace
            if (pos > 0) {
                pos--;
                // Erase character on screen
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        
        if (c >= 32 && c < 127) {  // Printable character
            buf[pos++] = c;
            write(STDOUT_FILENO, &c, 1);  // Echo
        }
    }
    
    buf[pos] = '\0';
    return pos;
}

void _start(void) {
    char cmd[MAX_CMD_LEN];
    
    printf("\n");
    printf("================================\n");
    printf("   Welcome to TocinOS Shell!\n");
    printf("================================\n");
    printf("Type 'help' for available commands.\n\n");
    
    while (1) {
        // Print prompt
        printf("tocinos> ");
        
        // Read command
        int len = readline(cmd, sizeof(cmd));
        printf("\n");
        
        // Skip empty lines
        if (len == 0) continue;
        
        // Try built-in command first
        if (try_builtin(cmd) >= 0) {
            continue;
        }
        
        // Try to run as external program
        // Build program path
        char path[32];
        path[0] = '/';
        int i;
        for (i = 0; cmd[i] && i < 20; i++) {
            path[i + 1] = cmd[i];
            if (cmd[i] >= 'a' && cmd[i] <= 'z') {
                path[i + 1] = cmd[i] - 32;  // Uppercase
            }
        }
        // Add .ELF extension
        path[i + 1] = '.';
        path[i + 2] = 'E';
        path[i + 3] = 'L';
        path[i + 4] = 'F';
        path[i + 5] = '\0';
        
        // Try to spawn the program
        int result = spawn(path);
        if (result < 0) {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for available commands.\n");
        }
    }
}
