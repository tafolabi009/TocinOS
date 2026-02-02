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
#include "../include/kernel/vfs.h"
#include "../include/kernel/usermode.h"
#include "../include/kernel/task.h"
#include "../include/kernel/process.h"
#include "../include/kernel/memory.h"

// External assembly interrupt handler for syscalls
extern void isr128(void);

/* Forward declarations for legacy memory syscall implementations */
static int syscall_brk_legacy(uint32_t addr);
static int syscall_mmap_legacy(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, uint32_t fd);
static int syscall_munmap_legacy(uint32_t addr, uint32_t length);

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
    
    // Register core system calls
    syscall_register(SYS_EXIT, (syscall_handler_t)sys_exit);
    syscall_register(SYS_WRITE, (syscall_handler_t)sys_write);
    syscall_register(SYS_READ, (syscall_handler_t)sys_read);
    syscall_register(SYS_OPEN, (syscall_handler_t)sys_open);
    syscall_register(SYS_CLOSE, (syscall_handler_t)sys_close);
    syscall_register(SYS_GETPID, (syscall_handler_t)sys_getpid);
    syscall_register(SYS_FORK, (syscall_handler_t)sys_fork);
    syscall_register(SYS_EXEC, (syscall_handler_t)sys_exec);
    syscall_register(SYS_WAIT, (syscall_handler_t)sys_wait);
    syscall_register(SYS_SLEEP, (syscall_handler_t)sys_sleep);
    syscall_register(SYS_GETTIME, (syscall_handler_t)sys_gettime);
    syscall_register(SYS_BRK, (syscall_handler_t)syscall_brk_legacy);
    syscall_register(SYS_LSEEK, (syscall_handler_t)sys_lseek);
    syscall_register(SYS_GETCWD, (syscall_handler_t)sys_getcwd);
    syscall_register(SYS_CHDIR, (syscall_handler_t)sys_chdir);
    syscall_register(SYS_OPENDIR, (syscall_handler_t)sys_opendir);
    syscall_register(SYS_READDIR, (syscall_handler_t)sys_readdir);
    syscall_register(SYS_CLOSEDIR, (syscall_handler_t)sys_closedir);
    syscall_register(SYS_WAITPID, (syscall_handler_t)sys_waitpid);
    syscall_register(SYS_SPAWN, (syscall_handler_t)sys_spawn);
    syscall_register(SYS_MMAP, (syscall_handler_t)syscall_mmap_legacy);
    syscall_register(SYS_MUNMAP, (syscall_handler_t)syscall_munmap_legacy);
    
    // Threading syscalls (Phase 3.2)
    syscall_register(SYS_CLONE, (syscall_handler_t)sys_clone);
    syscall_register(SYS_GETTID, (syscall_handler_t)sys_gettid);
    syscall_register(SYS_FUTEX, (syscall_handler_t)sys_futex);
    syscall_register(SYS_SET_TLS, (syscall_handler_t)sys_set_tls);
    syscall_register(SYS_GET_TLS, (syscall_handler_t)sys_get_tls);
    syscall_register(SYS_TKILL, (syscall_handler_t)sys_tkill);
    syscall_register(SYS_EXIT_GROUP, (syscall_handler_t)sys_exit_group);
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

// Spawn return context - used to return from spawned programs
static struct {
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    int active;
    int exit_code;
} spawn_context = {0, 0, 0, 0, 0};

/**
 * Exit the current process
 */
int sys_exit(uint32_t code) {
    extern void serial_printf(const char *fmt, ...);
    serial_printf("\n[EXIT] Program exited with code %u\n", code);
    
    // If we were spawned, return to the spawner
    if (spawn_context.active) {
        spawn_context.exit_code = code;
        spawn_context.active = 0;
        
        // Return to spawn point using saved context
        __asm__ volatile(
            "mov %0, %%esp\n"
            "mov %1, %%ebp\n"
            "jmp *%2\n"
            :
            : "r"(spawn_context.esp), "r"(spawn_context.ebp), "r"(spawn_context.eip)
        );
    }
    
    // No spawn context - halt the system
    serial_printf("[EXIT] System halting.\n");
    __asm__ volatile("cli; hlt");
    
    return 0;
}

/**
 * Write to a file descriptor
 */
int sys_write(uint32_t fd, uint32_t buf, uint32_t count) {
    // Use VFS to write
    return vfs_write(fd, (const void *)buf, count);
}

/**
 * Read from a file descriptor
 */
int sys_read(uint32_t fd, uint32_t buf, uint32_t count) {
    // Use VFS to read
    return vfs_read(fd, (void *)buf, count);
}

