/**
 * TocinFS - Next-Generation Filesystem
 * 
 * Modern filesystem with CoW, snapshots, and compression
 */

#ifndef TOCINFS_H
#define TOCINFS_H

#include "../stdint.h"

// TocinFS magic number
#define TOCINFS_MAGIC 0x546F6346  // "TocF"

// Filesystem flags
#define TOCINFS_FLAG_COMPRESSION  0x01
#define TOCINFS_FLAG_CHECKSUMS    0x02
#define TOCINFS_FLAG_SNAPSHOTS    0x04

// Compression types
enum tocinfs_compression {
    TOCINFS_COMPRESS_NONE = 0,
    TOCINFS_COMPRESS_LZ4 = 1,
    TOCINFS_COMPRESS_ZSTD = 2,
    TOCINFS_COMPRESS_LZO = 3,
};

// Checksum types
enum tocinfs_checksum {
    TOCINFS_CHECKSUM_NONE = 0,
    TOCINFS_CHECKSUM_CRC32 = 1,
    TOCINFS_CHECKSUM_SHA256 = 2,
};

// UUID structure
typedef struct {
    uint8_t bytes[16];
} uuid_t;

// TocinFS superblock
typedef struct tocinfs_super_block {
    uint32_t magic;                  // Magic number
    uint32_t version;                // Filesystem version
    uint64_t total_size;             // Total size in bytes
    uint64_t block_size;             // Block size (4KB, 8KB, 16KB)
    uint64_t root_tree_addr;         // Root B-tree address
    uint64_t chunk_tree_addr;        // Chunk allocation tree
    uint64_t log_tree_addr;          // Journal log tree
    uint64_t snapshot_tree_addr;     // Snapshot tree
    
    uint8_t compression_type;        // Default compression
    uint8_t checksum_type;           // Checksum algorithm
    uint16_t flags;                  // FS flags
    
    uuid_t uuid;                     // Filesystem UUID
    char label[256];                 // Volume label
    
    uint64_t generation;             // Transaction ID
    uint64_t num_devices;            // Number of devices
    
    uint8_t reserved[512];           // Reserved for future use
} tocinfs_super_t;

// TocinFS inode
typedef struct tocinfs_inode {
    uint64_t inode_num;              // Inode number
    uint64_t size;                   // File size
    uint64_t blocks;                 // Number of blocks
    uint64_t atime;                  // Access time
    uint64_t mtime;                  // Modification time
    uint64_t ctime;                  // Creation time
    uint32_t mode;                   // File mode
    uint32_t uid;                    // Owner UID
    uint32_t gid;                    // Owner GID
    uint32_t nlink;                  // Number of hard links
    uint64_t extent_tree;            // Extent tree root
} tocinfs_inode_t;

// TocinFS extent
typedef struct tocinfs_extent {
    uint64_t logical_offset;         // Logical offset in file
    uint64_t physical_offset;        // Physical offset on disk
    uint64_t length;                 // Length of extent
    uint32_t flags;                  // Extent flags
} tocinfs_extent_t;

// TocinFS snapshot
typedef struct tocinfs_snapshot {
    uint64_t snapshot_id;            // Snapshot ID
    uint64_t generation;             // Generation number
    uint64_t timestamp;              // Creation timestamp
    char name[256];                  // Snapshot name
    uint64_t root_tree_addr;         // Root tree of snapshot
} tocinfs_snapshot_t;

// Function prototypes

/**
 * Initialize TocinFS
 */
int tocinfs_init(void);

/**
 * Mount TocinFS filesystem
 */
int tocinfs_mount(const char *device, const char *mountpoint);

/**
 * Unmount TocinFS filesystem
 */
int tocinfs_unmount(const char *mountpoint);

/**
 * Create file
 */
int tocinfs_create(const char *path, uint32_t mode);

/**
 * Read file
 */
int tocinfs_read(int fd, void *buffer, uint64_t size);

/**
 * Write file (CoW)
 */
int tocinfs_write(int fd, const void *buffer, uint64_t size);

/**
 * Create snapshot
 */
int tocinfs_create_snapshot(const char *source, const char *snapshot_name);

/**
 * Rollback to snapshot
 */
int tocinfs_rollback_snapshot(const char *snapshot_name);

/**
 * Delete snapshot
 */
int tocinfs_delete_snapshot(const char *snapshot_name);

/**
 * Defragment file
 */
int tocinfs_defrag_file(const char *path);

/**
 * Get filesystem info
 */
int tocinfs_statfs(const char *path, tocinfs_super_t *info);

#endif // TOCINFS_H
