/**
 * Shell Implementation for TocinOS
 * 
 * Simple command-line interface
 */

#include "../include/kernel/shell.h"
#include "../include/kernel/keyboard.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/cpu_info.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/kernel.h"

#define MAX_COMMAND_LENGTH 256
#define MAX_HISTORY 10

// Command buffer
static char command_buffer[MAX_COMMAND_LENGTH];
static int command_pos = 0;

// Command history
static char command_history[MAX_HISTORY][MAX_COMMAND_LENGTH];
static int history_count = 0;

/**
 * Simple string length
 */
static int strlen(const char *str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

/**
 * Simple string compare
 */
static int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

/**
 * Simple string copy
 */
static void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

/**
 * Display shell prompt
 */
static void show_prompt(void) {
    kernel_print("TocinOS> ");
}

/**
 * Process shell command
 */
static void process_command(void) {
    // Null-terminate command
    command_buffer[command_pos] = '\0';
    
    // Skip empty commands
    if (command_pos == 0) {
        show_prompt();
        return;
    }
    
    // Add to history
    if (history_count < MAX_HISTORY) {
        strcpy(command_history[history_count], command_buffer);
        history_count++;
    }
    
    // Process commands
    if (strcmp(command_buffer, "help") == 0) {
        kernel_print("\nAvailable commands:\n");
        kernel_print("  help     - Show this help message\n");
        kernel_print("  clear    - Clear the screen\n");
        kernel_print("  cpuinfo  - Display CPU information\n");
        kernel_print("  meminfo  - Display memory information\n");
        kernel_print("  uptime   - Show system uptime\n");
        kernel_print("  echo     - Echo back text\n");
        kernel_print("  history  - Show command history\n");
        kernel_print("\n");
    }
    else if (strcmp(command_buffer, "clear") == 0) {
        screen_clear();
    }
    else if (strcmp(command_buffer, "cpuinfo") == 0) {
        kernel_print("\n");
        cpu_print_info();
        kernel_print("\n");
    }
    else if (strcmp(command_buffer, "meminfo") == 0) {
        kernel_print("\nMemory Information:\n");
        kernel_print("  Physical Memory Manager: Active\n");
        kernel_print("  Virtual Memory Manager: Active\n");
        kernel_print("  Page Size: 4KB\n");
        kernel_print("\n");
    }
    else if (strcmp(command_buffer, "uptime") == 0) {
        uint32_t ticks = timer_get_ticks();
        uint32_t seconds = ticks / 100;  // 100 Hz timer
        uint32_t minutes = seconds / 60;
        uint32_t hours = minutes / 60;
        
        kernel_print("\nSystem Uptime: ");
        // Simple number printing (hours)
        if (hours > 0) {
            char h[10];
            int i = 0;
            uint32_t temp = hours;
            if (temp == 0) {
                h[i++] = '0';
            } else {
                char rev[10];
                int j = 0;
                while (temp > 0) {
                    rev[j++] = '0' + (temp % 10);
                    temp /= 10;
                }
                while (j > 0) {
                    h[i++] = rev[--j];
                }
            }
            h[i] = '\0';
            kernel_print(h);
            kernel_print("h ");
        }
        
        // Minutes
        minutes = minutes % 60;
        char m[10];
        int i = 0;
        uint32_t temp = minutes;
        if (temp == 0) {
            m[i++] = '0';
        } else {
            char rev[10];
            int j = 0;
            while (temp > 0) {
                rev[j++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (j > 0) {
                m[i++] = rev[--j];
            }
        }
        m[i] = '\0';
        kernel_print(m);
        kernel_print("m ");
        
        // Seconds
        seconds = seconds % 60;
        char s[10];
        i = 0;
        temp = seconds;
        if (temp == 0) {
            s[i++] = '0';
        } else {
            char rev[10];
            int j = 0;
            while (temp > 0) {
                rev[j++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (j > 0) {
                s[i++] = rev[--j];
            }
        }
        s[i] = '\0';
        kernel_print(s);
        kernel_print("s\n\n");
    }
    else if (strcmp(command_buffer, "history") == 0) {
        kernel_print("\nCommand History:\n");
        for (int i = 0; i < history_count; i++) {
            kernel_print("  ");
            char num[5];
            int n = i + 1;
            int j = 0;
            char rev[5];
            int k = 0;
            while (n > 0) {
                rev[k++] = '0' + (n % 10);
                n /= 10;
            }
            while (k > 0) {
                num[j++] = rev[--k];
            }
            num[j] = '\0';
            kernel_print(num);
            kernel_print(". ");
            kernel_print(command_history[i]);
            kernel_print("\n");
        }
        kernel_print("\n");
    }
    else {
        kernel_print("\nUnknown command: ");
        kernel_print(command_buffer);
        kernel_print("\nType 'help' for available commands.\n\n");
    }
    
    // Reset command buffer
    command_pos = 0;
    show_prompt();
}

/**
 * Initialize shell
 */
void shell_init(void) {
    command_pos = 0;
    history_count = 0;
    
    kernel_print("\n");
    kernel_print("=============================================\n");
    kernel_print("    Welcome to TocinOS Shell v1.0\n");
    kernel_print("=============================================\n");
    kernel_print("\nType 'help' for available commands.\n\n");
    show_prompt();
}

/**
 * Run shell main loop
 */
void shell_run(void) {
    while (1) {
        // Get character from keyboard
        char c = keyboard_getchar();
        
        // Handle special characters
        if (c == '\n') {
            kernel_print("\n");
            process_command();
        }
        else if (c == '\b') {
            // Backspace
            if (command_pos > 0) {
                command_pos--;
                // Simple backspace (just print backspace, space, backspace)
                kernel_print("\b \b");
            }
        }
        else if (c >= 32 && c < 127) {
            // Printable character
            if (command_pos < MAX_COMMAND_LENGTH - 1) {
                command_buffer[command_pos++] = c;
                // Echo character
                char str[2] = {c, '\0'};
                kernel_print(str);
            }
        }
    }
}
