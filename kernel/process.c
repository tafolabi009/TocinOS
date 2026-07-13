/**
 * TocinOS Process Management Implementation
 * 
 * Implements Unix-like process model with fork/exec/wait
 */

#include "../include/kernel/process.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/elf.h"
#include "../include/kernel/usermode.h"
#include "../include/kernel/kernel.h"

// Process table
static process_entry_t process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static process_t *current_process = 0;
static process_t *init_process = 0;

extern void serial_printf(const char *fmt, ...);

// String functions (no libc)
static int str_copy(char *dest, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

/**
 * Initialize process management
 */
void process_init(void) {
    // Clear process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].in_use = 0;
    }
    
    // Create init process (PID 1)
    // This will be the parent of all orphaned processes
    init_process = process_create("init", 0, PROC_FLAG_KERNEL);
    if (init_process) {
        init_process->pid = 1;
        init_process->ppid = 0;
        init_process->state = PROC_RUNNING;
        current_process = init_process;

        // M2: give init a memory descriptor over the LIVE boot address
        // space (its pages are in the boot directory, so the fresh
        // directory mm_create() made is swapped for the current one).
        // current_mm is what the #PF resolver and mmap operate on.
        init_process->mm = mm_create();
        if (init_process->mm) {
            mm_adopt_current_directory(init_process->mm);
            init_process->page_directory = init_process->mm->page_directory;
            current_mm = init_process->mm;
            serial_printf("[MM] init mm attached (boot directory adopted)\n");
        }
    }

    kernel_print("    Process management initialized\n");
}

/**
 * Allocate a process slot
 */
static process_t *allocate_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_table[i].in_use) {
            process_table[i].in_use = 1;
            process_t *proc = &process_table[i].process;
            
            // Clear the structure
            uint8_t *p = (uint8_t *)proc;
            for (unsigned int j = 0; j < sizeof(process_t); j++) {
                p[j] = 0;
            }
            
            // Assign PID
            proc->pid = next_pid++;
            
            return proc;
        }
    }
    return 0;
}

/**
 * Free a process slot
 */
static void free_process(process_t *proc) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (&process_table[i].process == proc) {
            process_table[i].in_use = 0;
            return;
        }
    }
}

/**
 * Get current process
 */
process_t *process_get_current(void) {
    return current_process;
}

/**
 * Get process by PID
 */
process_t *process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].in_use && process_table[i].process.pid == pid) {
            return &process_table[i].process;
        }
    }
    return 0;
}

/**
 * Get current process PID
 */
uint32_t process_get_current_pid(void) {
    return current_process ? current_process->pid : 0;
}

/**
 * Create a new process
 */
process_t *process_create(const char *name, void (*entry)(void), uint32_t flags) {
    process_t *proc = allocate_process();
    if (!proc) {
        return 0;
    }
    
    // Set name
    str_copy(proc->name, name, sizeof(proc->name));
    
    // Set flags
    proc->flags = flags;
    
    // Set parent
    proc->ppid = current_process ? current_process->pid : 0;
    proc->parent = current_process;
    
    // Set default IDs
    proc->uid = 0;  // Root by default
    proc->gid = 0;
    proc->pgid = proc->pid;
    
    // Set default working directory
    str_copy(proc->cwd, "/", sizeof(proc->cwd));
    
    // Initialize file descriptors (-1 = not open)
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        proc->files[i] = -1;
    }
    
    // Setup standard file descriptors
    proc->files[0] = 0;  // stdin
    proc->files[1] = 1;  // stdout  
    proc->files[2] = 2;  // stderr
    
    // Set entry point
    proc->entry_point = (uint32_t)entry;
    
    // Allocate kernel stack (4KB)
    proc->kernel_stack = pmm_alloc_page();
    if (!proc->kernel_stack) {
        free_process(proc);
        return 0;
    }
    proc->kernel_stack += 0x1000; // Point to top of stack
    
    // Memory will be set up by exec or fork
    proc->page_directory = 0;
    proc->heap_start = 0x08100000;
    proc->heap_end = 0x08100000;
    proc->stack_top = 0xBFFFF000;
    proc->stack_bottom = 0xBFFF0000;
    
    // Set state
    proc->state = PROC_CREATED;
    
    // Add to parent's children list
    if (current_process) {
        proc->sibling = current_process->children;
        current_process->children = proc;
    }
    
    // Create underlying task if we have an entry point
    if (entry) {
        int task_id = task_create(entry, name, SCHED_NORMAL, NICE_TO_PRIORITY(0));
        if (task_id >= 0) {
            // TODO: Link task to process properly
            // proc->task = task_get_by_id(task_id);
        }
    }
    
    return proc;
}

