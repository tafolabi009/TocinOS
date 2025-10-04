/**
 * TocinOS Virtual File System (VFS) Implementation
 * 
 * Core VFS layer providing filesystem abstraction
 */

#include "../include/kernel/vfs.h"
#include "../include/kernel/memory.h"

// VFS state
static int vfs_initialized = 0;
static vfs_filesystem_t registered_fs[16];
static int num_registered_fs = 0;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int num_mounts = 0;

// File descriptor table
#define VFS_MAX_FDS 256
static vfs_fd_t fd_table[VFS_MAX_FDS];
static int fd_bitmap[VFS_MAX_FDS / 32];

/**
 * Initialize VFS
 */
int vfs_init(void) {
    if (vfs_initialized) {
        return 0;
    }
    
    // Clear filesystem table
    for (int i = 0; i < 16; i++) {
        registered_fs[i].name[0] = 0;
        registered_fs[i].ops = 0;
        registered_fs[i].fs_data = 0;
    }
    
    // Clear mount table
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].mountpoint[0] = 0;
        mounts[i].device[0] = 0;
        mounts[i].fs = 0;
        mounts[i].root = 0;
        mounts[i].flags = 0;
    }
    
    // Clear file descriptor table
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        fd_table[i].node = 0;
        fd_table[i].position = 0;
        fd_table[i].mode = 0;
        fd_table[i].flags = 0;
    }
    
    for (int i = 0; i < VFS_MAX_FDS / 32; i++) {
        fd_bitmap[i] = 0;
    }
    
    vfs_initialized = 1;
    return 0;
}

/**
 * Register a filesystem type
 */
int vfs_register_fs(const char *name, vfs_fs_ops_t *ops) {
    if (!vfs_initialized || !name || !ops) {
        return -1;
    }
    
    if (num_registered_fs >= 16) {
        return -1;
    }
    
    // Copy name
    int i = 0;
    while (name[i] && i < 31) {
        registered_fs[num_registered_fs].name[i] = name[i];
        i++;
    }
    registered_fs[num_registered_fs].name[i] = 0;
    
    registered_fs[num_registered_fs].ops = ops;
    registered_fs[num_registered_fs].fs_data = 0;
    num_registered_fs++;
    
    return 0;
}

/**
 * Mount a filesystem
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype) {
    if (!vfs_initialized || !device || !mountpoint || !fstype) {
        return -1;
    }
    
    if (num_mounts >= VFS_MAX_MOUNTS) {
        return -1;
    }
    
    // Find filesystem type
    vfs_filesystem_t *fs = 0;
    for (int i = 0; i < num_registered_fs; i++) {
        int match = 1;
        for (int j = 0; registered_fs[i].name[j] && fstype[j]; j++) {
            if (registered_fs[i].name[j] != fstype[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            fs = &registered_fs[i];
            break;
        }
    }
    
    if (!fs) {
        return -1; // Filesystem type not registered
    }
    
    // Call filesystem mount operation
    if (fs->ops->mount) {
        int result = fs->ops->mount(device, mountpoint);
        if (result != 0) {
            return result;
        }
    }
    
    // Add to mount table
    int i = 0;
    while (device[i] && i < VFS_MAX_PATH - 1) {
        mounts[num_mounts].device[i] = device[i];
        i++;
    }
    mounts[num_mounts].device[i] = 0;
    
    i = 0;
    while (mountpoint[i] && i < VFS_MAX_PATH - 1) {
        mounts[num_mounts].mountpoint[i] = mountpoint[i];
        i++;
    }
    mounts[num_mounts].mountpoint[i] = 0;
    
    mounts[num_mounts].fs = fs;
    mounts[num_mounts].root = 0; // TODO: Set root node
    mounts[num_mounts].flags = 0;
    num_mounts++;
    
    return 0;
}

/**
 * Unmount a filesystem
 */
int vfs_unmount(const char *mountpoint) {
    if (!vfs_initialized || !mountpoint) {
        return -1;
    }
    
    // Find mount point
    for (int i = 0; i < num_mounts; i++) {
        int match = 1;
        for (int j = 0; mounts[i].mountpoint[j] && mountpoint[j]; j++) {
            if (mounts[i].mountpoint[j] != mountpoint[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            // Call filesystem unmount operation
            if (mounts[i].fs->ops->unmount) {
                mounts[i].fs->ops->unmount(mountpoint);
            }
            
            // Remove from mount table
            for (int j = i; j < num_mounts - 1; j++) {
                mounts[j] = mounts[j + 1];
            }
            num_mounts--;
            return 0;
        }
    }
    
    return -1;
}

/**
 * Allocate a file descriptor
 */
static int vfs_alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        int word = i / 32;
        int bit = i % 32;
        if (!(fd_bitmap[word] & (1 << bit))) {
            fd_bitmap[word] |= (1 << bit);
            return i;
        }
    }
    return -1;
}

/**
 * Free a file descriptor
 */
static void vfs_free_fd(int fd) {
    if (fd >= 0 && fd < VFS_MAX_FDS) {
        int word = fd / 32;
        int bit = fd % 32;
        fd_bitmap[word] &= ~(1 << bit);
    }
}

