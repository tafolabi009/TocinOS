/**
 * TocinOS Process Filesystem (procfs)
 * 
 * Virtual filesystem that exposes kernel and process information.
 * Mounted at /proc, provides:
 *   /proc/[pid]/      - Per-process information
 *   /proc/cpuinfo     - CPU information
 *   /proc/meminfo     - Memory statistics
 *   /proc/uptime      - System uptime
 *   /proc/version     - Kernel version
 *   /proc/mounts      - Mounted filesystems
 *   /proc/interrupts  - Interrupt statistics
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/vfs.h"
#include "../../include/kernel/memory.h"
#include "../../include/kernel/task.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Forward declarations */
extern void serial_printf(const char *fmt, ...);
extern uint32_t timer_get_ticks(void);

/* ================================================================
 * PROCFS TYPES AND STRUCTURES
 * ================================================================ */

/* Maximum size for generated proc files */
#define PROCFS_MAX_SIZE 4096

/* Proc entry types */
#define PROC_TYPE_FILE      1
#define PROC_TYPE_DIR       2
#define PROC_TYPE_LINK      3

/* Proc file generator function type */
typedef int (*proc_generator_t)(char *buffer, int max_size);

/* Proc entry structure */
typedef struct proc_entry {
    char name[64];                  /* Entry name */
    uint8_t type;                   /* Entry type */
    uint16_t mode;                  /* Permissions */
    proc_generator_t generator;     /* Content generator for files */
    struct proc_entry *children;    /* Children for directories */
    struct proc_entry *next;        /* Next sibling */
    struct proc_entry *parent;      /* Parent directory */
} proc_entry_t;

/* Root of procfs */
static proc_entry_t *proc_root = NULL;
static int procfs_initialized = 0;

/* ================================================================
 * STRING UTILITIES
 * ================================================================ */

static int proc_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void proc_strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static int proc_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static char *proc_strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while (*src) *d++ = *src++;
    *d = '\0';
    return dest;
}

static int proc_itoa(int value, char *buf, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    int i = 0, j = 0;
    int negative = 0;
    
    if (value < 0 && base == 10) {
        negative = 1;
        value = -value;
    }
    
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    
    while (value > 0) {
        tmp[i++] = digits[value % base];
        value /= base;
    }
    
    if (negative) {
        buf[j++] = '-';
    }
    
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    
    return j;
}

static int proc_ultoa(uint32_t value, char *buf) {
    static const char digits[] = "0123456789";
    char tmp[32];
    int i = 0, j = 0;
    
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    
    while (value > 0) {
        tmp[i++] = digits[value % 10];
        value /= 10;
    }
    
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    
    return j;
}

/* ================================================================
 * PROC ENTRY MANAGEMENT
 * ================================================================ */

/**
 * Create a new proc entry
 */
static proc_entry_t *proc_create_entry(const char *name, uint8_t type, 
                                        uint16_t mode, proc_generator_t gen) {
    proc_entry_t *entry = (proc_entry_t *)kmalloc(sizeof(proc_entry_t));
    if (!entry) return NULL;
    
    proc_strcpy(entry->name, name);
    entry->type = type;
    entry->mode = mode;
    entry->generator = gen;
    entry->children = NULL;
    entry->next = NULL;
    entry->parent = NULL;
    
    return entry;
}

/**
 * Add entry to parent directory
 */
static void proc_add_entry(proc_entry_t *parent, proc_entry_t *entry) {
    if (!parent || !entry) return;
    
    entry->parent = parent;
    entry->next = parent->children;
    parent->children = entry;
}

/**
 * Find entry by name in directory
 */
