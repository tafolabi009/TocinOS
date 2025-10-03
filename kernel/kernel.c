/**
 * TocinOS Kernel Main Entry Point
 * 
 * This file contains the main kernel initialization and execution logic
 */

#include "../include/kernel/kernel.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/task.h"
#include "../include/kernel/cpu_info.h"
#include "../include/kernel/idt.h"
#include "../include/kernel/isr.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/keyboard.h"
#include "../include/kernel/shell.h"
#include "../include/drivers/mdf.h"

// VGA text mode buffer
#define VGA_MEMORY 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile unsigned short *vga_buffer = (unsigned short *)VGA_MEMORY;
static int vga_x = 0;
static int vga_y = 0;

/**
 * Clear the screen
 */
void screen_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (0x0F << 8) | ' ';
    }
    vga_x = 0;
    vga_y = 0;
}

/**
 * Print a string to the screen
 */
void kernel_print(const char *str) {
    while (*str) {
        if (*str == '\n') {
            vga_x = 0;
            vga_y++;
        } else if (*str == '\b') {
            // Backspace
            if (vga_x > 0) {
                vga_x--;
                int offset = vga_y * VGA_WIDTH + vga_x;
                vga_buffer[offset] = (0x0F << 8) | ' ';
            }
        } else {
            int offset = vga_y * VGA_WIDTH + vga_x;
            vga_buffer[offset] = (0x0F << 8) | *str;
            vga_x++;
            if (vga_x >= VGA_WIDTH) {
                vga_x = 0;
                vga_y++;
            }
        }
        if (vga_y >= VGA_HEIGHT) {
            // Scroll screen
            for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
                vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
            }
            // Clear last line
            for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
                vga_buffer[i] = (0x0F << 8) | ' ';
            }
            vga_y = VGA_HEIGHT - 1;
        }
        str++;
    }
}

/**
 * Main kernel entry point
 */
void kernel_main(void) {
    screen_clear();
    
    kernel_print("TocinOS v1.0\n");
    kernel_print("=============\n\n");
    
    // Detect CPU features
    kernel_print("[*] Detecting CPU features...\n");
    cpu_detect();
    cpu_print_info();
    kernel_print("\n");
    
    // Initialize memory management
    kernel_print("[*] Initializing Physical Memory Manager...\n");
    pmm_init();
    
    kernel_print("[*] Initializing Virtual Memory Manager...\n");
    vmm_init();
    
    // Initialize task scheduler
    kernel_print("[*] Initializing Task Scheduler...\n");
    scheduler_init();
    
    // Initialize MDF driver framework
    kernel_print("[*] Initializing MDF Driver Framework...\n");
    mdf_init();
    
    // Initialize interrupt handling
    kernel_print("[*] Initializing IDT...\n");
    idt_init();
    
    kernel_print("[*] Initializing ISR handlers...\n");
    isr_init();
    
    // Initialize timer (100 Hz)
    kernel_print("[*] Initializing Timer (100 Hz)...\n");
    timer_init(100);
    
    // Initialize keyboard
    kernel_print("[*] Initializing Keyboard...\n");
    keyboard_init();
    
    kernel_print("\n[OK] Kernel initialization complete!\n");
    kernel_print("[*] System ready.\n");
    
    // Start multitasking
    kernel_print("[*] Starting scheduler...\n");
    scheduler_start();
    
    // Initialize and run shell
    shell_init();
    shell_run();
    
    // Infinite loop (should never reach here)
    while (1) {
        __asm__ volatile("hlt");
    }
}
