/**
 * TocinOS Device Filesystem (devfs)
 * 
 * Virtual filesystem for device nodes.
 * Mounted at /dev, provides:
 *   /dev/null     - Null device (discards all writes)
 *   /dev/zero     - Zero device (returns zeros on read)
 *   /dev/random   - Random number generator
 *   /dev/tty      - Current terminal
 *   /dev/console  - System console
 *   /dev/hda      - First hard disk
 *   /dev/ttyS0    - First serial port
 *   /dev/mem      - Physical memory access
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/vfs.h"
#include "../../include/kernel/memory.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Forward declarations */
extern void serial_printf(const char *fmt, ...);
extern uint32_t timer_get_ticks(void);

/* ================================================================
 * DEVFS TYPES AND STRUCTURES
 * ================================================================ */

/* Device types */
#define DEV_TYPE_CHAR   1   /* Character device */
#define DEV_TYPE_BLOCK  2   /* Block device */
#define DEV_TYPE_DIR    3   /* Directory */

/* Major device numbers */
#define DEV_MAJOR_MEM   1   /* /dev/mem, /dev/null, /dev/zero, /dev/random */
#define DEV_MAJOR_TTY   4   /* TTY devices */
#define DEV_MAJOR_TTYAUX 5  /* TTY aux (console, tty) */
#define DEV_MAJOR_IDE   3   /* IDE disks */
#define DEV_MAJOR_SERIAL 4  /* Serial ports */

/* Minor device numbers for memory devices */
#define DEV_MINOR_NULL   3
#define DEV_MINOR_ZERO   5
#define DEV_MINOR_RANDOM 8
#define DEV_MINOR_URANDOM 9

/* Device read/write callbacks */
typedef int (*dev_read_t)(uint32_t minor, void *buffer, uint32_t size, uint32_t offset);
typedef int (*dev_write_t)(uint32_t minor, const void *buffer, uint32_t size, uint32_t offset);
typedef int (*dev_ioctl_t)(uint32_t minor, uint32_t cmd, void *arg);

/* Device driver structure */
typedef struct dev_driver {
    uint32_t major;             /* Major device number */
    char name[32];              /* Driver name */
    dev_read_t read;            /* Read function */
    dev_write_t write;          /* Write function */
    dev_ioctl_t ioctl;          /* IOCTL function */
    struct dev_driver *next;    /* Next driver */
} dev_driver_t;

/* Device node structure */
typedef struct dev_node {
    char name[64];              /* Device name */
    uint8_t type;               /* Character or block */
    uint32_t major;             /* Major number */
    uint32_t minor;             /* Minor number */
    uint16_t mode;              /* Permissions */
    struct dev_node *children;  /* Children (for directories) */
    struct dev_node *next;      /* Next sibling */
    struct dev_node *parent;    /* Parent directory */
} dev_node_t;

/* Global state */
static dev_node_t *devfs_root = NULL;
static dev_driver_t *dev_drivers = NULL;
static int devfs_initialized = 0;

/* Simple LFSR for random numbers */
static uint32_t random_state = 0x12345678;

/* ================================================================
 * STRING UTILITIES
 * ================================================================ */

static void dev_strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static int dev_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static int dev_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* ================================================================
 * RANDOM NUMBER GENERATOR
 * ================================================================ */

static uint32_t dev_random(void) {
    /* LFSR-based PRNG */
    uint32_t bit = ((random_state >> 0) ^ (random_state >> 2) ^
                    (random_state >> 3) ^ (random_state >> 5)) & 1;
    random_state = (random_state >> 1) | (bit << 31);
    
    /* Mix with timer ticks for additional entropy */
    random_state ^= timer_get_ticks();
    
    return random_state;
}

/* ================================================================
 * BUILT-IN DEVICE DRIVERS
 * ================================================================ */

/**
 * Memory devices read
 */