/**
 * Open a file
 */
int sys_open(uint32_t path, uint32_t flags, uint32_t mode) {
    (void)mode; // Mode is not yet implemented
    return vfs_open((const char *)path, flags);
}

/**
 * Close a file descriptor
 */
int sys_close(uint32_t fd) {
    return vfs_close(fd);
}

/**
 * Get current process ID
 */
int sys_getpid(void) {
    return process_getpid();
}

/**
 * Fork the current process
 * Returns: 0 to child, child PID to parent, -1 on error
 */
int sys_fork(void) {
    return process_fork();
}

/**
 * Execute a new program
 */
int sys_exec(uint32_t path, uint32_t argv, uint32_t envp) {
    return process_exec((const char *)path, (const char **)argv, (const char **)envp);
}

/**
 * Wait for a child process
 */
int sys_wait(uint32_t pid, uint32_t status, uint32_t options) {
    return process_wait((int32_t)pid, (int *)status, options);
}

/**
 * Get current time in ticks
 */
int sys_gettime(void) {
    return timer_get_ticks();
}

/**
 * Sleep for specified number of ticks
 */
int sys_sleep(uint32_t ticks) {
    timer_wait(ticks);
    return 0;
}

/**
 * Set the program break (heap end)
 */
static int syscall_brk_legacy(uint32_t addr) {
    return process_brk(addr);
}

/**
 * Increment program break
 */
int sys_sbrk(uint32_t increment) {
    return (int)(uint32_t)process_sbrk((int32_t)increment);
}

/**
 * Seek in a file
 */
int sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence) {
    return vfs_seek(fd, offset, whence);
}

/**
 * Get current working directory
 */
int sys_getcwd(uint32_t buf, uint32_t size) {
    return process_getcwd((char *)buf, size);
}

/**
 * Change current directory
 */
int sys_chdir(uint32_t path) {
    return process_chdir((const char *)path);
}

/**
 * Open directory for listing
 * Returns number of entries on success, -1 on error
 */
int sys_opendir(uint32_t path, uint32_t reserved) {
    (void)reserved;
    extern int fat_list_dir(const char *path, void *entries, int max_entries);
    
    // Return the count of directory entries
    // The actual entries will be read by sys_readdir
    static char temp_entries[32 * 64];  // Space for 64 entries
    int count = fat_list_dir((const char *)path, temp_entries, 64);
    return count;
}

/**
 * Read directory entries
 * Copies directory entries to user buffer
 */
int sys_readdir(uint32_t path, uint32_t entries, uint32_t max_entries) {
    extern int fat_list_dir(const char *path, void *entries, int max_entries);
    
    int count = fat_list_dir((const char *)path, (void *)entries, max_entries);
    return count;
}

/**
 * Close directory
 */
int sys_closedir(uint32_t dir) {
    (void)dir;
    return 0;  // Nothing to do for simple implementation
}

/**
 * Wait for specific child process
 */
int sys_waitpid(uint32_t pid, uint32_t status, uint32_t options) {
    return process_waitpid((int)pid, (int *)status, (int)options);
}

/**
 * Spawn a new program (run and wait for completion)
 * This is a simplified exec that doesn't replace the current process
 * Returns: exit code of spawned program, or -1 on error
 */
int sys_spawn(uint32_t path, uint32_t argv, uint32_t envp) {
    extern void serial_printf(const char *fmt, ...);
    (void)argv;  // TODO: Pass arguments
    (void)envp;  // TODO: Pass environment
    
    const char *program_path = (const char *)path;
    
    serial_printf("[SPAWN] Loading program: %s\n", program_path);
    
    // Load and execute the ELF file
    extern int elf_load_file(const char *path, void *context);
    extern int elf_execute(void *context);
    
    // Use a simple context structure
    typedef struct {
        void *elf_data;
        uint32_t elf_size;
        uint32_t entry_point;
        uint32_t load_base;
        int is_64bit;
    } elf_context_t;
    
    elf_context_t elf_ctx;
    
    if (elf_load_file(program_path, &elf_ctx) != 0) {
        serial_printf("[SPAWN] Failed to load ELF\n");
        return -1;
    }
    
    serial_printf("[SPAWN] Executing entry=0x%x\n", elf_ctx.entry_point);
    
    // Save return point so spawned program can return here
    // We use a label to get the return address
    volatile int spawn_done = 0;
    spawn_context.active = 1;
    spawn_context.exit_code = 0;
    
    // Save stack and return address
    uint32_t saved_esp, saved_ebp;
    __asm__ volatile(
        "mov %%esp, %0\n"
        "mov %%ebp, %1\n"
        : "=r"(saved_esp), "=r"(saved_ebp)
    );
    spawn_context.esp = saved_esp;
    spawn_context.ebp = saved_ebp;
    
    // Use inline asm to set up return point
    __asm__ volatile(
        "call 1f\n"
        "1: pop %%eax\n"
        "add $12, %%eax\n"  // Skip to after elf_execute call
        "mov %%eax, %0\n"
        : "=m"(spawn_context.eip)
        :
        : "eax"
    );
    
    if (!spawn_done) {
        spawn_done = 1;
        // Execute the program (this will switch to user mode)
        elf_execute(&elf_ctx);
    }
    
    // Return here after spawned program exits
    serial_printf("[SPAWN] Program returned, exit code: %d\n", spawn_context.exit_code);
    return spawn_context.exit_code;
}

