/**
 * TocinOS Temporary Filesystem (tmpfs)
 * 
 * RAM-based filesystem that stores all data in memory.
 * Typically mounted at /tmp, /run, /dev/shm.
 * Data is lost on reboot.
 * 
 * Features:
 *   - Fast in-memory storage
 *   - Dynamic allocation (grows as needed)
 *   - POSIX permissions
 *   - Hard and symbolic links
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
 * TMPFS CONFIGURATION
 * ================================================================ */

/* Maximum file size (1 MB) */
#define TMPFS_MAX_FILE_SIZE     (1024 * 1024)

/* Maximum filename length */
#define TMPFS_MAX_NAME          64

/* Maximum path length */
#define TMPFS_MAX_PATH          256

/* Default max size (16 MB) */
#define TMPFS_DEFAULT_MAX_SIZE  (16 * 1024 * 1024)

/* Block size for data storage */
#define TMPFS_BLOCK_SIZE        4096

/* ================================================================
 * TMPFS TYPES AND STRUCTURES
 * ================================================================ */

/* Inode types */
#define TMPFS_TYPE_FILE     1
#define TMPFS_TYPE_DIR      2
#define TMPFS_TYPE_SYMLINK  3

/* Data block list */
typedef struct tmpfs_block {
    void *data;                     /* Block data (4KB) */
    struct tmpfs_block *next;       /* Next block */
} tmpfs_block_t;

/* Inode structure */
typedef struct tmpfs_inode {
    uint32_t ino;                   /* Inode number */
    uint8_t type;                   /* File type */
    uint16_t mode;                  /* Permissions */
    uint16_t uid;                   /* Owner user ID */
    uint16_t gid;                   /* Owner group ID */
    uint16_t nlink;                 /* Number of links */
    uint32_t size;                  /* File size */
    uint32_t atime;                 /* Access time */
    uint32_t mtime;                 /* Modification time */
    uint32_t ctime;                 /* Creation time */
    
    union {
        tmpfs_block_t *blocks;      /* Data blocks (for files) */
        struct tmpfs_dentry *entries; /* Directory entries (for dirs) */
        char *symlink_target;       /* Symlink target */
    } data;
    
    struct tmpfs_inode *next;       /* Next inode in list */
} tmpfs_inode_t;

/* Directory entry */
typedef struct tmpfs_dentry {
    char name[TMPFS_MAX_NAME];      /* Entry name */
    tmpfs_inode_t *inode;           /* Associated inode */
    struct tmpfs_dentry *next;      /* Next entry */
} tmpfs_dentry_t;

/* Filesystem instance */
typedef struct tmpfs_instance {
    char mountpoint[TMPFS_MAX_PATH];/* Mount point */
    uint32_t max_size;              /* Maximum size */
    uint32_t used_size;             /* Current used size */
    uint32_t next_ino;              /* Next inode number */
    tmpfs_inode_t *root;            /* Root inode */
    tmpfs_inode_t *inodes;          /* All inodes list */
    struct tmpfs_instance *next;    /* Next instance */
} tmpfs_instance_t;

/* Global state */
static tmpfs_instance_t *tmpfs_instances = NULL;
static int tmpfs_initialized = 0;

/* ================================================================
 * STRING UTILITIES
 * ================================================================ */

static void tmpfs_strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static int tmpfs_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static int tmpfs_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void tmpfs_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void tmpfs_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

/* ================================================================
 * INODE MANAGEMENT
 * ================================================================ */

/**
 * Get current time (placeholder)
 */
static uint32_t tmpfs_get_time(void) {
    return timer_get_ticks() / 100;  /* Seconds since boot */
}

/**
 * Create new inode
 */
static tmpfs_inode_t *tmpfs_create_inode(tmpfs_instance_t *fs, uint8_t type, uint16_t mode) {
    tmpfs_inode_t *inode = (tmpfs_inode_t *)kmalloc(sizeof(tmpfs_inode_t));
    if (!inode) return NULL;
    
    uint32_t now = tmpfs_get_time();
    
    inode->ino = fs->next_ino++;
    inode->type = type;
    inode->mode = mode;
    inode->uid = 0;
    inode->gid = 0;
    inode->nlink = 1;
    inode->size = 0;
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->data.blocks = NULL;
    inode->next = fs->inodes;
    fs->inodes = inode;
    
    return inode;
}