static int mem_device_read(uint32_t minor, void *buffer, uint32_t size, uint32_t offset) {
    uint8_t *buf = (uint8_t *)buffer;
    (void)offset;
    
    switch (minor) {
        case DEV_MINOR_NULL:
            /* /dev/null - returns EOF immediately */
            return 0;
            
        case DEV_MINOR_ZERO:
            /* /dev/zero - returns zeros */
            for (uint32_t i = 0; i < size; i++) {
                buf[i] = 0;
            }
            return size;
            
        case DEV_MINOR_RANDOM:
        case DEV_MINOR_URANDOM:
            /* /dev/random, /dev/urandom - returns random bytes */
            for (uint32_t i = 0; i < size; i++) {
                if (i % 4 == 0) {
                    *(uint32_t *)&buf[i] = dev_random();
                }
            }
            return size;
            
        default:
            return -1;
    }
}

/**
 * Memory devices write
 */
static int mem_device_write(uint32_t minor, const void *buffer, uint32_t size, uint32_t offset) {
    (void)buffer;
    (void)offset;
    
    switch (minor) {
        case DEV_MINOR_NULL:
            /* /dev/null - discards all writes */
            return size;
            
        case DEV_MINOR_ZERO:
            /* /dev/zero - discards all writes */
            return size;
            
        case DEV_MINOR_RANDOM:
        case DEV_MINOR_URANDOM:
            /* Add entropy from writes */
            {
                const uint8_t *buf = (const uint8_t *)buffer;
                for (uint32_t i = 0; i < size; i++) {
                    random_state ^= ((uint32_t)buf[i]) << ((i % 4) * 8);
                }
            }
            return size;
            
        default:
            return -1;
    }
}

/**
 * TTY device read
 */
static int tty_device_read(uint32_t minor, void *buffer, uint32_t size, uint32_t offset) {
    (void)minor;
    (void)offset;
    
    /* Read from keyboard */
    extern char keyboard_getchar(void);
    extern int keyboard_has_data(void);
    
    char *buf = (char *)buffer;
    uint32_t i = 0;
    
    while (i < size && keyboard_has_data()) {
        buf[i++] = keyboard_getchar();
    }
    
    return i;
}

/**
 * TTY device write
 */
static int tty_device_write(uint32_t minor, const void *buffer, uint32_t size, uint32_t offset) {
    (void)minor;
    (void)offset;
    
    /* Write to console */
    extern void kernel_print_char(char c);
    const char *buf = (const char *)buffer;
    
    for (uint32_t i = 0; i < size; i++) {
        kernel_print_char(buf[i]);
    }
    
    return size;
}

/**
 * Serial device read
 */
static int serial_device_read(uint32_t minor, void *buffer, uint32_t size, uint32_t offset) {
    (void)minor;
    (void)offset;
    
    extern int serial_read(uint16_t port, char *data, int len);
    return serial_read(0x3F8, (char *)buffer, size);
}

/**
 * Serial device write
 */
static int serial_device_write(uint32_t minor, const void *buffer, uint32_t size, uint32_t offset) {
    (void)minor;
    (void)offset;
    
    extern void serial_write(uint16_t port, const char *data);
    const char *buf = (const char *)buffer;
    
    for (uint32_t i = 0; i < size; i++) {
        char c[2] = { buf[i], 0 };
        serial_write(0x3F8, c);
    }
    
    return size;
}

/**
 * IDE device read
 */
static int ide_device_read(uint32_t minor, void *buffer, uint32_t size, uint32_t offset) {
    extern int ide_read_sector(uint8_t drive, uint32_t lba, void *buffer);
    
    uint8_t drive = minor & 0x0F;
    uint32_t lba = offset / 512;
    uint32_t sector_offset = offset % 512;
    
    static uint8_t sector_buf[512];
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t bytes_read = 0;
    
    while (bytes_read < size) {
        if (ide_read_sector(drive, lba, sector_buf) != 0) {
            break;
        }
        
        uint32_t to_copy = 512 - sector_offset;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }
        
        for (uint32_t i = 0; i < to_copy; i++) {
            dst[bytes_read + i] = sector_buf[sector_offset + i];
        }
        
        bytes_read += to_copy;
        sector_offset = 0;
        lba++;
    }
    
    return bytes_read;
}

