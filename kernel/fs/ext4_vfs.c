/**
 * TocinOS ext4 VFS Integration
 * 
 * Provides VFS filesystem operations for ext4 filesystems.
 * This bridges the VFS layer with the ext2.c implementation.
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/vfs.h"
#include "../../include/kernel/ext2.h"
#include "../../include/kernel/memory.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

extern void serial_printf(const char *fmt, ...);

/* ================================================================
 * EXT4 VFS STATE
 * ================================================================ */

/* Maximum number of ext4 mounts */
#define EXT4_MAX_MOUNTS 4

/* ext4 mount context */
typedef struct ext4_mount_ctx {
    ext2_fs_t *fs;                  /* ext4 filesystem handle */
    char mountpoint[256];           /* Mount point path */
    char device[64];                /* Device identifier */
    int active;                     /* Is mount active? */
} ext4_mount_ctx_t;

/* Global mount table */
static ext4_mount_ctx_t ext4_mounts[EXT4_MAX_MOUNTS];
static int ext4_vfs_initialized = 0;

/* ext4 file descriptor context */
typedef struct ext4_fd_ctx {
    ext2_fs_t *fs;                  /* Filesystem handle */
    ext2_inode_t inode;             /* File inode */
    uint32_t inode_num;             /* Inode number */
    uint32_t position;              /* Current file position */
    uint32_t flags;                 /* Open flags */
    int active;                     /* Is fd active? */
} ext4_fd_ctx_t;

/* Maximum file descriptors for ext4 */
#define EXT4_MAX_FDS 64
static ext4_fd_ctx_t ext4_fds[EXT4_MAX_FDS];

/* ================================================================
 * HELPER FUNCTIONS
 * ================================================================ */

/**
 * String compare
 */
static int ext4_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

/**
 * String copy
 */
static void ext4_strcpy(char *dest, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * Find ext4 mount by mountpoint
 */
static ext4_mount_ctx_t *ext4_find_mount(const char *mountpoint) {
    for (int i = 0; i < EXT4_MAX_MOUNTS; i++) {
        if (ext4_mounts[i].active && ext4_strcmp(ext4_mounts[i].mountpoint, mountpoint) == 0) {
            return &ext4_mounts[i];
        }
    }
    return NULL;
}

/**
 * Find ext4 mount by device
 */
static ext4_mount_ctx_t *ext4_find_mount_by_device(const char *device) {
    for (int i = 0; i < EXT4_MAX_MOUNTS; i++) {
        if (ext4_mounts[i].active && ext4_strcmp(ext4_mounts[i].device, device) == 0) {
            return &ext4_mounts[i];
        }
    }
    return NULL;
}

/**
 * Allocate ext4 file descriptor
 */
static int ext4_alloc_fd(void) {
    for (int i = 0; i < EXT4_MAX_FDS; i++) {
        if (!ext4_fds[i].active) {
            return i;
        }
    }
    return -1;
}

/* ================================================================
 * VFS OPERATIONS IMPLEMENTATION
 * ================================================================ */

/**
 * Mount ext4 filesystem
 */
static int ext4_vfs_mount(const char *device, const char *mountpoint) {
    if (!device || !mountpoint) {
        return -1;
    }
    
    serial_printf("[EXT4-VFS] Mount request: %s on %s\n", device, mountpoint);
    
    /* Find free mount slot */
    ext4_mount_ctx_t *ctx = NULL;
    for (int i = 0; i < EXT4_MAX_MOUNTS; i++) {
        if (!ext4_mounts[i].active) {
            ctx = &ext4_mounts[i];
            break;
        }
    }
    
    if (!ctx) {
        serial_printf("[EXT4-VFS] No free mount slots\n");
        return -1;
    }
    
    /* Initialize ext4 if needed */
    ext2_init();
    
    /* Mount the filesystem */
    ext2_fs_t *fs = NULL;
    if (ext2_mount(device, &fs) != 0) {
        serial_printf("[EXT4-VFS] Failed to mount ext4 filesystem\n");
        return -1;
    }
    
    /* Initialize journal if present */
    jbd2_init(fs);
    jbd2_recover(fs);
    
    /* Store context */
    ctx->fs = fs;
    ext4_strcpy(ctx->mountpoint, mountpoint, sizeof(ctx->mountpoint));
    ext4_strcpy(ctx->device, device, sizeof(ctx->device));
    ctx->active = 1;
    
    serial_printf("[EXT4-VFS] Mounted successfully\n");
    return 0;
}

/**
 * Unmount ext4 filesystem
 */
static int ext4_vfs_unmount(const char *mountpoint) {
    ext4_mount_ctx_t *ctx = ext4_find_mount(mountpoint);
    if (!ctx) {
        return -1;
    }
    
    /* Close all open files on this mount */
    for (int i = 0; i < EXT4_MAX_FDS; i++) {
        if (ext4_fds[i].active && ext4_fds[i].fs == ctx->fs) {
            ext4_fds[i].active = 0;
        }
    }
    
    /* Sync and unmount */
    ext2_sync(ctx->fs);
    ext2_unmount(ctx->fs);
    
    ctx->active = 0;
    ctx->fs = NULL;
    
    serial_printf("[EXT4-VFS] Unmounted %s\n", mountpoint);
    return 0;
}

/**
 * Open file on ext4 filesystem
 */
static int ext4_vfs_open(vfs_node_t *node, uint32_t mode) {
    /* This is called by VFS after resolving path */
    /* For now, we handle opens differently via ext4_open_file */
    (void)node;
    (void)mode;
    return 0;
}

/**
 * Close file
 */
static int ext4_vfs_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

/**
 * Read from file
 */
static int ext4_vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = (ext4_fd_ctx_t *)node->fs_specific;
    
    int bytes = ext2_read_file(fd->fs, &fd->inode, offset, size, buffer);
    return bytes;
}