/**
 * Free inode and its data
 */
static void tmpfs_free_inode(tmpfs_instance_t *fs, tmpfs_inode_t *inode) {
    if (!inode) return;
    
    /* Free data based on type */
    switch (inode->type) {
        case TMPFS_TYPE_FILE:
            /* Free data blocks */
            {
                tmpfs_block_t *block = inode->data.blocks;
                while (block) {
                    tmpfs_block_t *next = block->next;
                    if (block->data) {
                        fs->used_size -= TMPFS_BLOCK_SIZE;
                        kfree(block->data);
                    }
                    kfree(block);
                    block = next;
                }
            }
            break;
            
        case TMPFS_TYPE_DIR:
            /* Free directory entries */
            {
                tmpfs_dentry_t *entry = inode->data.entries;
                while (entry) {
                    tmpfs_dentry_t *next = entry->next;
                    kfree(entry);
                    entry = next;
                }
            }
            break;
            
        case TMPFS_TYPE_SYMLINK:
            if (inode->data.symlink_target) {
                kfree(inode->data.symlink_target);
            }
            break;
    }
    
    /* Remove from inode list */
    if (fs->inodes == inode) {
        fs->inodes = inode->next;
    } else {
        for (tmpfs_inode_t *i = fs->inodes; i; i = i->next) {
            if (i->next == inode) {
                i->next = inode->next;
                break;
            }
        }
    }
    
    kfree(inode);
}

/**
 * Find inode by number
 */
static tmpfs_inode_t *tmpfs_find_inode(tmpfs_instance_t *fs, uint32_t ino) {
    for (tmpfs_inode_t *i = fs->inodes; i; i = i->next) {
        if (i->ino == ino) return i;
    }
    return NULL;
}

/* ================================================================
 * DIRECTORY OPERATIONS
 * ================================================================ */

/**
 * Add entry to directory
 */
static int tmpfs_dir_add_entry(tmpfs_inode_t *dir, const char *name, tmpfs_inode_t *inode) {
    if (!dir || dir->type != TMPFS_TYPE_DIR || !name || !inode) {
        return -1;
    }
    
    tmpfs_dentry_t *entry = (tmpfs_dentry_t *)kmalloc(sizeof(tmpfs_dentry_t));
    if (!entry) return -1;
    
    tmpfs_strcpy(entry->name, name);
    entry->inode = inode;
    entry->next = dir->data.entries;
    dir->data.entries = entry;
    
    inode->nlink++;
    
    return 0;
}

/**
 * Remove entry from directory
 */
static int tmpfs_dir_remove_entry(tmpfs_inode_t *dir, const char *name) {
    if (!dir || dir->type != TMPFS_TYPE_DIR || !name) {
        return -1;
    }
    
    tmpfs_dentry_t **prev = &dir->data.entries;
    tmpfs_dentry_t *entry = dir->data.entries;
    
    while (entry) {
        if (tmpfs_strcmp(entry->name, name) == 0) {
            *prev = entry->next;
            entry->inode->nlink--;
            kfree(entry);
            return 0;
        }
        prev = &entry->next;
        entry = entry->next;
    }
    
    return -1;  /* Not found */
}

/**
 * Find entry in directory
 */
static tmpfs_dentry_t *tmpfs_dir_find_entry(tmpfs_inode_t *dir, const char *name) {
    if (!dir || dir->type != TMPFS_TYPE_DIR || !name) {
        return NULL;
    }
    
    for (tmpfs_dentry_t *e = dir->data.entries; e; e = e->next) {
        if (tmpfs_strcmp(e->name, name) == 0) {
            return e;
        }
    }
    
    return NULL;
}

/**
 * Count directory entries
 */