/**
 * Fork - create a copy of the current process
 */
int32_t process_fork(void) {
    if (!current_process) {
        return -1;
    }
    
    // Create new process
    process_t *child = allocate_process();
    if (!child) {
        return -1;
    }
    
    // Copy parent's state
    str_copy(child->name, current_process->name, sizeof(child->name));
    child->ppid = current_process->pid;
    child->parent = current_process;
    child->uid = current_process->uid;
    child->gid = current_process->gid;
    child->pgid = current_process->pgid;
    child->flags = current_process->flags & ~PROC_FLAG_KERNEL;
    child->flags |= PROC_FLAG_USER;
    
    // Copy working directory
    str_copy(child->cwd, current_process->cwd, sizeof(child->cwd));
    
    // Copy file descriptors
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        child->files[i] = current_process->files[i];
        // TODO: Increment reference counts in VFS
    }
    
    // Allocate kernel stack for child
    child->kernel_stack = pmm_alloc_page();
    if (!child->kernel_stack) {
        free_process(child);
        return -1;
    }
    child->kernel_stack += 0x1000;
    
    // COW clone of the address space (M2): the child gets its own page
    // directory and page tables, but SHARES the parent's frames — private
    // writable pages are downgraded to read-only + PAGE_COW on both sides
    // and frame refcounts are bumped (mm_clone in kernel/mm/vma.c). The
    // first write on either side breaks the share in the #PF path.
    if (current_process->mm) {
        child->mm = mm_clone(current_process->mm);
        if (!child->mm) {
            pmm_free_page(child->kernel_stack - 0x1000);
            free_process(child);
            return -1;
        }
        child->page_directory = child->mm->page_directory;
    } else {
        child->mm = 0;
        child->page_directory = 0;
    }

    // Copy memory layout
    child->heap_start = current_process->heap_start;
    child->heap_end = current_process->heap_end;
    child->stack_top = current_process->stack_top;
    child->stack_bottom = current_process->stack_bottom;
    child->entry_point = current_process->entry_point;
    
    // Add to parent's children list
    child->sibling = current_process->children;
    current_process->children = child;
    
    // Set child state to ready
    child->state = PROC_READY;
    
    // TODO: Create task for child with copied register state
    // The child's task should return 0 from this function
    // The parent returns the child's PID
    
    // For now, return child PID to parent
    // Real fork would involve scheduler to set up child's return value
    return child->pid;
}

/**
 * Exec - replace current process image with new program
 */
int process_exec(const char *path, const char *argv[], const char *envp[]) {
    (void)argv;  // TODO: Pass arguments
    (void)envp;  // TODO: Pass environment
    
    if (!current_process || !path) {
        return -1;
    }
    
    // Load ELF file
    elf_context_t elf_ctx;
    if (elf_load_file(path, &elf_ctx) != 0) {
        return -1;
    }
    
    // Update process name to executable name
    // Extract basename from path
    const char *basename = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            basename = p + 1;
        }
    }
    str_copy(current_process->name, basename, sizeof(current_process->name));
    
    // Update entry point
    current_process->entry_point = elf_ctx.entry_point;
    
    // Reset heap
    current_process->heap_start = 0x08100000;
    current_process->heap_end = 0x08100000;
    
    // Close all file descriptors except stdin/stdout/stderr
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (current_process->files[i] >= 0) {
            vfs_close(current_process->files[i]);
            current_process->files[i] = -1;
        }
    }
    
    // Execute - this doesn't return on success
    return elf_execute(&elf_ctx);
}