/**
 * Write to file
 */
static int ext4_vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const void *buffer) {
    if (!node || !node->fs_specific || !buffer) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = (ext4_fd_ctx_t *)node->fs_specific;
    
    int bytes = ext2_write_file(fd->fs, &fd->inode, offset, size, buffer);
    
    /* Update inode on disk */
    if (bytes > 0) {
        ext2_write_inode(fd->fs, fd->inode_num, &fd->inode);
    }
    
    return bytes;
}

/**
 * Read directory entry
 */
static int ext4_vfs_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *dirent) {
    if (!node || !node->fs_specific || !dirent) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = (ext4_fd_ctx_t *)node->fs_specific;
    
    /* Use buffer for directory entry with name */
    static uint8_t entry_buf[sizeof(ext2_dir_entry_t) + 256];
    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)entry_buf;
    
    int result = ext2_read_dir(fd->fs, &fd->inode, index, entry);
    
    if (result == 0) {
        dirent->inode = entry->inode;
        
        /* Copy name */
        for (int i = 0; i < entry->name_len && i < VFS_MAX_NAME - 1; i++) {
            dirent->name[i] = entry->name[i];
        }
        dirent->name[entry->name_len] = '\0';
        
        /* Map file type */
        switch (entry->file_type) {
            case EXT2_FT_REG_FILE:
                dirent->type = VFS_TYPE_FILE;
                break;
            case EXT2_FT_DIR:
                dirent->type = VFS_TYPE_DIR;
                break;
            case EXT2_FT_SYMLINK:
                dirent->type = VFS_TYPE_SYMLINK;
                break;
            case EXT2_FT_CHRDEV:
                dirent->type = VFS_TYPE_CHARDEV;
                break;
            case EXT2_FT_BLKDEV:
                dirent->type = VFS_TYPE_BLOCKDEV;
                break;
            default:
                dirent->type = VFS_TYPE_FILE;
                break;
        }
    }
    
    return result;
}

/**
 * Find entry in directory
 */
static int ext4_vfs_finddir(vfs_node_t *node, const char *name, vfs_node_t *result) {
    if (!node || !node->fs_specific || !name || !result) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = (ext4_fd_ctx_t *)node->fs_specific;
    
    /* Use buffer for directory entry with name */
    static uint8_t entry_buf[sizeof(ext2_dir_entry_t) + 256];
    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)entry_buf;
    
    if (ext2_find_entry(fd->fs, &fd->inode, name, entry) != 0) {
        return -1;  /* Not found */
    }
    
    /* Read the inode */
    ext2_inode_t inode;
    if (ext2_read_inode(fd->fs, entry->inode, &inode) != 0) {
        return -1;
    }
    
    /* Fill result node */
    for (int i = 0; i < entry->name_len && i < VFS_MAX_NAME - 1; i++) {
        result->name[i] = entry->name[i];
    }
    result->name[entry->name_len] = '\0';
    
    result->inode = entry->inode;
    result->size = inode.i_size;
    result->uid = inode.i_uid;
    result->gid = inode.i_gid;
    result->atime = inode.i_atime;
    result->mtime = inode.i_mtime;
    result->ctime = inode.i_ctime;
    
    /* Map mode to type */
    switch (inode.i_mode & 0xF000) {
        case EXT2_S_IFREG:
            result->type = VFS_TYPE_FILE;
            break;
        case EXT2_S_IFDIR:
            result->type = VFS_TYPE_DIR;
            break;
        case EXT2_S_IFLNK:
            result->type = VFS_TYPE_SYMLINK;
            break;
        case EXT2_S_IFCHR:
            result->type = VFS_TYPE_CHARDEV;
            break;
        case EXT2_S_IFBLK:
            result->type = VFS_TYPE_BLOCKDEV;
            break;
        default:
            result->type = VFS_TYPE_FILE;
            break;
    }
    
    /* Map permissions */
    result->permissions = 0;
    if (inode.i_mode & 0x100) result->permissions |= VFS_PERM_READ;
    if (inode.i_mode & 0x080) result->permissions |= VFS_PERM_WRITE;
    if (inode.i_mode & 0x040) result->permissions |= VFS_PERM_EXEC;
    
    return 0;
}