static int tmpfs_dir_count(tmpfs_inode_t *dir) {
    if (!dir || dir->type != TMPFS_TYPE_DIR) return 0;
    
    int count = 0;
    for (tmpfs_dentry_t *e = dir->data.entries; e; e = e->next) {
        count++;
    }
    return count;
}

/* ================================================================
 * FILE OPERATIONS
 * ================================================================ */

/**
 * Read from file
 */
static int tmpfs_file_read(tmpfs_inode_t *inode, uint32_t offset, 
                           void *buffer, uint32_t size) {
    if (!inode || inode->type != TMPFS_TYPE_FILE || !buffer) {
        return -1;
    }
    
    if (offset >= inode->size) {
        return 0;  /* EOF */
    }
    
    uint32_t to_read = size;
    if (offset + to_read > inode->size) {
        to_read = inode->size - offset;
    }
    
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t bytes_read = 0;
    
    /* Find starting block */
    uint32_t block_num = offset / TMPFS_BLOCK_SIZE;
    uint32_t block_offset = offset % TMPFS_BLOCK_SIZE;
    
    tmpfs_block_t *block = inode->data.blocks;
    for (uint32_t i = 0; i < block_num && block; i++) {
        block = block->next;
    }
    
    /* Read data */
    while (bytes_read < to_read && block) {
        uint32_t chunk = TMPFS_BLOCK_SIZE - block_offset;
        if (chunk > to_read - bytes_read) {
            chunk = to_read - bytes_read;
        }
        
        if (block->data) {
            tmpfs_memcpy(dst + bytes_read, (uint8_t *)block->data + block_offset, chunk);
        } else {
            tmpfs_memset(dst + bytes_read, 0, chunk);
        }
        
        bytes_read += chunk;
        block_offset = 0;
        block = block->next;
    }
    
    inode->atime = tmpfs_get_time();
    return bytes_read;
}

/**
 * Write to file
 */
static int tmpfs_file_write(tmpfs_instance_t *fs, tmpfs_inode_t *inode, 
                            uint32_t offset, const void *buffer, uint32_t size) {
    if (!inode || inode->type != TMPFS_TYPE_FILE || !buffer) {
        return -1;
    }
    
    /* Check size limit */
    if (offset + size > TMPFS_MAX_FILE_SIZE) {
        return -1;
    }
    
    /* Check filesystem limit */
    uint32_t new_blocks_needed = ((offset + size + TMPFS_BLOCK_SIZE - 1) / TMPFS_BLOCK_SIZE);
    uint32_t current_blocks = (inode->size + TMPFS_BLOCK_SIZE - 1) / TMPFS_BLOCK_SIZE;
    
    if (new_blocks_needed > current_blocks) {
        uint32_t extra_size = (new_blocks_needed - current_blocks) * TMPFS_BLOCK_SIZE;
        if (fs->used_size + extra_size > fs->max_size) {
            return -1;  /* No space */
        }
    }
    
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t bytes_written = 0;
    
    /* Find or create starting block */
    uint32_t block_num = offset / TMPFS_BLOCK_SIZE;
    uint32_t block_offset = offset % TMPFS_BLOCK_SIZE;
    
    /* Ensure we have enough blocks */
    tmpfs_block_t **block_ptr = &inode->data.blocks;
    for (uint32_t i = 0; i <= block_num; i++) {
        if (!*block_ptr) {
            *block_ptr = (tmpfs_block_t *)kmalloc(sizeof(tmpfs_block_t));
            if (!*block_ptr) return bytes_written;
            (*block_ptr)->data = NULL;
            (*block_ptr)->next = NULL;
        }
        if (i < block_num) {
            block_ptr = &(*block_ptr)->next;
        }
    }
    
    tmpfs_block_t *block = *block_ptr;
    
    /* Write data */
    while (bytes_written < size) {
        /* Allocate block data if needed */
        if (!block->data) {
            block->data = kmalloc(TMPFS_BLOCK_SIZE);
            if (!block->data) break;
            tmpfs_memset(block->data, 0, TMPFS_BLOCK_SIZE);
            fs->used_size += TMPFS_BLOCK_SIZE;
        }
        
        uint32_t chunk = TMPFS_BLOCK_SIZE - block_offset;
        if (chunk > size - bytes_written) {
            chunk = size - bytes_written;
        }
        
        tmpfs_memcpy((uint8_t *)block->data + block_offset, src + bytes_written, chunk);
        
        bytes_written += chunk;
        block_offset = 0;
        
        /* Move to next block */
        if (bytes_written < size) {
            if (!block->next) {
                block->next = (tmpfs_block_t *)kmalloc(sizeof(tmpfs_block_t));
                if (!block->next) break;
                block->next->data = NULL;
                block->next->next = NULL;
            }
            block = block->next;
        }
    }
    
    /* Update size */
    if (offset + bytes_written > inode->size) {
        inode->size = offset + bytes_written;
    }
    
    inode->mtime = tmpfs_get_time();
    return bytes_written;
}

