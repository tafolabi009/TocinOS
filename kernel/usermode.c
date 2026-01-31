/**
 * TocinOS User Mode Implementation
 * 
 * Implements Ring 3 execution with TSS and privilege level switching
 */

#include "../include/kernel/usermode.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/gdt.h"

// Global TSS
static tss_entry_t tss = {0};
static int usermode_initialized = 0;

// Process table
#define MAX_PROCESSES 64
static user_process_t process_table[MAX_PROCESSES] = {0};
static uint32_t next_pid = 1;
static user_process_t *current_process = 0;

/**
 * Write TSS descriptor to GDT
 */
static void write_tss_descriptor(void) {
    // Update the TSS entry in the GDT with the actual TSS address
    gdt_update_tss((uint32_t)&tss, sizeof(tss_entry_t) - 1);
}

/**
 * Initialize user mode support
 */
int usermode_init(void) {
    if (usermode_initialized) {
        return 0;
    }
    
    // Clear TSS
    for (unsigned int i = 0; i < sizeof(tss_entry_t); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }
    
    // Setup TSS
    tss.ss0 = GDT_KERNEL_DATA;  // Kernel data segment
    tss.esp0 = 0;               // Will be set per process
    tss.cs = GDT_USER_CODE | 3; // User code segment with DPL=3
    tss.ss = GDT_USER_DATA | 3; // User data segment with DPL=3
    tss.ds = GDT_USER_DATA | 3;
    tss.es = GDT_USER_DATA | 3;
    tss.fs = GDT_USER_DATA | 3;
    tss.gs = GDT_USER_DATA | 3;
    tss.iomap_base = sizeof(tss_entry_t);
    
    // Write TSS to GDT
    write_tss_descriptor();
    
    // Load TSS
    __asm__ volatile("ltr %%ax" : : "a"(GDT_TSS));
    
    usermode_initialized = 1;
    return 0;
}

/**
 * Setup TSS with kernel stack
 */
void usermode_setup_tss(uint32_t kernel_stack) {
    tss.esp0 = kernel_stack;
}

/**
 * Set kernel stack for TSS
 */
void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

/**
 * Get TSS
 */
tss_entry_t *tss_get(void) {
    return &tss;
}

/**
 * Allocate a new process
 */
static user_process_t *allocate_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_TERMINATED || 
            process_table[i].pid == 0) {
            process_table[i].pid = next_pid++;
            return &process_table[i];
        }
    }
    return 0; // No free slots
}

/**
 * Create a user mode process
 */
int usermode_create_process(void (*entry_point)(void), uint32_t *pid) {
    if (!usermode_initialized || !entry_point) {
        return -1;
    }
    
    // Allocate process structure
    user_process_t *process = allocate_process();
    if (!process) {
        return -1;
    }
    
    // Allocate kernel stack (8KB)
    process->kernel_stack = (uint32_t)pmm_alloc_page() + 0x2000;
    if (!process->kernel_stack) {
        return -1;
    }
    
    // Allocate user stack (8KB)
    uint32_t user_stack = (uint32_t)pmm_alloc_page() + 0x2000;
    if (!user_stack) {
        pmm_free_page((void *)(process->kernel_stack - 0x2000));
        return -1;
    }
    
    // Setup process
    process->esp = user_stack;
    process->ebp = user_stack;
    process->eip = (uint32_t)entry_point;
    process->state = PROCESS_STATE_READY;
    process->priority = 5; // Default priority
    
    // Create page directory for process (simplified)
    process->page_directory = 0; // Use kernel page directory for now
    
    if (pid) {
        *pid = process->pid;
    }
    
    return 0;
}

/**
 * Switch to user mode with specific user stack
 */
void usermode_switch_to_user_with_stack(void (*entry_point)(void), uint32_t user_esp);

void usermode_switch_to_user(void (*entry_point)(void)) {
    if (!usermode_initialized) {
        return;
    }
    
    // Use a default user stack at top of user stack area
    // Stack grows down, so point to top of stack area
    // USER_STACK_TOP = 0xC0000000, -0x1000 gap = 0xBFFFF000
    // This is the top of the allocated user stack region
    uint32_t user_esp = 0xBFFFEFF0;  // Leave some space at top
    
    usermode_switch_to_user_with_stack(entry_point, user_esp);
}

/**
 * Switch to user mode with specific stack
 */
void usermode_switch_to_user_with_stack(void (*entry_point)(void), uint32_t user_esp) {
    if (!usermode_initialized) {
        return;
    }
    
    // Setup user stack and entry point
    // This uses inline assembly to:
    // 1. Setup stack frame for user mode
    // 2. Push SS, ESP, EFLAGS, CS, EIP for iret
    // 3. Switch to ring 3
    
    __asm__ volatile(
        "cli\n"                          // Disable interrupts
        "mov $0x23, %%ax\n"              // User data segment (0x20 | 3)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        
        "pushl $0x23\n"                  // User SS
        "pushl %1\n"                     // User ESP
        "pushf\n"                        // EFLAGS
        "popl %%eax\n"
        "orl $0x200, %%eax\n"            // Enable interrupts in EFLAGS
        "pushl %%eax\n"
        "pushl $0x1B\n"                  // User CS (0x18 | 3)
        "pushl %0\n"                     // User EIP
        "iret\n"                         // Switch to user mode
        :
        : "r"(entry_point), "r"(user_esp)
        : "eax"
    );
}

/**
 * Return to kernel mode
 */
void usermode_return_to_kernel(void) {
    // This would typically be called via a system call
    // The interrupt handler would handle the privilege level switch
}

/**
 * Get current process
 */
user_process_t *usermode_get_current_process(void) {
    return current_process;
}

/**
 * Switch to a different process
 */
int usermode_switch_process(uint32_t pid) {
    // Find process
    user_process_t *process = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && 
            process_table[i].state != PROCESS_STATE_TERMINATED) {
            process = &process_table[i];
            break;
        }
    }
    
    if (!process) {
        return -1;
    }
    
    // Save current process state if exists
    if (current_process) {
        current_process->state = PROCESS_STATE_READY;
    }
    
    // Switch to new process
    current_process = process;
    current_process->state = PROCESS_STATE_RUNNING;
    
    // Update TSS kernel stack
    tss_set_kernel_stack(process->kernel_stack);
    
    // Switch page directory if needed
    if (process->page_directory) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(process->page_directory));
    }
    
    return 0;
}

/**
 * Terminate a process
 */
int usermode_terminate_process(uint32_t pid) {
    // Find process
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid) {
            // Free resources
            if (process_table[i].kernel_stack) {
                pmm_free_page((void *)(process_table[i].kernel_stack - 0x2000));
            }
            
            // Mark as terminated
            process_table[i].state = PROCESS_STATE_TERMINATED;
            process_table[i].pid = 0;
            
            if (current_process == &process_table[i]) {
                current_process = 0;
            }
            
            return 0;
        }
    }
    
    return -1;
}