/**
 * Create file or directory
 */
static int ext4_vfs_create(vfs_node_t *parent, const char *name, uint32_t type, uint32_t permissions) {
    if (!parent || !parent->fs_specific || !name) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = (ext4_fd_ctx_t *)parent->fs_specific;
    
    /* Map VFS type to ext2 mode */
    uint16_t mode = 0;
    switch (type) {
        case VFS_TYPE_FILE:
            mode = EXT2_S_IFREG;
            break;
        case VFS_TYPE_DIR:
            mode = EXT2_S_IFDIR;
            break;
        default:
            return -1;
    }
    
    /* Add permissions */
    if (permissions & VFS_PERM_READ) mode |= 0444;
    if (permissions & VFS_PERM_WRITE) mode |= 0222;
    if (permissions & VFS_PERM_EXEC) mode |= 0111;
    
    uint32_t new_inode_num;
    return ext2_create(fd->fs, fd->inode_num, name, mode, &new_inode_num);
}

/**
 * Delete file
 */
static int ext4_vfs_unlink(vfs_node_t *parent, const char *name) {
    /* TODO: Implement file deletion */
    (void)parent;
    (void)name;
    return -1;
}

/**
 * Create directory
 */
static int ext4_vfs_mkdir(vfs_node_t *parent, const char *name, uint32_t permissions) {
    return ext4_vfs_create(parent, name, VFS_TYPE_DIR, permissions);
}

/**
 * Remove directory
 */
static int ext4_vfs_rmdir(vfs_node_t *parent, const char *name) {
    /* TODO: Implement directory deletion */
    (void)parent;
    (void)name;
    return -1;
}

/* ================================================================
 * VFS OPERATIONS STRUCTURE
 * ================================================================ */

static vfs_fs_ops_t ext4_vfs_ops = {
    .mount = ext4_vfs_mount,
    .unmount = ext4_vfs_unmount,
    .open = ext4_vfs_open,
    .close = ext4_vfs_close,
    .read = ext4_vfs_read,
    .write = ext4_vfs_write,
    .readdir = ext4_vfs_readdir,
    .finddir = ext4_vfs_finddir,
    .create = ext4_vfs_create,
    .unlink = ext4_vfs_unlink,
    .mkdir = ext4_vfs_mkdir,
    .rmdir = ext4_vfs_rmdir
};

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/**
 * Initialize ext4 VFS integration
 */
int ext4_vfs_init(void) {
    if (ext4_vfs_initialized) {
        return 0;
    }
    
    /* Clear mount table */
    for (int i = 0; i < EXT4_MAX_MOUNTS; i++) {
        ext4_mounts[i].active = 0;
        ext4_mounts[i].fs = NULL;
    }
    
    /* Clear file descriptor table */
    for (int i = 0; i < EXT4_MAX_FDS; i++) {
        ext4_fds[i].active = 0;
    }
    
    /* Register with VFS */
    if (vfs_register_fs("ext4", &ext4_vfs_ops) != 0) {
        serial_printf("[EXT4-VFS] Failed to register with VFS\n");
        return -1;
    }
    
    /* Also register ext3 and ext2 as aliases */
    vfs_register_fs("ext3", &ext4_vfs_ops);
    vfs_register_fs("ext2", &ext4_vfs_ops);
    
    serial_printf("[EXT4-VFS] Registered ext2/ext3/ext4 filesystems\n");
    
    ext4_vfs_initialized = 1;
    return 0;
}

/**
 * Open a file on ext4 filesystem
 * 
 * @param mountpoint Mount point path
 * @param path Relative path within mount
 * @param mode Open mode
 * @return File descriptor, or negative on error
 */