/**
 * Truncate file
 */
static int tmpfs_file_truncate(tmpfs_instance_t *fs, tmpfs_inode_t *inode, uint32_t size) {
    if (!inode || inode->type != TMPFS_TYPE_FILE) {
        return -1;
    }
    
    if (size > TMPFS_MAX_FILE_SIZE) {
        return -1;
    }
    
    /* Free blocks beyond new size */
    uint32_t new_blocks = (size + TMPFS_BLOCK_SIZE - 1) / TMPFS_BLOCK_SIZE;
    uint32_t block_num = 0;
    
    tmpfs_block_t *block = inode->data.blocks;
    tmpfs_block_t *prev = NULL;
    
    while (block) {
        if (block_num >= new_blocks) {
            /* Free this block */
            if (prev) {
                prev->next = NULL;
            } else {
                inode->data.blocks = NULL;
            }
            
            while (block) {
                tmpfs_block_t *next = block->next;
                if (block->data) {
                    kfree(block->data);
                    fs->used_size -= TMPFS_BLOCK_SIZE;
                }
                kfree(block);
                block = next;
            }
            break;
        }
        
        block_num++;
        prev = block;
        block = block->next;
    }
    
    inode->size = size;
    inode->mtime = tmpfs_get_time();
    return 0;
}

/* ================================================================
 * PATH RESOLUTION
 * ================================================================ */

/**
 * Find instance by mountpoint
 */
static tmpfs_instance_t *tmpfs_find_instance(const char *mountpoint) {
    for (tmpfs_instance_t *inst = tmpfs_instances; inst; inst = inst->next) {
        if (tmpfs_strcmp(inst->mountpoint, mountpoint) == 0) {
            return inst;
        }
    }
    return NULL;
}

/**
 * Resolve path to inode
 */
static tmpfs_inode_t *tmpfs_resolve_path(tmpfs_instance_t *fs, const char *path,
                                          tmpfs_inode_t **parent_out, char *name_out) {
    if (!fs || !path) return NULL;
    
    /* Skip mountpoint prefix */
    int mp_len = tmpfs_strlen(fs->mountpoint);
    if (tmpfs_strcmp(path, fs->mountpoint) == 0) {
        if (parent_out) *parent_out = NULL;
        if (name_out) name_out[0] = '\0';
        return fs->root;
    }
    
    const char *rel_path = path;
    if (path[0] == '/' && mp_len > 1) {
        int match = 1;
        for (int i = 0; i < mp_len && path[i]; i++) {
            if (path[i] != fs->mountpoint[i]) {
                match = 0;
                break;
            }
        }
        if (match) {
            rel_path = path + mp_len;
            if (*rel_path == '/') rel_path++;
        }
    }
    
    if (rel_path[0] == '/') rel_path++;
    
    if (*rel_path == '\0') {
        if (parent_out) *parent_out = NULL;
        if (name_out) name_out[0] = '\0';
        return fs->root;
    }
    
    tmpfs_inode_t *current = fs->root;
    tmpfs_inode_t *parent = NULL;
    char component[TMPFS_MAX_NAME];
    int ci = 0;
    
    while (*rel_path) {
        if (*rel_path == '/') {
            if (ci > 0) {
                component[ci] = '\0';
                tmpfs_dentry_t *entry = tmpfs_dir_find_entry(current, component);
                if (!entry) {
                    if (parent_out) *parent_out = current;
                    if (name_out) tmpfs_strcpy(name_out, component);
                    return NULL;
                }
                parent = current;
                current = entry->inode;
                ci = 0;
            }
            rel_path++;
        } else {
            if (ci < TMPFS_MAX_NAME - 1) {
                component[ci++] = *rel_path;
            }
            rel_path++;
        }
    }
    
    if (ci > 0) {
        component[ci] = '\0';
        tmpfs_dentry_t *entry = tmpfs_dir_find_entry(current, component);
        if (!entry) {
            if (parent_out) *parent_out = current;
            if (name_out) tmpfs_strcpy(name_out, component);
            return NULL;
        }
        if (parent_out) *parent_out = current;
        if (name_out) tmpfs_strcpy(name_out, component);
        return entry->inode;
    }
    
    if (parent_out) *parent_out = parent;
    return current;
}