static proc_entry_t *proc_find_entry(proc_entry_t *dir, const char *name) {
    if (!dir || dir->type != PROC_TYPE_DIR) return NULL;
    
    for (proc_entry_t *e = dir->children; e; e = e->next) {
        if (proc_strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

/**
 * Resolve path to proc entry
 */
static proc_entry_t *proc_resolve_path(const char *path) {
    if (!path || path[0] != '/') return NULL;
    
    /* Skip leading /proc if present */
    if (path[0] == '/' && path[1] == 'p' && path[2] == 'r' && 
        path[3] == 'o' && path[4] == 'c') {
        path += 5;
        if (*path == '/') path++;
    } else if (path[0] == '/') {
        path++;
    }
    
    if (*path == '\0') {
        return proc_root;
    }
    
    proc_entry_t *current = proc_root;
    char component[64];
    int ci = 0;
    
    while (*path) {
        if (*path == '/') {
            if (ci > 0) {
                component[ci] = '\0';
                current = proc_find_entry(current, component);
                if (!current) return NULL;
                ci = 0;
            }
            path++;
        } else {
            if (ci < 63) {
                component[ci++] = *path;
            }
            path++;
        }
    }
    
    if (ci > 0) {
        component[ci] = '\0';
        current = proc_find_entry(current, component);
    }
    
    return current;
}

/* ================================================================
 * PROC FILE GENERATORS
 * ================================================================ */

/**
 * Generate /proc/version content
 */
static int proc_gen_version(char *buffer, int max_size) {
    const char *version = "TocinOS version 0.1.0 (gcc 11.x) #1 SMP PREEMPT\n";
    int len = proc_strlen(version);
    if (len > max_size - 1) len = max_size - 1;
    
    for (int i = 0; i < len; i++) {
        buffer[i] = version[i];
    }
    buffer[len] = '\0';
    
    return len;
}

/**
 * Generate /proc/uptime content
 */
static int proc_gen_uptime(char *buffer, int max_size) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;  /* Assuming 100Hz timer */
    uint32_t idle_seconds = seconds / 10;  /* Placeholder: 10% idle */
    
    char num[32];
    int pos = 0;
    
    pos += proc_ultoa(seconds, buffer + pos);
    buffer[pos++] = '.';
    buffer[pos++] = '0';
    buffer[pos++] = '0';
    buffer[pos++] = ' ';
    
    pos += proc_ultoa(idle_seconds, buffer + pos);
    buffer[pos++] = '.';
    buffer[pos++] = '0';
    buffer[pos++] = '0';
    buffer[pos++] = '\n';
    buffer[pos] = '\0';
    
    (void)num;
    (void)max_size;
    return pos;
}

/**
 * Generate /proc/meminfo content
 */
static int proc_gen_meminfo(char *buffer, int max_size) {
    extern uint32_t pmm_get_free_pages(void);
    extern uint32_t pmm_get_total_pages(void);
    
    uint32_t free_pages = pmm_get_free_pages();
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = total_pages - free_pages;
    
    /* Convert to KB (4KB pages) */
    uint32_t total_kb = total_pages * 4;
    uint32_t free_kb = free_pages * 4;
    uint32_t used_kb = used_pages * 4;
    
    int pos = 0;
    char num[32];
    
    /* MemTotal */
    proc_strcpy(buffer + pos, "MemTotal:       ");
    pos += 16;
    pos += proc_ultoa(total_kb, buffer + pos);
    proc_strcpy(buffer + pos, " kB\n");
    pos += 4;
    
    /* MemFree */
    proc_strcpy(buffer + pos, "MemFree:        ");
    pos += 16;
    pos += proc_ultoa(free_kb, buffer + pos);
    proc_strcpy(buffer + pos, " kB\n");
    pos += 4;
    
    /* MemUsed (non-standard but useful) */
    proc_strcpy(buffer + pos, "MemUsed:        ");
    pos += 16;
    pos += proc_ultoa(used_kb, buffer + pos);
    proc_strcpy(buffer + pos, " kB\n");
    pos += 4;
    
    /* Buffers */
    proc_strcpy(buffer + pos, "Buffers:        0 kB\n");
    pos += 21;
    
    /* Cached */
    proc_strcpy(buffer + pos, "Cached:         0 kB\n");
    pos += 21;
    
    buffer[pos] = '\0';
    
    (void)num;
    (void)max_size;
    return pos;
}

/**
 * Generate /proc/cpuinfo content
 */
static int proc_gen_cpuinfo(char *buffer, int max_size) {
    extern void cpu_get_vendor(char *vendor);
    extern void cpu_get_brand(char *brand);
    extern uint32_t cpu_get_family(void);
    extern uint32_t cpu_get_model(void);
    extern uint32_t cpu_get_stepping(void);
    
    char vendor[16] = "Unknown";
    char brand[64] = "Unknown CPU";
    
    /* Try to get CPU info */
    cpu_get_vendor(vendor);
    cpu_get_brand(brand);
    
    int pos = 0;
    char num[32];
    
    /* Processor */
    proc_strcpy(buffer + pos, "processor\t: 0\n");
    pos += 14;
    
    /* Vendor */
    proc_strcpy(buffer + pos, "vendor_id\t: ");
    pos += 12;
    proc_strcpy(buffer + pos, vendor);
    pos += proc_strlen(vendor);
    buffer[pos++] = '\n';
    
    /* CPU Family */
    proc_strcpy(buffer + pos, "cpu family\t: ");
    pos += 13;
    pos += proc_itoa(cpu_get_family(), buffer + pos, 10);
    buffer[pos++] = '\n';
    
    /* Model */
    proc_strcpy(buffer + pos, "model\t\t: ");
    pos += 9;
    pos += proc_itoa(cpu_get_model(), buffer + pos, 10);
    buffer[pos++] = '\n';
    
    /* Model Name */
    proc_strcpy(buffer + pos, "model name\t: ");
    pos += 13;
    proc_strcpy(buffer + pos, brand);
    pos += proc_strlen(brand);
    buffer[pos++] = '\n';
    
    /* Stepping */
    proc_strcpy(buffer + pos, "stepping\t: ");
    pos += 11;
    pos += proc_itoa(cpu_get_stepping(), buffer + pos, 10);
    buffer[pos++] = '\n';
    
    /* CPU MHz (placeholder) */
    proc_strcpy(buffer + pos, "cpu MHz\t\t: 3000.000\n");
    pos += 20;
    
    /* Bogomips (placeholder) */
    proc_strcpy(buffer + pos, "bogomips\t: 6000.00\n");
    pos += 19;
    
    buffer[pos] = '\0';
    
    (void)num;
    (void)max_size;
    return pos;
}

/**
 * Generate /proc/mounts content
 */
static int proc_gen_mounts(char *buffer, int max_size) {
    int pos = 0;
    
    /* List mounted filesystems */
    proc_strcpy(buffer + pos, "/dev/hda1 / fat rw 0 0\n");
    pos += 23;
    
    proc_strcpy(buffer + pos, "proc /proc proc rw 0 0\n");
    pos += 23;
    
    proc_strcpy(buffer + pos, "devfs /dev devfs rw 0 0\n");
    pos += 24;
    
    proc_strcpy(buffer + pos, "tmpfs /tmp tmpfs rw 0 0\n");
    pos += 24;
    
    buffer[pos] = '\0';
    
    (void)max_size;
    return pos;
}

/**
 * Generate /proc/interrupts content
 */
static int proc_gen_interrupts(char *buffer, int max_size) {
    int pos = 0;
    
    /* Header */
    proc_strcpy(buffer + pos, "           CPU0\n");
    pos += 16;
    
    /* Timer (IRQ 0) */
    proc_strcpy(buffer + pos, "  0:       1000  PIT     timer\n");
    pos += 31;
    
    /* Keyboard (IRQ 1) */
    proc_strcpy(buffer + pos, "  1:        100  XT-PIC  keyboard\n");
    pos += 34;
    
    /* Cascade (IRQ 2) */
    proc_strcpy(buffer + pos, "  2:          0  XT-PIC  cascade\n");
    pos += 33;
    
    /* COM1 (IRQ 4) */
    proc_strcpy(buffer + pos, "  4:         50  XT-PIC  serial\n");
    pos += 32;
    
    /* IDE (IRQ 14) */
    proc_strcpy(buffer + pos, " 14:        200  XT-PIC  ide0\n");
    pos += 30;
    
    buffer[pos] = '\0';
    
    (void)max_size;
    return pos;
}

/**
 * Generate /proc/cmdline content
 */
static int proc_gen_cmdline(char *buffer, int max_size) {
    const char *cmdline = "root=/dev/hda1 console=ttyS0,115200\n";
    int len = proc_strlen(cmdline);
    if (len > max_size - 1) len = max_size - 1;
    
    for (int i = 0; i < len; i++) {
        buffer[i] = cmdline[i];
    }
    buffer[len] = '\0';
    
    return len;
}

/**
 * Generate /proc/stat content
 */
static int proc_gen_stat(char *buffer, int max_size) {
    uint32_t ticks = timer_get_ticks();
    int pos = 0;
    
    /* CPU line: user nice system idle iowait irq softirq */
    proc_strcpy(buffer + pos, "cpu  ");
    pos += 5;
    pos += proc_ultoa(ticks / 10, buffer + pos);  /* user */
    buffer[pos++] = ' ';
    buffer[pos++] = '0';  /* nice */
    buffer[pos++] = ' ';
    pos += proc_ultoa(ticks / 20, buffer + pos);  /* system */
    buffer[pos++] = ' ';
    pos += proc_ultoa(ticks * 8 / 10, buffer + pos);  /* idle */
    buffer[pos++] = ' ';
    buffer[pos++] = '0';  /* iowait */
    buffer[pos++] = ' ';
    buffer[pos++] = '0';  /* irq */
    buffer[pos++] = ' ';
    buffer[pos++] = '0';  /* softirq */
    buffer[pos++] = '\n';
    
    /* Processes */
    proc_strcpy(buffer + pos, "processes 10\n");
    pos += 13;
    
    /* Procs running */
    proc_strcpy(buffer + pos, "procs_running 1\n");
    pos += 16;
    
    /* Procs blocked */
    proc_strcpy(buffer + pos, "procs_blocked 0\n");
    pos += 16;
    
    buffer[pos] = '\0';
    
    (void)max_size;
    return pos;
}

/**
 * Generate /proc/loadavg content
 */
static int proc_gen_loadavg(char *buffer, int max_size) {
    /* Format: load1 load5 load15 running/total last_pid */
    proc_strcpy(buffer, "0.50 0.40 0.30 1/10 100\n");
    (void)max_size;
    return 24;
}

/**
 * Generate /proc/filesystems content
 */
static int proc_gen_filesystems(char *buffer, int max_size) {
    int pos = 0;
    
    proc_strcpy(buffer + pos, "\tfat\n");
    pos += 5;
    
    proc_strcpy(buffer + pos, "\text4\n");
    pos += 6;
    
    proc_strcpy(buffer + pos, "\text3\n");
    pos += 6;
    
    proc_strcpy(buffer + pos, "\text2\n");
    pos += 6;
    
    proc_strcpy(buffer + pos, "nodev\tproc\n");
    pos += 11;
    
    proc_strcpy(buffer + pos, "nodev\tdevfs\n");
    pos += 12;
    
    proc_strcpy(buffer + pos, "nodev\ttmpfs\n");
    pos += 12;
    
    buffer[pos] = '\0';
    
    (void)max_size;
    return pos;
}

/* ================================================================
 * PROCFS VFS OPERATIONS
 * ================================================================ */

static int procfs_vfs_mount(const char *device, const char *mountpoint) {
    serial_printf("[PROCFS] Mounted at %s\n", mountpoint);
    (void)device;
    return 0;
}

static int procfs_vfs_unmount(const char *mountpoint) {
    serial_printf("[PROCFS] Unmounted from %s\n", mountpoint);
    return 0;
}

static int procfs_vfs_open(vfs_node_t *node, uint32_t mode) {
    (void)node;
    (void)mode;
    return 0;
}

static int procfs_vfs_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int procfs_vfs_read(vfs_node_t *node, uint32_t offset, 
                           uint32_t size, void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    proc_entry_t *entry = (proc_entry_t *)node->fs_specific;
    
    if (entry->type != PROC_TYPE_FILE || !entry->generator) {
        return -1;
    }
    
    /* Generate content */
    static char proc_buffer[PROCFS_MAX_SIZE];
    int content_len = entry->generator(proc_buffer, PROCFS_MAX_SIZE);
    
    if (content_len < 0) {
        return -1;
    }
    
    /* Handle offset */
    if (offset >= (uint32_t)content_len) {
        return 0;  /* EOF */
    }
    
    /* Calculate bytes to read */
    int available = content_len - offset;
    int to_read = (size < (uint32_t)available) ? size : available;
    
    /* Copy data */
    char *src = proc_buffer + offset;
    char *dst = (char *)buffer;
    for (int i = 0; i < to_read; i++) {
        dst[i] = src[i];
    }
    
    return to_read;
}

static int procfs_vfs_write(vfs_node_t *node, uint32_t offset, 
                            uint32_t size, const void *buffer) {
    /* Proc files are read-only */
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return -1;
}

static int procfs_vfs_readdir(vfs_node_t *node, uint32_t index, 
                              vfs_dirent_t *dirent) {
    if (!node || !node->fs_specific || !dirent) {
        return -1;
    }
    
    proc_entry_t *dir = (proc_entry_t *)node->fs_specific;
    
    if (dir->type != PROC_TYPE_DIR) {
        return -1;
    }
    
    /* Find entry at index */
    proc_entry_t *entry = dir->children;
    uint32_t i = 0;
    
    while (entry && i < index) {
        entry = entry->next;
        i++;
    }
    
    if (!entry) {
        return -1;  /* No more entries */
    }
    
    /* Fill dirent */
    proc_strcpy(dirent->name, entry->name);
    dirent->inode = (uint32_t)entry;  /* Use pointer as inode */
    dirent->type = (entry->type == PROC_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    
    return 0;
}

static int procfs_vfs_finddir(vfs_node_t *node, const char *name, 
                              vfs_node_t *result) {
    if (!node || !node->fs_specific || !name || !result) {
        return -1;
    }
    
    proc_entry_t *dir = (proc_entry_t *)node->fs_specific;
    proc_entry_t *entry = proc_find_entry(dir, name);
    
    if (!entry) {
        return -1;
    }
    
    /* Fill result */
    proc_strcpy(result->name, entry->name);
    result->inode = (uint32_t)entry;
    result->type = (entry->type == PROC_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    result->permissions = entry->mode;
    result->size = 0;  /* Dynamic size */
    result->fs_specific = entry;
    
    return 0;
}

static int procfs_vfs_create(vfs_node_t *parent, const char *name,
                             uint32_t type, uint32_t permissions) {
    /* Cannot create files in procfs */
    (void)parent;
    (void)name;
    (void)type;
    (void)permissions;
    return -1;
}

static int procfs_vfs_unlink(vfs_node_t *parent, const char *name) {
    /* Cannot delete files in procfs */
    (void)parent;
    (void)name;
    return -1;
}

static int procfs_vfs_mkdir(vfs_node_t *parent, const char *name, 
                            uint32_t permissions) {
    /* Cannot create directories in procfs */
    (void)parent;
    (void)name;
    (void)permissions;
    return -1;
}

static int procfs_vfs_rmdir(vfs_node_t *parent, const char *name) {
    /* Cannot remove directories in procfs */
    (void)parent;
    (void)name;
    return -1;
}

/* VFS operations structure */
static vfs_fs_ops_t procfs_vfs_ops = {
    .mount = procfs_vfs_mount,
    .unmount = procfs_vfs_unmount,
    .open = procfs_vfs_open,
    .close = procfs_vfs_close,
    .read = procfs_vfs_read,
    .write = procfs_vfs_write,
    .readdir = procfs_vfs_readdir,
    .finddir = procfs_vfs_finddir,
    .create = procfs_vfs_create,
    .unlink = procfs_vfs_unlink,
    .mkdir = procfs_vfs_mkdir,
    .rmdir = procfs_vfs_rmdir
};

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/**
 * Initialize procfs
 */
int procfs_init(void) {
    if (procfs_initialized) {
        return 0;
    }
    
    serial_printf("[PROCFS] Initializing process filesystem...\n");
    
    /* Create root directory */
    proc_root = proc_create_entry("proc", PROC_TYPE_DIR, 0555, NULL);
    if (!proc_root) {
        serial_printf("[PROCFS] Failed to create root\n");
        return -1;
    }
    
    /* Create standard proc entries */
    proc_add_entry(proc_root, 
        proc_create_entry("version", PROC_TYPE_FILE, 0444, proc_gen_version));
    
    proc_add_entry(proc_root,
        proc_create_entry("uptime", PROC_TYPE_FILE, 0444, proc_gen_uptime));
    
    proc_add_entry(proc_root,
        proc_create_entry("meminfo", PROC_TYPE_FILE, 0444, proc_gen_meminfo));
    
    proc_add_entry(proc_root,
        proc_create_entry("cpuinfo", PROC_TYPE_FILE, 0444, proc_gen_cpuinfo));
    
    proc_add_entry(proc_root,
        proc_create_entry("mounts", PROC_TYPE_FILE, 0444, proc_gen_mounts));
    
    proc_add_entry(proc_root,
        proc_create_entry("interrupts", PROC_TYPE_FILE, 0444, proc_gen_interrupts));
    
    proc_add_entry(proc_root,
        proc_create_entry("cmdline", PROC_TYPE_FILE, 0444, proc_gen_cmdline));
    
    proc_add_entry(proc_root,
        proc_create_entry("stat", PROC_TYPE_FILE, 0444, proc_gen_stat));
    
    proc_add_entry(proc_root,
        proc_create_entry("loadavg", PROC_TYPE_FILE, 0444, proc_gen_loadavg));
    
    proc_add_entry(proc_root,
        proc_create_entry("filesystems", PROC_TYPE_FILE, 0444, proc_gen_filesystems));
    
    /* Create self symlink (would point to current process) */
    proc_add_entry(proc_root,
        proc_create_entry("self", PROC_TYPE_LINK, 0777, NULL));
    
    /* Register with VFS */
    if (vfs_register_fs("proc", &procfs_vfs_ops) != 0) {
        serial_printf("[PROCFS] Failed to register with VFS\n");
        return -1;
    }
    
    serial_printf("[PROCFS] Initialized with %d entries\n", 11);
    
    procfs_initialized = 1;
    return 0;
}

/**
 * Get procfs root entry
 */
proc_entry_t *procfs_get_root(void) {
    return proc_root;
}

/**
 * Read proc file by path
 */
int procfs_read_file(const char *path, char *buffer, int max_size) {
    proc_entry_t *entry = proc_resolve_path(path);
    
    if (!entry || entry->type != PROC_TYPE_FILE || !entry->generator) {
        return -1;
    }
    
    return entry->generator(buffer, max_size);
}

/**
 * List proc directory
 */
int procfs_list_dir(const char *path) {
    proc_entry_t *dir = proc_resolve_path(path);
    
    if (!dir || dir->type != PROC_TYPE_DIR) {
        return -1;
    }
    
    serial_printf("[PROCFS] Listing %s:\n", path);
    
    int count = 0;
    for (proc_entry_t *e = dir->children; e; e = e->next) {
        char type_char = (e->type == PROC_TYPE_DIR) ? 'd' : 
                         (e->type == PROC_TYPE_LINK) ? 'l' : '-';
        serial_printf("  %c %s\n", type_char, e->name);
        count++;
    }
    
    return count;
}