/**
 * IDE device write
 */
static int ide_device_write(uint32_t minor, const void *buffer, uint32_t size, uint32_t offset) {
    extern int ide_write_sector(uint8_t drive, uint32_t lba, const void *buffer);
    
    uint8_t drive = minor & 0x0F;
    uint32_t lba = offset / 512;
    uint32_t sector_offset = offset % 512;
    
    static uint8_t sector_buf[512];
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t bytes_written = 0;
    
    while (bytes_written < size) {
        /* Read sector if partial write */
        if (sector_offset != 0 || (size - bytes_written) < 512) {
            extern int ide_read_sector(uint8_t drive, uint32_t lba, void *buffer);
            ide_read_sector(drive, lba, sector_buf);
        }
        
        uint32_t to_copy = 512 - sector_offset;
        if (to_copy > size - bytes_written) {
            to_copy = size - bytes_written;
        }
        
        for (uint32_t i = 0; i < to_copy; i++) {
            sector_buf[sector_offset + i] = src[bytes_written + i];
        }
        
        if (ide_write_sector(drive, lba, sector_buf) != 0) {
            break;
        }
        
        bytes_written += to_copy;
        sector_offset = 0;
        lba++;
    }
    
    return bytes_written;
}

/* ================================================================
 * DEVICE MANAGEMENT
 * ================================================================ */

/**
 * Register device driver
 */
int devfs_register_driver(uint32_t major, const char *name,
                          dev_read_t read, dev_write_t write, dev_ioctl_t ioctl) {
    dev_driver_t *drv = (dev_driver_t *)kmalloc(sizeof(dev_driver_t));
    if (!drv) return -1;
    
    drv->major = major;
    dev_strcpy(drv->name, name);
    drv->read = read;
    drv->write = write;
    drv->ioctl = ioctl;
    drv->next = dev_drivers;
    dev_drivers = drv;
    
    serial_printf("[DEVFS] Registered driver '%s' (major %d)\n", name, major);
    return 0;
}

/**
 * Find driver by major number
 */
static dev_driver_t *devfs_find_driver(uint32_t major) {
    for (dev_driver_t *drv = dev_drivers; drv; drv = drv->next) {
        if (drv->major == major) {
            return drv;
        }
    }
    return NULL;
}

/**
 * Create device node
 */
static dev_node_t *dev_create_node(const char *name, uint8_t type,
                                    uint32_t major, uint32_t minor, uint16_t mode) {
    dev_node_t *node = (dev_node_t *)kmalloc(sizeof(dev_node_t));
    if (!node) return NULL;
    
    dev_strcpy(node->name, name);
    node->type = type;
    node->major = major;
    node->minor = minor;
    node->mode = mode;
    node->children = NULL;
    node->next = NULL;
    node->parent = NULL;
    
    return node;
}

/**
 * Add node to parent
 */
static void dev_add_node(dev_node_t *parent, dev_node_t *node) {
    if (!parent || !node) return;
    
    node->parent = parent;
    node->next = parent->children;
    parent->children = node;
}

/**
 * Find node by name
 */
static dev_node_t *dev_find_node(dev_node_t *dir, const char *name) {
    if (!dir || dir->type != DEV_TYPE_DIR) return NULL;
    
    for (dev_node_t *n = dir->children; n; n = n->next) {
        if (dev_strcmp(n->name, name) == 0) {
            return n;
        }
    }
    return NULL;
}

/**
 * Resolve path to device node
 */