/**
 * Wait - wait for child process to exit
 */
int32_t process_wait(int32_t pid, int *status, int options) {
    if (!current_process) {
        return -1;
    }
    
    // Find a child to wait for
    process_t *child = 0;
    
    if (pid > 0) {
        // Wait for specific child
        child = process_get_by_pid(pid);
        if (!child || child->ppid != current_process->pid) {
            return -1; // Not our child
        }
    } else if (pid == -1) {
        // Wait for any child
        child = current_process->children;
    } else {
        // Other pid values (process group, etc.) not implemented
        return -1;
    }
    
    if (!child) {
        return -1; // No children
    }
    
    // Check for zombie children first
    for (process_t *c = current_process->children; c; c = c->sibling) {
        if ((pid == -1 || c->pid == (uint32_t)pid) && c->state == PROC_ZOMBIE) {
            // Found a zombie child
            int32_t child_pid = c->pid;
            if (status) {
                *status = c->exit_code;
            }
            
            // Remove from children list
            if (current_process->children == c) {
                current_process->children = c->sibling;
            } else {
                for (process_t *prev = current_process->children; prev; prev = prev->sibling) {
                    if (prev->sibling == c) {
                        prev->sibling = c->sibling;
                        break;
                    }
                }
            }
            
            // Free the process
            if (c->kernel_stack) {
                pmm_free_page(c->kernel_stack - 0x1000);
            }
            if (c->mm) {
                // Drops shared-frame refcounts, frees frames whose last
                // reference went away, and frees the child's page tables
                // and directory (mm/vma.c).
                mm_release(c->mm);
                c->mm = 0;
                c->page_directory = 0;
            } else if (c->page_directory) {
                pmm_free_page(c->page_directory);
            }
            free_process(c);

            return child_pid;
        }
    }
    
    // No zombie child found
    if (options & WNOHANG) {
        return 0; // Don't block
    }
    
    // Block until a child exits
    // TODO: Implement proper blocking with scheduler
    current_process->state = PROC_SLEEPING;
    task_yield();
    
    // After waking, try again
    return process_wait(pid, status, options | WNOHANG);
}

/**
 * Waitpid - Linux-compatible wait for child process
 */
int32_t process_waitpid(int32_t pid, int *status, int options) {
    return process_wait(pid, status, options);
}

/**
 * Exit - terminate current process
 */
void process_exit(int exit_code) {
    if (!current_process) {
        return;
    }
    
    // Close all open files
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (current_process->files[i] >= 0) {
            vfs_close(current_process->files[i]);
            current_process->files[i] = -1;
        }
    }
    
    // Adopt out our children to init
    process_adopt_orphans(current_process);
    
    // Set exit code and become zombie
    current_process->exit_code = exit_code;
    current_process->state = PROC_ZOMBIE;
    
    // Wake parent if waiting
    if (current_process->parent && current_process->parent->state == PROC_SLEEPING) {
        current_process->parent->state = PROC_READY;
        task_wakeup(current_process->parent->task ? current_process->parent->task->id : 0);
    }
    
    // Yield - won't return
    task_exit(exit_code);
}

/**
 * Kill a process
 */
int process_kill(uint32_t pid, int signal) {
    (void)signal; // Signals not implemented yet
    
    process_t *proc = process_get_by_pid(pid);
    if (!proc) {
        return -1;
    }
    
    // Can't kill init
    if (proc->pid == 1) {
        return -1;
    }
    
    // Terminate the process
    proc->exit_code = 128 + signal; // Killed by signal
    proc->state = PROC_ZOMBIE;
    
    // Adopt out children
    process_adopt_orphans(proc);
    
    return 0;
}

/**
 * Get current PID
 */
int process_getpid(void) {
    return current_process ? current_process->pid : 0;
}

/**
 * Get parent PID
 */
int process_getppid(void) {
    return current_process ? current_process->ppid : 0;
}

/**
 * Set program break
 */
