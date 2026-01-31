/**
 * TocinOS Virtual File System (VFS) Implementation
 * 
 * Core VFS layer providing filesystem abstraction
 */

#include "../include/kernel/vfs.h"
#include "../include/kernel/fat.h"
#include "../include/kernel/memory.h"

// VFS state
static int vfs_initialized = 0;
static vfs_filesystem_t registered_fs[16];
static int num_registered_fs = 0;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int num_mounts = 0;

// File descriptor table - stores FAT file handles
#define VFS_MAX_FDS 256
static vfs_fd_t fd_table[VFS_MAX_FDS];
static fat_file_t fat_files[VFS_MAX_FDS];  // FAT file handles for each fd
static int fd_bitmap[VFS_MAX_FDS / 32];

// Standard file descriptors (0=stdin, 1=stdout, 2=stderr)
#define STDIN_FD  0
#define STDOUT_FD 1
#define STDERR_FD 2

/**
 * Simple string copy
 */
static void vfs_strcpy(char *dest, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

/**
 * Simple string compare
 */
static int vfs_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

/**
 * Simple string length
 */
static int vfs_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

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
        fat_files[i].is_open = 0;
    }
    
    for (int i = 0; i < VFS_MAX_FDS / 32; i++) {
        fd_bitmap[i] = 0;
    }
    
    // Reserve stdin, stdout, stderr
    fd_bitmap[0] |= 0x7;  // Mark fds 0, 1, 2 as used
    
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
    
    // Skip leading mount point (assume root for now)
    const char *file_path = path;
    if (path[0] == '/') {
        file_path = path + 1;
    }
    
    // Allocate file descriptor
    int fd = vfs_alloc_fd();
    if (fd < 0) {
        return -1;
    }
    
    // Check if FAT is mounted
    if (!fat_is_mounted()) {
        vfs_free_fd(fd);
        return -1;
    }
    
    // Open file via FAT
    if (fat_open(file_path, &fat_files[fd]) != 0) {
        vfs_free_fd(fd);
        return -1;
    }
    
    // Initialize file descriptor
    fd_table[fd].node = (vfs_node_t *)1;  // Mark as in use (non-null)
    fd_table[fd].position = 0;
    fd_table[fd].mode = mode;
    fd_table[fd].flags = 0;
    
    return fd;
}

/**
 * Close a file
 */
int vfs_close(int fd) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS) {
        return -1;
    }
    
    // Don't close stdin/stdout/stderr
    if (fd <= STDERR_FD) {
        return -1;
    }
    
    if (!fd_table[fd].node) {
        return -1;
    }
    
    // Close FAT file handle
    if (fat_files[fd].is_open) {
        fat_close(&fat_files[fd]);
    }
    
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
    
    // Handle stdin (keyboard input)
    if (fd == STDIN_FD) {
        extern char keyboard_getchar(void);
        extern int keyboard_has_data(void);
        char *buf = (char *)buffer;
        uint32_t i = 0;
        
        while (i < size) {
            // Wait for keyboard input
            while (!keyboard_has_data()) {
                __asm__ volatile("hlt");  // Wait for interrupt
            }
            
            char c = keyboard_getchar();
            
            // Echo character to screen
            extern void kernel_print_char(char c);
            if (c == '\b') {
                // Handle backspace
                if (i > 0) {
                    i--;
                    kernel_print_char('\b');
                    kernel_print_char(' ');
                    kernel_print_char('\b');
                }
                continue;
            }
            
            kernel_print_char(c);
            buf[i++] = c;
            
            // Return on newline
            if (c == '\n') {
                break;
            }
        }
        
        return i;
    }
    
    // Regular file
    if (!fd_table[fd].node || !fat_files[fd].is_open) {
        return -1;
    }
    
    // Read from FAT file
    int bytes_read = fat_read(&fat_files[fd], buffer, size);
    if (bytes_read > 0) {
        fd_table[fd].position += bytes_read;
    }
    
    return bytes_read;
}

/**
 * Write to a file
 */
int vfs_write(int fd, const void *buffer, uint32_t size) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS || !buffer) {
        return -1;
    }
    
    // Handle stdout/stderr (screen output)
    if (fd == STDOUT_FD || fd == STDERR_FD) {
        // Write to console and serial
        extern void kernel_print(const char *str);
        extern void serial_write(uint16_t port, const char *data);
        const char *str = (const char *)buffer;
        for (uint32_t i = 0; i < size && str[i]; i++) {
            char c[2] = {str[i], 0};
            kernel_print(c);
            serial_write(0x3F8, c);  // Also output to serial (COM1)
        }
        return size;
    }
    
    // Regular file
    if (!fd_table[fd].node || !fat_files[fd].is_open) {
        return -1;
    }
    
    // Write to FAT file
    int bytes_written = fat_write(&fat_files[fd], buffer, size);
    if (bytes_written > 0) {
        fd_table[fd].position += bytes_written;
    }
    
    return bytes_written;
}

/**
 * Seek within a file
 */
int vfs_seek(int fd, int32_t offset, uint32_t whence) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_FDS) {
        return -1;
    }
    
    // Can't seek stdin/stdout/stderr
    if (fd <= STDERR_FD) {
        return -1;
    }
    
    if (!fd_table[fd].node || !fat_files[fd].is_open) {
        return -1;
    }
    
    uint32_t new_pos = fd_table[fd].position;
    uint32_t file_size = fat_files[fd].size;
    
    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos += offset;
            break;
        case VFS_SEEK_END:
            new_pos = file_size + offset;
            break;
        default:
            return -1;
    }
    
    // Check bounds
    if (new_pos > file_size) {
        return -1;
    }
    
    // Update FAT file position
    fat_seek(&fat_files[fd], new_pos);
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
