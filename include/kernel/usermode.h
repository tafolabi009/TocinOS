/**
 * TocinOS User Mode Support
 * 
 * Provides Ring 3 (user mode) execution support with TSS and privilege switching
 */

#ifndef USERMODE_H
#define USERMODE_H

#include "../stdint.h"

// TSS (Task State Segment) structure for x86
typedef struct {
    uint32_t prev_tss;      // Previous TSS (for task switching)
    uint32_t esp0;          // ESP for ring 0
    uint32_t ss0;           // SS for ring 0
    uint32_t esp1;          // ESP for ring 1
    uint32_t ss1;           // SS for ring 1
    uint32_t esp2;          // ESP for ring 2
    uint32_t ss2;           // SS for ring 2
    uint32_t cr3;           // Page directory base
    uint32_t eip;           // Instruction pointer
    uint32_t eflags;        // Flags
    uint32_t eax;           // General purpose registers
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;           // Stack pointer
    uint32_t ebp;           // Base pointer
    uint32_t esi;
    uint32_t edi;
    uint32_t es;            // Segment selectors
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;           // LDT selector
    uint16_t trap;          // Trap on task switch
    uint16_t iomap_base;    // I/O map base address
} __attribute__((packed)) tss_entry_t;

// User process structure
typedef struct {
    uint32_t pid;           // Process ID
    uint32_t esp;           // User stack pointer
    uint32_t ebp;           // User base pointer
    uint32_t eip;           // User instruction pointer
    uint32_t page_directory;// Page directory physical address
    uint32_t kernel_stack;  // Kernel stack for this process
    uint32_t state;         // Process state
    uint32_t priority;      // Process priority
} user_process_t;

// Process states
#define PROCESS_STATE_READY      0
#define PROCESS_STATE_RUNNING    1
#define PROCESS_STATE_BLOCKED    2
#define PROCESS_STATE_TERMINATED 3

// GDT entry indices for user mode
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20
#define GDT_TSS          0x28

// User mode API
int usermode_init(void);
void usermode_setup_tss(uint32_t kernel_stack);
int usermode_create_process(void (*entry_point)(void), uint32_t *pid);
void usermode_switch_to_user(void (*entry_point)(void));
void usermode_return_to_kernel(void);
int usermode_terminate_process(uint32_t pid);

// TSS management
void tss_set_kernel_stack(uint32_t stack);
tss_entry_t *tss_get(void);

// Process management
user_process_t *usermode_get_current_process(void);
int usermode_switch_process(uint32_t pid);

#endif // USERMODE_H