int process_brk(uint32_t addr) {
    if (!current_process) {
        return -1;
    }
    
    if (addr == 0) {
        return current_process->heap_end;
    }
    
    // Validate address
    if (addr < current_process->heap_start || addr >= current_process->stack_bottom) {
        return -1;
    }
    
    // TODO: Allocate/free pages as needed
    current_process->heap_end = addr;
    return 0;
}

/**
 * Increment program break
 */
void *process_sbrk(int32_t increment) {
    if (!current_process) {
        return (void *)-1;
    }
    
    uint32_t old_break = current_process->heap_end;
    uint32_t new_break = old_break + increment;
    
    if (new_break < current_process->heap_start || new_break >= current_process->stack_bottom) {
        return (void *)-1;
    }
    
    current_process->heap_end = new_break;
    return (void *)old_break;
}

/**
 * Duplicate file descriptor
 */
int process_dup_fd(int oldfd) {
    if (!current_process || oldfd < 0 || oldfd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    if (current_process->files[oldfd] < 0) {
        return -1; // Not open
    }
    
    // Find first free slot
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (current_process->files[i] < 0) {
            current_process->files[i] = current_process->files[oldfd];
            // TODO: Increment reference count in VFS
            return i;
        }
    }
    
    return -1; // No free slots
}

/**
 * Duplicate file descriptor to specific fd
 */
int process_dup2_fd(int oldfd, int newfd) {
    if (!current_process) {
        return -1;
    }
    
    if (oldfd < 0 || oldfd >= MAX_OPEN_FILES ||
        newfd < 0 || newfd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    if (current_process->files[oldfd] < 0) {
        return -1;
    }
    
    if (oldfd == newfd) {
        return newfd;
    }
    
    // Close newfd if open
    if (current_process->files[newfd] >= 0) {
        vfs_close(current_process->files[newfd]);
    }
    
    current_process->files[newfd] = current_process->files[oldfd];
    // TODO: Increment reference count
    
    return newfd;
}

/**
 * Close file descriptor
 */
int process_close_fd(int fd) {
    if (!current_process || fd < 0 || fd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    if (current_process->files[fd] < 0) {
        return -1;
    }
    
    int result = vfs_close(current_process->files[fd]);
    current_process->files[fd] = -1;
    return result;
}

/**
 * Change working directory
 */
int process_chdir(const char *path) {
    if (!current_process || !path) {
        return -1;
    }
    
    // TODO: Validate path exists and is a directory
    str_copy(current_process->cwd, path, sizeof(current_process->cwd));
    return 0;
}

/**
 * Get current working directory
 */
int process_getcwd(char *buf, uint32_t size) {
    if (!current_process || !buf || size == 0) {
        return -1;
    }
    
    int len = str_len(current_process->cwd) + 1;
    if ((uint32_t)len > size) {
        return -1;
    }
    
    str_copy(buf, current_process->cwd, size);
    return 0;
}

/**
 * Adopt orphaned children (when parent dies)
 */
void process_adopt_orphans(process_t *dead_parent) {
    if (!dead_parent || !init_process) {
        return;
    }
    
    // Move all children to init
    process_t *child = dead_parent->children;
    while (child) {
        process_t *next = child->sibling;
        
        child->ppid = init_process->pid;
        child->parent = init_process;
        child->flags |= PROC_FLAG_ORPHAN;
        
        // Add to init's children
        child->sibling = init_process->children;
        init_process->children = child;
        
        child = next;
    }
    
    dead_parent->children = 0;
}

/**
 * Reap zombie processes
 */
void process_reap_zombies(void) {
    // This is typically called by init
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].in_use) {
            process_t *proc = &process_table[i].process;
            if (proc->state == PROC_ZOMBIE && (proc->flags & PROC_FLAG_ORPHAN)) {
                // Free zombie orphan
                if (proc->kernel_stack) {
                    pmm_free_page(proc->kernel_stack - 0x1000);
                }
                if (proc->mm) {
                    mm_release(proc->mm);
                    proc->mm = 0;
                    proc->page_directory = 0;
                } else if (proc->page_directory) {
                    pmm_free_page(proc->page_directory);
                }
                free_process(proc);
            }
        }
    }
}