/* ================================================================
 * TMPFS VFS OPERATIONS
 * ================================================================ */

static int tmpfs_vfs_mount(const char *device, const char *mountpoint) {
    (void)device;
    
    /* Create new instance */
    tmpfs_instance_t *inst = (tmpfs_instance_t *)kmalloc(sizeof(tmpfs_instance_t));
    if (!inst) return -1;
    
    tmpfs_strcpy(inst->mountpoint, mountpoint);
    inst->max_size = TMPFS_DEFAULT_MAX_SIZE;
    inst->used_size = 0;
    inst->next_ino = 2;  /* 1 is reserved for root */
    inst->inodes = NULL;
    
    /* Create root inode */
    inst->root = tmpfs_create_inode(inst, TMPFS_TYPE_DIR, 01777);
    if (!inst->root) {
        kfree(inst);
        return -1;
    }
    inst->root->ino = 1;
    
    /* Add . and .. entries */
    tmpfs_dir_add_entry(inst->root, ".", inst->root);
    tmpfs_dir_add_entry(inst->root, "..", inst->root);
    
    /* Add to instances list */
    inst->next = tmpfs_instances;
    tmpfs_instances = inst;
    
    serial_printf("[TMPFS] Mounted at %s (max %d KB)\n", mountpoint, 
                  inst->max_size / 1024);
    return 0;
}

static int tmpfs_vfs_unmount(const char *mountpoint) {
    tmpfs_instance_t **prev = &tmpfs_instances;
    tmpfs_instance_t *inst = tmpfs_instances;
    
    while (inst) {
        if (tmpfs_strcmp(inst->mountpoint, mountpoint) == 0) {
            *prev = inst->next;
            
            /* Free all inodes */
            while (inst->inodes) {
                tmpfs_free_inode(inst, inst->inodes);
            }
            
            kfree(inst);
            serial_printf("[TMPFS] Unmounted from %s\n", mountpoint);
            return 0;
        }
        prev = &inst->next;
        inst = inst->next;
    }
    
    return -1;
}

static int tmpfs_vfs_open(vfs_node_t *node, uint32_t mode) {
    (void)node;
    (void)mode;
    return 0;
}

static int tmpfs_vfs_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int tmpfs_vfs_read(vfs_node_t *node, uint32_t offset, 
                          uint32_t size, void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    tmpfs_inode_t *inode = (tmpfs_inode_t *)node->fs_specific;
    return tmpfs_file_read(inode, offset, buffer, size);
}

static int tmpfs_vfs_write(vfs_node_t *node, uint32_t offset, 
                           uint32_t size, const void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    /* Need to find the instance - for now use first one */
    tmpfs_instance_t *inst = tmpfs_instances;
    tmpfs_inode_t *inode = (tmpfs_inode_t *)node->fs_specific;
    
    return tmpfs_file_write(inst, inode, offset, buffer, size);
}