/**
 * Find mount point for a path
 */
vfs_mount_t *vfs_find_mount(const char *path) {
    if (!path) {
        return 0;
    }
    
    // Find longest matching mount point
    vfs_mount_t *best_match = 0;
    int best_length = 0;
    
    for (int i = 0; i < num_mounts; i++) {
        int length = 0;
        int match = 1;
        
        while (mounts[i].mountpoint[length] && path[length]) {
            if (mounts[i].mountpoint[length] != path[length]) {
                match = 0;
                break;
            }
            length++;
        }
        
        if (match && length > best_length) {
            best_match = &mounts[i];
            best_length = length;
        }
    }
    
    return best_match;
}

/**
 * Resolve a path to a VFS node
 */
vfs_node_t *vfs_resolve_path(const char *path) {
    if (!path || path[0] != '/') {
        return 0;
    }
    
    // Find mount point
    vfs_mount_t *mount = vfs_find_mount(path);
    if (!mount || !mount->root) {
        return 0;
    }
    
    // TODO: Traverse path components
    return mount->root;
}

/**
 * Open a file
 */
int vfs_open(const char *path, uint32_t mode) {
    if (!vfs_initialized || !path) {
        return -1;
    }
    
    // Resolve path
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        return -1;
    }
    
    // Allocate file descriptor
    int fd = vfs_alloc_fd();
    if (fd < 0) {
        return -1;
    }
    
    // Initialize file descriptor
    fd_table[fd].node = node;
    fd_table[fd].position = 0;
    fd_table[fd].mode = mode;
    fd_table[fd].flags = 0;
    
    // Call filesystem open operation
    vfs_mount_t *mount = vfs_find_mount(path);
    if (mount && mount->fs->ops->open) {
        int result = mount->fs->ops->open(node, mode);
        if (result != 0) {
            vfs_free_fd(fd);
            return -1;
        }
    }
    
    return fd;
}

/**
 * Close a file
 */
int vfs_close(int fd) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS) {
        return -1;
    }
    
    if (!fd_table[fd].node) {
        return -1;
    }
    
    // Call filesystem close operation (if exists)
    // TODO: Get mount point from node
    
    // Clear file descriptor
    fd_table[fd].node = 0;
    fd_table[fd].position = 0;
    fd_table[fd].mode = 0;
    fd_table[fd].flags = 0;
    
    vfs_free_fd(fd);
    return 0;
}

/**
 * Read from a file
 */
int vfs_read(int fd, void *buffer, uint32_t size) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS || !buffer) {
        return -1;
    }
    
    if (!fd_table[fd].node) {
        return -1;
    }
    
    vfs_node_t *node = fd_table[fd].node;
    uint32_t position = fd_table[fd].position;
    
    // TODO: Call filesystem read operation
    
    return 0;
}

/**
 * Write to a file
 */
int vfs_write(int fd, const void *buffer, uint32_t size) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS || !buffer) {
        return -1;
    }
    
    if (!fd_table[fd].node) {
        return -1;
    }
    
    vfs_node_t *node = fd_table[fd].node;
    uint32_t position = fd_table[fd].position;
    
    // TODO: Call filesystem write operation
    
    return 0;
}

/**
 * Seek within a file
 */
int vfs_seek(int fd, int32_t offset, uint32_t whence) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS) {
        return -1;
    }
    
    if (!fd_table[fd].node) {
        return -1;
    }
    
    vfs_node_t *node = fd_table[fd].node;
    uint32_t new_pos = fd_table[fd].position;
    
    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos += offset;
            break;
        case VFS_SEEK_END:
            new_pos = node->size + offset;
            break;
        default:
            return -1;
    }
    
    // Check bounds
    if (new_pos > node->size) {
        return -1;
    }
    
    fd_table[fd].position = new_pos;
    return new_pos;
}

/**
 * Get file status
 */
int vfs_stat(const char *path, vfs_node_t *stat) {
    if (!vfs_initialized || !path || !stat) {
        return -1;
    }
    
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        return -1;
    }
    
    // Copy node information
    *stat = *node;
    return 0;
}

/**
 * Create a new file
 */
int vfs_create(const char *path, uint32_t mode) {
    if (!vfs_initialized || !path) {
        return -1;
    }
    
    // TODO: Parse path, find parent, create file
    return -1;
}

/**
 * Delete a file
 */
int vfs_unlink(const char *path) {
    if (!vfs_initialized || !path) {
        return -1;
    }
    
    // TODO: Parse path, find parent, delete file
    return -1;
}

/**
 * Create a directory
 */
int vfs_mkdir(const char *path, uint32_t permissions) {
    if (!vfs_initialized || !path) {
        return -1;
    }
    
    // TODO: Parse path, find parent, create directory
    return -1;
}

/**
 * Remove a directory
 */
int vfs_rmdir(const char *path) {
    if (!vfs_initialized || !path) {
        return -1;
    }
    
    // TODO: Parse path, find parent, remove directory
    return -1;
}
