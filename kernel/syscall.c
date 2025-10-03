/**
 * System Call Implementation
 * 
 * Handles system calls from user space
 */

#include "../include/kernel/syscall.h"
#include "../include/kernel/isr.h"
#include "../include/kernel/idt.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/kernel.h"

// External assembly interrupt handler for syscalls
extern void isr128(void);

// System call table
static syscall_handler_t syscall_table[SYSCALL_MAX];

/**
 * System call handler (called from interrupt)
 */
static void syscall_handler(registers_t *regs) {
    // Get system call number from EAX
    uint32_t syscall_num = regs->eax;
    
    // Check if system call number is valid
    if (syscall_num >= SYSCALL_MAX || syscall_table[syscall_num] == 0) {
        // Invalid system call
        regs->eax = (uint32_t)-1;
        return;
    }
    
    // Call the system call handler
    syscall_handler_t handler = syscall_table[syscall_num];
    int result = handler(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
    
    // Return result in EAX
    regs->eax = result;
}

/**
 * Initialize system call interface
 */
void syscall_init(void) {
    // Clear system call table
    for (int i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = 0;
    }
    
    // Register system call interrupt handler (interrupt 0x80)
    isr_register_handler(0x80, syscall_handler);
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, IDT_ATTR_PRESENT | IDT_TYPE_INTERRUPT | IDT_ATTR_DPL3);
    
    // Register system call implementations
    syscall_register(SYS_EXIT, (syscall_handler_t)sys_exit);
    syscall_register(SYS_WRITE, (syscall_handler_t)sys_write);
    syscall_register(SYS_READ, (syscall_handler_t)sys_read);
    syscall_register(SYS_GETTIME, (syscall_handler_t)sys_gettime);
    syscall_register(SYS_SLEEP, (syscall_handler_t)sys_sleep);
}

/**
 * Register a system call handler
 */
void syscall_register(uint32_t num, syscall_handler_t handler) {
    if (num < SYSCALL_MAX) {
        syscall_table[num] = handler;
    }
}

/**
 * System call implementations
 */

int sys_exit(uint32_t code) {
    (void)code;
    kernel_print("\n[SYSCALL] Process exit called\n");
    // TODO: Implement process termination
    return 0;
}

int sys_write(uint32_t fd, uint32_t buf, uint32_t count) {
    (void)fd;
    // For now, just write to screen
    const char *str = (const char *)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (str[i] == 0) break;
        char c[2] = {str[i], 0};
        kernel_print(c);
    }
    return count;
}

int sys_read(uint32_t fd, uint32_t buf, uint32_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    // TODO: Implement read system call
    return 0;
}

int sys_gettime(void) {
    return timer_get_ticks();
}

int sys_sleep(uint32_t ticks) {
    timer_wait(ticks);
    return 0;
}