static int tmpfs_vfs_readdir(vfs_node_t *node, uint32_t index, 
                             vfs_dirent_t *dirent) {
    if (!node || !node->fs_specific || !dirent) {
        return -1;
    }
    
    tmpfs_inode_t *dir = (tmpfs_inode_t *)node->fs_specific;
    
    if (dir->type != TMPFS_TYPE_DIR) {
        return -1;
    }
    
    /* Find entry at index */
    tmpfs_dentry_t *entry = dir->data.entries;
    uint32_t i = 0;
    
    while (entry && i < index) {
        entry = entry->next;
        i++;
    }
    
    if (!entry) {
        return -1;
    }
    
    tmpfs_strcpy(dirent->name, entry->name);
    dirent->inode = entry->inode->ino;
    dirent->type = (entry->inode->type == TMPFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    
    return 0;
}

static int tmpfs_vfs_finddir(vfs_node_t *node, const char *name, 
                             vfs_node_t *result) {
    if (!node || !node->fs_specific || !name || !result) {
        return -1;
    }
    
    tmpfs_inode_t *dir = (tmpfs_inode_t *)node->fs_specific;
    tmpfs_dentry_t *entry = tmpfs_dir_find_entry(dir, name);
    
    if (!entry) {
        return -1;
    }
    
    tmpfs_strcpy(result->name, entry->name);
    result->inode = entry->inode->ino;
    result->type = (entry->inode->type == TMPFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    result->permissions = entry->inode->mode;
    result->size = entry->inode->size;
    result->uid = entry->inode->uid;
    result->gid = entry->inode->gid;
    result->atime = entry->inode->atime;
    result->mtime = entry->inode->mtime;
    result->ctime = entry->inode->ctime;
    result->fs_specific = entry->inode;
    
    return 0;
}

static int tmpfs_vfs_create(vfs_node_t *parent, const char *name,
                            uint32_t type, uint32_t permissions) {
    if (!parent || !parent->fs_specific || !name) {
        return -1;
    }
    
    tmpfs_inode_t *dir = (tmpfs_inode_t *)parent->fs_specific;
    
    if (dir->type != TMPFS_TYPE_DIR) {
        return -1;
    }
    
    /* Check if entry already exists */
    if (tmpfs_dir_find_entry(dir, name)) {
        return -1;
    }
    
    /* Find instance */
    tmpfs_instance_t *inst = tmpfs_instances;
    
    /* Create inode */
    uint8_t inode_type = (type == VFS_TYPE_DIR) ? TMPFS_TYPE_DIR : TMPFS_TYPE_FILE;
    tmpfs_inode_t *inode = tmpfs_create_inode(inst, inode_type, permissions);
    if (!inode) {
        return -1;
    }
    
    /* Initialize directory */
    if (inode_type == TMPFS_TYPE_DIR) {
        tmpfs_dir_add_entry(inode, ".", inode);
        tmpfs_dir_add_entry(inode, "..", dir);
    }
    
    /* Add to parent */
    return tmpfs_dir_add_entry(dir, name, inode);
}

static int tmpfs_vfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->fs_specific || !name) {
        return -1;
    }
    
    tmpfs_inode_t *dir = (tmpfs_inode_t *)parent->fs_specific;
    tmpfs_dentry_t *entry = tmpfs_dir_find_entry(dir, name);
    
    if (!entry) {
        return -1;
    }
    
    if (entry->inode->type == TMPFS_TYPE_DIR) {
        return -1;  /* Use rmdir for directories */
    }
    
    tmpfs_instance_t *inst = tmpfs_instances;
    
    tmpfs_dir_remove_entry(dir, name);
    
    if (entry->inode->nlink == 0) {
        tmpfs_free_inode(inst, entry->inode);
    }
    
    return 0;
}

static int tmpfs_vfs_mkdir(vfs_node_t *parent, const char *name, 
                           uint32_t permissions) {
    return tmpfs_vfs_create(parent, name, VFS_TYPE_DIR, permissions);
}

