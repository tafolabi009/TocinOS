/**
 * TocinOS Virtual File System (VFS) Layer
 * 
 * Provides abstraction layer for multiple filesystem types (FAT, ext2/3/4, etc.)
 * Supports mounting, unmounting, and standard file operations
 */

#ifndef VFS_H
#define VFS_H

#include "../stdint.h"

// Maximum path length
#define VFS_MAX_PATH        256
#define VFS_MAX_NAME        64
#define VFS_MAX_MOUNTS      16

// File types
#define VFS_TYPE_FILE       0x01
#define VFS_TYPE_DIR        0x02
#define VFS_TYPE_CHARDEV    0x03
#define VFS_TYPE_BLOCKDEV   0x04
#define VFS_TYPE_SYMLINK    0x05
#define VFS_TYPE_SOCKET     0x06

// File permissions (Unix-style)
#define VFS_PERM_READ       0x01
#define VFS_PERM_WRITE      0x02
#define VFS_PERM_EXEC       0x04

// File open modes
#define VFS_O_RDONLY        0x00
#define VFS_O_WRONLY        0x01
#define VFS_O_RDWR          0x02
#define VFS_O_CREAT         0x04
#define VFS_O_TRUNC         0x08
#define VFS_O_APPEND        0x10

// Seek modes
#define VFS_SEEK_SET        0
#define VFS_SEEK_CUR        1
#define VFS_SEEK_END        2

// VFS node (inode-like structure)
typedef struct vfs_node {
    char name[VFS_MAX_NAME];        // File/directory name
    uint32_t inode;                 // Inode number
    uint32_t type;                  // File type
    uint32_t permissions;           // Access permissions
    uint32_t uid;                   // User ID
    uint32_t gid;                   // Group ID
    uint32_t size;                  // File size in bytes
    uint32_t atime;                 // Last access time
    uint32_t mtime;                 // Last modification time
    uint32_t ctime;                 // Creation time
    void *fs_specific;              // Filesystem-specific data
    struct vfs_node *parent;        // Parent directory
} vfs_node_t;

// Directory entry
typedef struct vfs_dirent {
    uint32_t inode;                 // Inode number
    char name[VFS_MAX_NAME];        // Entry name
    uint32_t type;                  // Entry type
} vfs_dirent_t;

// File descriptor
typedef struct vfs_fd {
    vfs_node_t *node;               // VFS node
    uint32_t position;              // Current position in file
    uint32_t mode;                  // Open mode
    uint32_t flags;                 // File flags
} vfs_fd_t;

// Filesystem operations
typedef struct vfs_fs_ops {
    int (*mount)(const char *device, const char *mountpoint);
    int (*unmount)(const char *mountpoint);
    int (*open)(vfs_node_t *node, uint32_t mode);
    int (*close)(vfs_node_t *node);
    int (*read)(vfs_node_t *node, uint32_t offset, uint32_t size, void *buffer);
    int (*write)(vfs_node_t *node, uint32_t offset, uint32_t size, const void *buffer);
    int (*readdir)(vfs_node_t *node, uint32_t index, vfs_dirent_t *dirent);
    int (*finddir)(vfs_node_t *node, const char *name, vfs_node_t *result);
    int (*create)(vfs_node_t *parent, const char *name, uint32_t type, uint32_t permissions);
    int (*unlink)(vfs_node_t *parent, const char *name);
    int (*mkdir)(vfs_node_t *parent, const char *name, uint32_t permissions);
    int (*rmdir)(vfs_node_t *parent, const char *name);
} vfs_fs_ops_t;

// Filesystem type
typedef struct vfs_filesystem {
    char name[32];                  // Filesystem type name (fat, ext2, etc.)
    vfs_fs_ops_t *ops;              // Filesystem operations
    void *fs_data;                  // Filesystem-specific data
} vfs_filesystem_t;

// Mount point
typedef struct vfs_mount {
    char mountpoint[VFS_MAX_PATH];  // Mount point path
    char device[VFS_MAX_PATH];      // Device path
    vfs_filesystem_t *fs;           // Filesystem
    vfs_node_t *root;               // Root node of mounted filesystem
    uint32_t flags;                 // Mount flags
} vfs_mount_t;

// VFS API
int vfs_init(void);
int vfs_register_fs(const char *name, vfs_fs_ops_t *ops);
int vfs_mount(const char *device, const char *mountpoint, const char *fstype);
int vfs_unmount(const char *mountpoint);

// File operations
int vfs_open(const char *path, uint32_t mode);
int vfs_close(int fd);
int vfs_read(int fd, void *buffer, uint32_t size);
int vfs_write(int fd, const void *buffer, uint32_t size);
int vfs_seek(int fd, int32_t offset, uint32_t whence);
int vfs_stat(const char *path, vfs_node_t *stat);

// Directory operations
int vfs_readdir(int fd, vfs_dirent_t *dirent);
int vfs_opendir(const char *path);
int vfs_mkdir(const char *path, uint32_t permissions);
int vfs_rmdir(const char *path);

// File management
int vfs_create(const char *path, uint32_t mode);
int vfs_unlink(const char *path);

// Path resolution
vfs_node_t *vfs_resolve_path(const char *path);
vfs_mount_t *vfs_find_mount(const char *path);

#endif // VFS_H