int ext4_open_file(const char *mountpoint, const char *path, uint32_t mode) {
    ext4_mount_ctx_t *ctx = ext4_find_mount(mountpoint);
    if (!ctx) {
        serial_printf("[EXT4-VFS] Mount point not found: %s\n", mountpoint);
        return -1;
    }
    
    /* Resolve path to inode */
    ext2_inode_t inode;
    uint32_t inode_num;
    
    if (ext2_resolve_path(ctx->fs, path, &inode, &inode_num) != 0) {
        serial_printf("[EXT4-VFS] File not found: %s\n", path);
        return -1;
    }
    
    /* Allocate file descriptor */
    int fd_idx = ext4_alloc_fd();
    if (fd_idx < 0) {
        serial_printf("[EXT4-VFS] No free file descriptors\n");
        return -1;
    }
    
    ext4_fd_ctx_t *fd = &ext4_fds[fd_idx];
    fd->fs = ctx->fs;
    fd->inode = inode;
    fd->inode_num = inode_num;
    fd->position = 0;
    fd->flags = mode;
    fd->active = 1;
    
    return fd_idx;
}

/**
 * Read from ext4 file
 */
int ext4_read_fd(int fd_idx, void *buffer, uint32_t size) {
    if (fd_idx < 0 || fd_idx >= EXT4_MAX_FDS || !ext4_fds[fd_idx].active) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = &ext4_fds[fd_idx];
    
    int bytes = ext2_read_file(fd->fs, &fd->inode, fd->position, size, buffer);
    if (bytes > 0) {
        fd->position += bytes;
    }
    
    return bytes;
}

/**
 * Write to ext4 file
 */
int ext4_write_fd(int fd_idx, const void *buffer, uint32_t size) {
    if (fd_idx < 0 || fd_idx >= EXT4_MAX_FDS || !ext4_fds[fd_idx].active) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = &ext4_fds[fd_idx];
    
    int bytes = ext2_write_file(fd->fs, &fd->inode, fd->position, size, buffer);
    if (bytes > 0) {
        fd->position += bytes;
        ext2_write_inode(fd->fs, fd->inode_num, &fd->inode);
    }
    
    return bytes;
}

/**
 * Seek in ext4 file
 */
int ext4_seek_fd(int fd_idx, int32_t offset, int whence) {
    if (fd_idx < 0 || fd_idx >= EXT4_MAX_FDS || !ext4_fds[fd_idx].active) {
        return -1;
    }
    
    ext4_fd_ctx_t *fd = &ext4_fds[fd_idx];
    uint32_t new_pos;
    
    switch (whence) {
        case 0:  /* SEEK_SET */
            new_pos = offset;
            break;
        case 1:  /* SEEK_CUR */
            new_pos = fd->position + offset;
            break;
        case 2:  /* SEEK_END */
            new_pos = fd->inode.i_size + offset;
            break;
        default:
            return -1;
    }
    
    fd->position = new_pos;
    return (int)new_pos;
}

/**
 * Close ext4 file
 */
int ext4_close_fd(int fd_idx) {
    if (fd_idx < 0 || fd_idx >= EXT4_MAX_FDS || !ext4_fds[fd_idx].active) {
        return -1;
    }
    
    ext4_fds[fd_idx].active = 0;
    return 0;
}

/**
 * List directory on ext4 filesystem
 */
int ext4_list_dir(const char *mountpoint, const char *path) {
    ext4_mount_ctx_t *ctx = ext4_find_mount(mountpoint);
    if (!ctx) {
        return -1;
    }
    
    /* Resolve path to inode */
    ext2_inode_t inode;
    uint32_t inode_num;
    
    if (ext2_resolve_path(ctx->fs, path, &inode, &inode_num) != 0) {
        return -1;
    }
    
    /* Check if directory */
    if ((inode.i_mode & 0xF000) != EXT2_S_IFDIR) {
        return -1;
    }
    
    /* Use buffer for directory entry with name */
    static uint8_t entry_buf[sizeof(ext2_dir_entry_t) + 256];
    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)entry_buf;
    
    serial_printf("[EXT4-VFS] Directory listing for %s%s:\n", mountpoint, path);
    
    uint32_t index = 0;
    while (ext2_read_dir(ctx->fs, &inode, index, entry) == 0) {
        /* Print entry */
        char type_char = '?';
        switch (entry->file_type) {
            case EXT2_FT_REG_FILE: type_char = '-'; break;
            case EXT2_FT_DIR:      type_char = 'd'; break;
            case EXT2_FT_SYMLINK:  type_char = 'l'; break;
            case EXT2_FT_CHRDEV:   type_char = 'c'; break;
            case EXT2_FT_BLKDEV:   type_char = 'b'; break;
        }
        
        serial_printf("  %c %8u %s\n", type_char, entry->inode, entry->name);
        index++;
    }
    
    return (int)index;
}
