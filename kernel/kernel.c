/**
 * TocinOS Kernel Main Entry Point
 * 
 * This file contains the main kernel initialization and execution logic
 */

#include "../include/kernel/kernel.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/task.h"
#include "../include/kernel/cpu_info.h"
#include "../include/kernel/gdt.h"
#include "../include/kernel/idt.h"
#include "../include/kernel/isr.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/keyboard.h"
#include "../include/kernel/serial.h"
#include "../include/kernel/syscall.h"
#include "../include/kernel/shell.h"
#include "../include/kernel/usermode.h"
#include "../include/kernel/process.h"
#include "../include/kernel/fat.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/elf.h"
#include "../include/drivers/mdf.h"
#include "../include/drivers/vesa.h"
#include "../include/drivers/ide.h"
#include "../include/drivers/net.h"

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
 * Print a single character to the screen
 */
void kernel_print_char(char c) {
    char str[2] = {c, 0};
    kernel_print(str);
    serial_write(COM1, str);
}

/**
 * Main kernel entry point
 */
void kernel_main(void) {
    // Initialize serial port FIRST for debugging
    serial_init(COM1);
    serial_printf("\n\n=== TocinOS Booting ===\n");
    serial_printf("[EARLY] Serial initialized\n");
    
    screen_clear();
    
    kernel_print("TocinOS v1.0\n");
    serial_printf("[KERNEL] VGA cleared, starting init...\n");
    kernel_print("=============\n\n");
    
    // Detect CPU features
    serial_printf("[INIT] Detecting CPU...\n");
    kernel_print("[*] Detecting CPU features...\n");
    cpu_detect();
    cpu_print_info();
    kernel_print("\n");
    
    // Initialize memory management
    serial_printf("[INIT] PMM init...\n");
    kernel_print("[*] Initializing Physical Memory Manager...\n");
    pmm_init();
    
    serial_printf("[INIT] VMM init...\n");
    kernel_print("[*] Initializing Virtual Memory Manager...\n");
    vmm_init();
    
    // Initialize task scheduler
    serial_printf("[INIT] Scheduler init...\n");
    kernel_print("[*] Initializing Task Scheduler...\n");
    scheduler_init();
    
    // Initialize MDF driver framework
    serial_printf("[INIT] MDF init...\n");
    kernel_print("[*] Initializing MDF Driver Framework...\n");
    mdf_init();
    
    // Initialize GDT with user mode segments and TSS
    serial_printf("[INIT] GDT init...\n");
    kernel_print("[*] Initializing GDT with user mode support...\n");
    gdt_init();
    serial_printf("[INIT] GDT done\n");
    
    // Initialize interrupt handling
    serial_printf("[INIT] IDT init...\n");
    kernel_print("[*] Initializing IDT...\n");
    idt_init();
    serial_printf("[INIT] IDT done\n");
    
    serial_printf("[INIT] ISR init...\n");
    kernel_print("[*] Initializing ISR handlers...\n");
    isr_init();
    serial_printf("[INIT] ISR done\n");
    
    // Initialize timer (100 Hz)
    serial_printf("[INIT] Timer init...\n");
    kernel_print("[*] Initializing Timer (100 Hz)...\n");
    timer_init(100);
    serial_printf("[INIT] Timer done\n");
    
    // Initialize keyboard
    serial_printf("[INIT] Keyboard init...\n");
    kernel_print("[*] Initializing Keyboard...\n");
    keyboard_init();
    serial_printf("[INIT] Keyboard done\n");
    
    // Initialize serial port
    kernel_print("[*] Initializing Serial Port (COM1)...\n");
    if (serial_init(COM1) == 0) {
        serial_write(COM1, "TocinOS serial port initialized\n");
    }
    
    // Initialize system calls
    kernel_print("[*] Initializing System Call Interface...\n");
    syscall_init();
    
    // Initialize user mode support
    kernel_print("[*] Initializing User Mode Support...\n");
    if (usermode_init() == 0) {
        kernel_print("    User mode support enabled\n");
    }
    
    // Initialize process management
    kernel_print("[*] Initializing Process Management...\n");
    process_init();
    
    // Initialize VESA graphics
    kernel_print("[*] Initializing VESA Graphics...\n");
    if (vesa_init() == 0) {
        kernel_print("    VESA graphics initialized\n");
    } else {
        kernel_print("    VESA not available\n");
    }
    
    // Initialize IDE disk driver
    kernel_print("[*] Initializing IDE Disk Driver...\n");
    if (ide_init() == 0) {
        kernel_print("    IDE driver initialized\n");
        
        // Test: Read MBR and check signature
        serial_printf("[IDE TEST] Reading MBR from drive 0...\n");
        uint8_t mbr[512];
        int result = ide_read_sector(0, 0, mbr);
        if (result > 0) {
            serial_printf("[IDE TEST] MBR read successful\n");
            serial_printf("[IDE TEST] MBR signature: 0x%x%x\n", mbr[511], mbr[510]);
            if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                serial_printf("[IDE TEST] Valid boot signature detected!\n");
            }
        } else {
            serial_printf("[IDE TEST] MBR read failed: %d\n", result);
        }
    } else {
        kernel_print("    No IDE drives detected\n");
    }
    
    // Initialize FAT filesystem
    kernel_print("[*] Initializing FAT Filesystem...\n");
    if (fat_init(0) == 0) {
        kernel_print("    FAT filesystem mounted\n");
        
        // Get FAT info for logging
        fat_info_t *info = fat_get_info();
        if (info) {
            serial_printf("[FAT] Type: FAT%d\n", info->type);
            serial_printf("[FAT] Bytes per sector: %u\n", info->bytes_per_sector);
            serial_printf("[FAT] Sectors per cluster: %u\n", info->sectors_per_cluster);
            serial_printf("[FAT] Total clusters: %u\n", info->cluster_count);
        }
        
        // Test: List root directory
        serial_printf("[FAT TEST] Listing root directory...\n");
        fat_dir_entry_t entries[16];
        int num_entries = fat_list_dir("/", entries, 16);
        serial_printf("[FAT TEST] Found %d entries\n", num_entries);
        for (int i = 0; i < num_entries && i < 16; i++) {
            char name[12];
            for (int j = 0; j < 11; j++) {
                name[j] = entries[i].name[j];
            }
            name[11] = 0;
            serial_printf("[FAT TEST]   %s  size=%u\n", name, entries[i].file_size);
        }
    } else {
        kernel_print("    FAT filesystem not found\n");
        serial_printf("[FAT] Failed to mount FAT filesystem\n");
    }
    
    // Initialize VFS
    kernel_print("[*] Initializing VFS...\n");
    vfs_init();
    kernel_print("    VFS initialized\n");
    serial_printf("[INIT] VFS initialized\n");
    
    // Initialize ELF loader
    kernel_print("[*] Initializing ELF Loader...\n");
    elf_init();
    kernel_print("    ELF loader initialized\n");
    serial_printf("[INIT] ELF loader initialized\n");
    
    // Test all user programs using spawn mechanism
    serial_printf("\n=== MULTI-PROGRAM TEST ===\n");
    
    // Use sys_spawn to run programs (tests spawn/exit return mechanism)
    extern int sys_spawn(uint32_t path, uint32_t argv, uint32_t envp);
    
    // Quick test of echo, hello, ls
    const char *test_programs[] = {"/ECHO.ELF", "/HELLO.ELF", "/LS.ELF", (const char*)0};
    
    for (int i = 0; test_programs[i] != (const char*)0; i++) {
        serial_printf("\n--- Running %s ---\n", test_programs[i]);
        int exit_code = sys_spawn((uint32_t)test_programs[i], 0, 0);
        serial_printf("[TEST] %s returned with exit code: %d\n", test_programs[i], exit_code);
    }
    
    serial_printf("\n=== STARTING SHELL ===\n");
    sys_spawn((uint32_t)"/SHELL.ELF", 0, 0);
    serial_printf("\n=== SHELL EXITED ===\n");
    
    // Initialize network driver
    kernel_print("[*] Initializing Network Driver...\n");
    if (net_init() == 0) {
        kernel_print("    Network driver initialized\n");
    } else {
        kernel_print("    No network card detected\n");
    }
    
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