static dev_node_t *devfs_resolve_path(const char *path) {
    if (!path || path[0] != '/') return NULL;
    
    /* Skip leading /dev if present */
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v') {
        path += 4;
        if (*path == '/') path++;
    } else if (path[0] == '/') {
        path++;
    }
    
    if (*path == '\0') {
        return devfs_root;
    }
    
    dev_node_t *current = devfs_root;
    char component[64];
    int ci = 0;
    
    while (*path) {
        if (*path == '/') {
            if (ci > 0) {
                component[ci] = '\0';
                current = dev_find_node(current, component);
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
        current = dev_find_node(current, component);
    }
    
    return current;
}

/* ================================================================
 * DEVFS VFS OPERATIONS
 * ================================================================ */

static int devfs_vfs_mount(const char *device, const char *mountpoint) {
    serial_printf("[DEVFS] Mounted at %s\n", mountpoint);
    (void)device;
    return 0;
}

static int devfs_vfs_unmount(const char *mountpoint) {
    serial_printf("[DEVFS] Unmounted from %s\n", mountpoint);
    return 0;
}

static int devfs_vfs_open(vfs_node_t *node, uint32_t mode) {
    (void)node;
    (void)mode;
    return 0;
}

static int devfs_vfs_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int devfs_vfs_read(vfs_node_t *node, uint32_t offset, 
                          uint32_t size, void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    dev_node_t *dev = (dev_node_t *)node->fs_specific;
    
    if (dev->type == DEV_TYPE_DIR) {
        return -1;
    }
    
    /* Find driver */
    dev_driver_t *drv = devfs_find_driver(dev->major);
    if (!drv || !drv->read) {
        return -1;
    }
    
    return drv->read(dev->minor, buffer, size, offset);
}

static int devfs_vfs_write(vfs_node_t *node, uint32_t offset, 
                           uint32_t size, const void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    dev_node_t *dev = (dev_node_t *)node->fs_specific;
    
    if (dev->type == DEV_TYPE_DIR) {
        return -1;
    }
    
    /* Find driver */
    dev_driver_t *drv = devfs_find_driver(dev->major);
    if (!drv || !drv->write) {
        return -1;
    }
    
    return drv->write(dev->minor, buffer, size, offset);
}

static int devfs_vfs_readdir(vfs_node_t *node, uint32_t index, 
                             vfs_dirent_t *dirent) {
    if (!node || !node->fs_specific || !dirent) {
        return -1;
    }
    
    dev_node_t *dir = (dev_node_t *)node->fs_specific;
    
    if (dir->type != DEV_TYPE_DIR) {
        return -1;
    }
    
    /* Find entry at index */
    dev_node_t *entry = dir->children;
    uint32_t i = 0;
    
    while (entry && i < index) {
        entry = entry->next;
        i++;
    }
    
    if (!entry) {
        return -1;
    }
    
    dev_strcpy(dirent->name, entry->name);
    dirent->inode = (uint32_t)entry;
    dirent->type = (entry->type == DEV_TYPE_DIR) ? VFS_TYPE_DIR : 
                   (entry->type == DEV_TYPE_CHAR) ? VFS_TYPE_CHARDEV : VFS_TYPE_BLOCKDEV;
    
    return 0;
}

static int devfs_vfs_finddir(vfs_node_t *node, const char *name, 
                             vfs_node_t *result) {
    if (!node || !node->fs_specific || !name || !result) {
        return -1;
    }
    
    dev_node_t *dir = (dev_node_t *)node->fs_specific;
    dev_node_t *entry = dev_find_node(dir, name);
    
    if (!entry) {
        return -1;
    }
    
    dev_strcpy(result->name, entry->name);
    result->inode = (uint32_t)entry;
    result->type = (entry->type == DEV_TYPE_DIR) ? VFS_TYPE_DIR :
                   (entry->type == DEV_TYPE_CHAR) ? VFS_TYPE_CHARDEV : VFS_TYPE_BLOCKDEV;
    result->permissions = entry->mode;
    result->size = 0;
    result->fs_specific = entry;
    
    return 0;
}

static int devfs_vfs_create(vfs_node_t *parent, const char *name,
                            uint32_t type, uint32_t permissions) {
    (void)parent;
    (void)name;
    (void)type;
    (void)permissions;
    return -1;
}

static int devfs_vfs_unlink(vfs_node_t *parent, const char *name) {
    (void)parent;
    (void)name;
    return -1;
}

static int devfs_vfs_mkdir(vfs_node_t *parent, const char *name, 
                           uint32_t permissions) {
    (void)parent;
    (void)name;
    (void)permissions;
    return -1;
}

static int devfs_vfs_rmdir(vfs_node_t *parent, const char *name) {
    (void)parent;
    (void)name;
    return -1;
}

/* VFS operations structure */
static vfs_fs_ops_t devfs_vfs_ops = {
    .mount = devfs_vfs_mount,
    .unmount = devfs_vfs_unmount,
    .open = devfs_vfs_open,
    .close = devfs_vfs_close,
    .read = devfs_vfs_read,
    .write = devfs_vfs_write,
    .readdir = devfs_vfs_readdir,
    .finddir = devfs_vfs_finddir,
    .create = devfs_vfs_create,
    .unlink = devfs_vfs_unlink,
    .mkdir = devfs_vfs_mkdir,
    .rmdir = devfs_vfs_rmdir
};

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/**
 * Initialize devfs
 */
int devfs_init(void) {
    if (devfs_initialized) {
        return 0;
    }
    
    serial_printf("[DEVFS] Initializing device filesystem...\n");
    
    /* Create root directory */
    devfs_root = dev_create_node("dev", DEV_TYPE_DIR, 0, 0, 0755);
    if (!devfs_root) {
        serial_printf("[DEVFS] Failed to create root\n");
        return -1;
    }
    
    /* Register built-in drivers */
    devfs_register_driver(DEV_MAJOR_MEM, "mem", mem_device_read, mem_device_write, NULL);
    devfs_register_driver(DEV_MAJOR_TTY, "tty", tty_device_read, tty_device_write, NULL);
    devfs_register_driver(DEV_MAJOR_SERIAL, "serial", serial_device_read, serial_device_write, NULL);
    devfs_register_driver(DEV_MAJOR_IDE, "ide", ide_device_read, ide_device_write, NULL);
    
    /* Create standard device nodes */
    
    /* Memory devices */
    dev_add_node(devfs_root, dev_create_node("null", DEV_TYPE_CHAR, 
                 DEV_MAJOR_MEM, DEV_MINOR_NULL, 0666));
    dev_add_node(devfs_root, dev_create_node("zero", DEV_TYPE_CHAR,
                 DEV_MAJOR_MEM, DEV_MINOR_ZERO, 0666));
    dev_add_node(devfs_root, dev_create_node("random", DEV_TYPE_CHAR,
                 DEV_MAJOR_MEM, DEV_MINOR_RANDOM, 0666));
    dev_add_node(devfs_root, dev_create_node("urandom", DEV_TYPE_CHAR,
                 DEV_MAJOR_MEM, DEV_MINOR_URANDOM, 0666));
    
    /* TTY devices */
    dev_add_node(devfs_root, dev_create_node("tty", DEV_TYPE_CHAR,
                 DEV_MAJOR_TTYAUX, 0, 0666));
    dev_add_node(devfs_root, dev_create_node("console", DEV_TYPE_CHAR,
                 DEV_MAJOR_TTYAUX, 1, 0600));
    dev_add_node(devfs_root, dev_create_node("tty0", DEV_TYPE_CHAR,
                 DEV_MAJOR_TTY, 0, 0620));
    dev_add_node(devfs_root, dev_create_node("tty1", DEV_TYPE_CHAR,
                 DEV_MAJOR_TTY, 1, 0620));
    
    /* Serial ports */
    dev_add_node(devfs_root, dev_create_node("ttyS0", DEV_TYPE_CHAR,
                 DEV_MAJOR_SERIAL, 0, 0660));
    dev_add_node(devfs_root, dev_create_node("ttyS1", DEV_TYPE_CHAR,
                 DEV_MAJOR_SERIAL, 1, 0660));
    
    /* IDE disks */
    dev_add_node(devfs_root, dev_create_node("hda", DEV_TYPE_BLOCK,
                 DEV_MAJOR_IDE, 0, 0660));
    dev_add_node(devfs_root, dev_create_node("hdb", DEV_TYPE_BLOCK,
                 DEV_MAJOR_IDE, 1, 0660));
    dev_add_node(devfs_root, dev_create_node("hdc", DEV_TYPE_BLOCK,
                 DEV_MAJOR_IDE, 2, 0660));
    dev_add_node(devfs_root, dev_create_node("hdd", DEV_TYPE_BLOCK,
                 DEV_MAJOR_IDE, 3, 0660));
    
    /* Standard directories */
    dev_node_t *pts_dir = dev_create_node("pts", DEV_TYPE_DIR, 0, 0, 0755);
    dev_add_node(devfs_root, pts_dir);
    
    dev_node_t *shm_dir = dev_create_node("shm", DEV_TYPE_DIR, 0, 0, 01777);
    dev_add_node(devfs_root, shm_dir);
    
    /* Register with VFS */
    if (vfs_register_fs("devfs", &devfs_vfs_ops) != 0) {
        serial_printf("[DEVFS] Failed to register with VFS\n");
        return -1;
    }
    
    serial_printf("[DEVFS] Created %d device nodes\n", 16);
    
    devfs_initialized = 1;
    return 0;
}

/**
 * Create a new device node dynamically
 */
int devfs_mknod(const char *path, uint8_t type, uint32_t major, uint32_t minor) {
    if (!devfs_initialized) return -1;
    
    /* Parse path to get parent and name */
    char name[64];
    int len = dev_strlen(path);
    int last_slash = -1;
    
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    /* Extract name */
    if (last_slash >= 0) {
        dev_strcpy(name, path + last_slash + 1);
    } else {
        dev_strcpy(name, path);
    }
    
    /* Create node in root for now */
    dev_node_t *node = dev_create_node(name, type, major, minor, 0660);
    if (!node) return -1;
    
    dev_add_node(devfs_root, node);
    
    serial_printf("[DEVFS] Created device %s (%d, %d)\n", name, major, minor);
    return 0;
}

/**
 * List devices
 */
int devfs_list(void) {
    if (!devfs_initialized) return -1;
    
    serial_printf("[DEVFS] Device listing:\n");
    
    int count = 0;
    for (dev_node_t *n = devfs_root->children; n; n = n->next) {
        char type_char = (n->type == DEV_TYPE_DIR) ? 'd' :
                         (n->type == DEV_TYPE_CHAR) ? 'c' : 'b';
        serial_printf("  %c %3d, %3d  %s\n", type_char, n->major, n->minor, n->name);
        count++;
    }
    
    return count;
}

/**
 * Read from device by path
 */
int devfs_read(const char *path, void *buffer, uint32_t size, uint32_t offset) {
    dev_node_t *node = devfs_resolve_path(path);
    if (!node || node->type == DEV_TYPE_DIR) {
        return -1;
    }
    
    dev_driver_t *drv = devfs_find_driver(node->major);
    if (!drv || !drv->read) {
        return -1;
    }
    
    return drv->read(node->minor, buffer, size, offset);
}

/**
 * Write to device by path
 */
int devfs_write(const char *path, const void *buffer, uint32_t size, uint32_t offset) {
    dev_node_t *node = devfs_resolve_path(path);
    if (!node || node->type == DEV_TYPE_DIR) {
        return -1;
    }
    
    dev_driver_t *drv = devfs_find_driver(node->major);
    if (!drv || !drv->write) {
        return -1;
    }
    
    return drv->write(node->minor, buffer, size, offset);
}