static int tmpfs_vfs_rmdir(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->fs_specific || !name) {
        return -1;
    }
    
    tmpfs_inode_t *dir = (tmpfs_inode_t *)parent->fs_specific;
    tmpfs_dentry_t *entry = tmpfs_dir_find_entry(dir, name);
    
    if (!entry || entry->inode->type != TMPFS_TYPE_DIR) {
        return -1;
    }
    
    /* Check if directory is empty (only . and ..) */
    if (tmpfs_dir_count(entry->inode) > 2) {
        return -1;  /* Not empty */
    }
    
    tmpfs_instance_t *inst = tmpfs_instances;
    
    tmpfs_dir_remove_entry(dir, name);
    
    if (entry->inode->nlink == 0) {
        tmpfs_free_inode(inst, entry->inode);
    }
    
    return 0;
}

/* VFS operations structure */
static vfs_fs_ops_t tmpfs_vfs_ops = {
    .mount = tmpfs_vfs_mount,
    .unmount = tmpfs_vfs_unmount,
    .open = tmpfs_vfs_open,
    .close = tmpfs_vfs_close,
    .read = tmpfs_vfs_read,
    .write = tmpfs_vfs_write,
    .readdir = tmpfs_vfs_readdir,
    .finddir = tmpfs_vfs_finddir,
    .create = tmpfs_vfs_create,
    .unlink = tmpfs_vfs_unlink,
    .mkdir = tmpfs_vfs_mkdir,
    .rmdir = tmpfs_vfs_rmdir
};

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/**
 * Initialize tmpfs
 */
int tmpfs_init(void) {
    if (tmpfs_initialized) {
        return 0;
    }
    
    serial_printf("[TMPFS] Initializing temporary filesystem...\n");
    
    /* Register with VFS */
    if (vfs_register_fs("tmpfs", &tmpfs_vfs_ops) != 0) {
        serial_printf("[TMPFS] Failed to register with VFS\n");
        return -1;
    }
    
    serial_printf("[TMPFS] Registered with VFS\n");
    
    tmpfs_initialized = 1;
    return 0;
}

/**
 * Get tmpfs statistics
 */
int tmpfs_get_stats(const char *mountpoint, uint32_t *used, uint32_t *max) {
    tmpfs_instance_t *inst = tmpfs_find_instance(mountpoint);
    if (!inst) return -1;
    
    if (used) *used = inst->used_size;
    if (max) *max = inst->max_size;
    
    return 0;
}

/**
 * Create file in tmpfs
 */
int tmpfs_create_file(const char *path, const void *data, uint32_t size) {
    /* Find instance from path */
    tmpfs_instance_t *inst = tmpfs_instances;
    if (!inst) return -1;
    
    tmpfs_inode_t *parent;
    char name[TMPFS_MAX_NAME];
    
    tmpfs_inode_t *existing = tmpfs_resolve_path(inst, path, &parent, name);
    
    if (existing) {
        /* File exists - overwrite */
        if (existing->type != TMPFS_TYPE_FILE) return -1;
        tmpfs_file_truncate(inst, existing, 0);
        return tmpfs_file_write(inst, existing, 0, data, size);
    }
    
    if (!parent || parent->type != TMPFS_TYPE_DIR) {
        return -1;
    }
    
    /* Create new file */
    tmpfs_inode_t *inode = tmpfs_create_inode(inst, TMPFS_TYPE_FILE, 0644);
    if (!inode) return -1;
    
    tmpfs_dir_add_entry(parent, name, inode);
    
    if (data && size > 0) {
        return tmpfs_file_write(inst, inode, 0, data, size);
    }
    
    return 0;
}

/**
 * Read file from tmpfs
 */
int tmpfs_read_file(const char *path, void *buffer, uint32_t size, uint32_t offset) {
    tmpfs_instance_t *inst = tmpfs_instances;
    if (!inst) return -1;
    
    tmpfs_inode_t *inode = tmpfs_resolve_path(inst, path, NULL, NULL);
    if (!inode || inode->type != TMPFS_TYPE_FILE) {
        return -1;
    }
    
    return tmpfs_file_read(inode, offset, buffer, size);
}