/**
 * Memory map - map virtual memory
 * 
 * Simplified mmap for TocinOS:
 * - addr: hint for virtual address (0 = let kernel choose)
 * - length: number of bytes to map
 * - prot: protection flags (ignored for now, always RW)
 * - flags: mapping flags (MAP_PRIVATE, MAP_ANONYMOUS, MAP_FIXED)
 * - fd: file descriptor (ignored for anonymous maps)
 * - offset: offset in file (ignored for anonymous maps)
 * 
 * Returns mapped virtual address, or -1 on error
 */
static int syscall_mmap_legacy(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, uint32_t fd) {
    extern void serial_printf(const char *fmt, ...);
    extern unsigned int pmm_alloc_page(void);
    extern void vmm_map_page(unsigned int vaddr, unsigned int paddr, unsigned int flags);
    
    // Ignore prot and fd for now (anonymous mapping only)
    (void)prot;
    (void)fd;
    
    serial_printf("[MMAP] addr=0x%x len=%u flags=0x%x\n", addr, length, flags);
    
    if (length == 0) {
        return -1;
    }
    
    // Round up length to page boundary
    uint32_t num_pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Choose virtual address if not specified or MAP_FIXED
    #define MAP_FIXED 0x10
    #define MAP_ANONYMOUS 0x20
    
    uint32_t vaddr;
    if (addr == 0 || !(flags & MAP_FIXED)) {
        // Use a high address region for mmap
        // Start at 0x30000000 and grow upward
        static uint32_t mmap_next = 0x30000000;
        vaddr = mmap_next;
        mmap_next += num_pages * PAGE_SIZE;
    } else {
        vaddr = addr & ~(PAGE_SIZE - 1);  // Align to page boundary
    }
    
    serial_printf("[MMAP] Mapping %u pages at 0x%x\n", num_pages, vaddr);
    
    // Allocate and map pages
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t paddr = pmm_alloc_page();
        if (!paddr) {
            serial_printf("[MMAP] Out of memory!\n");
            return -1;
        }
        
        // Zero the page
        uint8_t *p = (uint8_t *)paddr;
        for (int j = 0; j < PAGE_SIZE; j++) {
            p[j] = 0;
        }
        
        // Map with user access
        vmm_map_page(vaddr + i * PAGE_SIZE, paddr, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        
        // Invalidate TLB
        __asm__ volatile("invlpg (%0)" : : "r"(vaddr + i * PAGE_SIZE) : "memory");
    }
    
    return (int)vaddr;
}

/**
 * Memory unmap - unmap virtual memory
 * 
 * addr: virtual address to unmap
 * length: number of bytes to unmap
 * 
 * Returns 0 on success, -1 on error
 */
static int syscall_munmap_legacy(uint32_t addr, uint32_t length) {
    extern void serial_printf(const char *fmt, ...);
    extern void pmm_free_page(unsigned int address);
    extern unsigned int vmm_get_physical(unsigned int vaddr);
    extern void vmm_unmap_page(unsigned int vaddr);
    
    serial_printf("[MUNMAP] addr=0x%x len=%u\n", addr, length);
    
    if (length == 0) {
        return -1;
    }
    
    // Round to page boundaries
    uint32_t start = addr & ~(PAGE_SIZE - 1);
    uint32_t end = (addr + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Unmap each page
    for (uint32_t vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        uint32_t paddr = vmm_get_physical(vaddr);
        if (paddr) {
            // Unmap virtual page
            vmm_unmap_page(vaddr);
            
            // Free physical page
            pmm_free_page(paddr);
            
            // Invalidate TLB
            __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
        }
    }
    
    return 0;
}

