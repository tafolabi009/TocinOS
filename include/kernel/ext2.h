/**
 * TocinOS ext2/3/4 Filesystem Support
 * 
 * Provides support for Linux ext2, ext3, and ext4 filesystems
 */

#ifndef EXT2_H
#define EXT2_H

#include "../stdint.h"

// ext2 magic number
#define EXT2_SUPER_MAGIC    0xEF53

// ext2 block size
#define EXT2_MIN_BLOCK_SIZE     1024
#define EXT2_MAX_BLOCK_SIZE     4096

// ext2 inode sizes
#define EXT2_GOOD_OLD_INODE_SIZE    128
#define EXT2_DYNAMIC_INODE_SIZE     256

// ext2 file types
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

// ext2 inode modes
#define EXT2_S_IFSOCK   0xC000  // Socket
#define EXT2_S_IFLNK    0xA000  // Symbolic link
#define EXT2_S_IFREG    0x8000  // Regular file
#define EXT2_S_IFBLK    0x6000  // Block device
#define EXT2_S_IFDIR    0x4000  // Directory
#define EXT2_S_IFCHR    0x2000  // Character device
#define EXT2_S_IFIFO    0x1000  // FIFO

// ext2 superblock
typedef struct {
    uint32_t s_inodes_count;        // Total inodes count
    uint32_t s_blocks_count;        // Total blocks count
    uint32_t s_r_blocks_count;      // Reserved blocks count
    uint32_t s_free_blocks_count;   // Free blocks count
    uint32_t s_free_inodes_count;   // Free inodes count
    uint32_t s_first_data_block;    // First data block
    uint32_t s_log_block_size;      // Block size (log2(block_size) - 10)
    uint32_t s_log_frag_size;       // Fragment size
    uint32_t s_blocks_per_group;    // Blocks per group
    uint32_t s_frags_per_group;     // Fragments per group
    uint32_t s_inodes_per_group;    // Inodes per group
    uint32_t s_mtime;               // Mount time
    uint32_t s_wtime;               // Write time
    uint16_t s_mnt_count;           // Mount count
    uint16_t s_max_mnt_count;       // Max mount count
    uint16_t s_magic;               // Magic signature (0xEF53)
    uint16_t s_state;               // File system state
    uint16_t s_errors;              // Behavior on errors
    uint16_t s_minor_rev_level;     // Minor revision level
    uint32_t s_lastcheck;           // Last check time
    uint32_t s_checkinterval;       // Check interval
    uint32_t s_creator_os;          // Creator OS
    uint32_t s_rev_level;           // Revision level
    uint16_t s_def_resuid;          // Default uid for reserved blocks
    uint16_t s_def_resgid;          // Default gid for reserved blocks
    
    // Extended fields (for dynamic inodes)
    uint32_t s_first_ino;           // First non-reserved inode
    uint16_t s_inode_size;          // Size of inode structure
    uint16_t s_block_group_nr;      // Block group number of this superblock
    uint32_t s_feature_compat;      // Compatible feature set
    uint32_t s_feature_incompat;    // Incompatible feature set
    uint32_t s_feature_ro_compat;   // Readonly-compatible feature set
    uint8_t  s_uuid[16];            // Volume UUID
    uint8_t  s_volume_name[16];     // Volume name
    uint8_t  s_last_mounted[64];    // Directory last mounted on
    uint32_t s_algorithm_usage_bitmap; // Compression algorithms
    
    // Performance hints
    uint8_t  s_prealloc_blocks;     // Blocks to preallocate for files
    uint8_t  s_prealloc_dir_blocks; // Blocks to preallocate for directories
    uint16_t s_reserved_gdt_blocks; // Reserved GDT blocks for expansion
    
    // Journaling (ext3)
    uint8_t  s_journal_uuid[16];    // UUID of journal superblock
    uint32_t s_journal_inum;        // Inode number of journal file
    uint32_t s_journal_dev;         // Device number of journal file
    uint32_t s_last_orphan;         // Head of orphan inode list
    uint32_t s_hash_seed[4];        // HTREE hash seed
    uint8_t  s_def_hash_version;    // Default hash version
    uint8_t  s_jnl_backup_type;     // Journal backup type
    uint16_t s_desc_size;           // Group descriptor size
    uint32_t s_default_mount_opts;  // Default mount options
    uint32_t s_first_meta_bg;       // First metablock group
    uint32_t s_mkfs_time;           // Filesystem creation time
    uint32_t s_jnl_blocks[17];      // Journal inode backup
    
    // ext4 extensions
    uint32_t s_blocks_count_hi;     // High 32 bits of block count
    uint32_t s_r_blocks_count_hi;   // High 32 bits of reserved block count
    uint32_t s_free_blocks_count_hi;// High 32 bits of free block count
    uint16_t s_min_extra_isize;     // All inodes have at least this size
    uint16_t s_want_extra_isize;    // New inodes should reserve this size
    uint32_t s_flags;               // Miscellaneous flags
    uint16_t s_raid_stride;         // RAID stride
    uint16_t s_mmp_interval;        // Multi-mount protection interval
    uint64_t s_mmp_block;           // Multi-mount protection block
    uint32_t s_raid_stripe_width;   // RAID stripe width
    uint8_t  s_log_groups_per_flex; // FLEX_BG group size
    uint8_t  s_checksum_type;       // Metadata checksum algorithm
    uint16_t s_reserved_pad;        // Padding
    uint64_t s_kbytes_written;      // KB written lifetime
    uint32_t s_snapshot_inum;       // Inode number of active snapshot
    uint32_t s_snapshot_id;         // ID of active snapshot
    uint64_t s_snapshot_r_blocks_count; // Reserved blocks for snapshot
    uint32_t s_snapshot_list;       // Inode number of snapshot list
    uint32_t s_error_count;         // Number of errors
    uint32_t s_first_error_time;    // First error time
    uint32_t s_first_error_ino;     // First error inode
    uint64_t s_first_error_block;   // First error block
    uint8_t  s_first_error_func[32];// First error function
    uint32_t s_first_error_line;    // First error line
    uint32_t s_last_error_time;     // Last error time
    uint32_t s_last_error_ino;      // Last error inode
    uint32_t s_last_error_line;     // Last error line
    uint64_t s_last_error_block;    // Last error block
    uint8_t  s_last_error_func[32]; // Last error function
    uint8_t  s_mount_opts[64];      // Mount options
    uint32_t s_usr_quota_inum;      // User quota inode
    uint32_t s_grp_quota_inum;      // Group quota inode
    uint32_t s_overhead_blocks;     // Overhead blocks
    uint32_t s_backup_bgs[2];       // Backup superblock groups
    uint8_t  s_encrypt_algos[4];    // Encryption algorithms
    uint8_t  s_encrypt_pw_salt[16]; // Encryption password salt
    uint32_t s_lpf_ino;             // Lost+found inode
    uint32_t s_prj_quota_inum;      // Project quota inode
    uint32_t s_checksum_seed;       // Checksum seed
    uint32_t s_reserved[98];        // Padding to end of block
    uint32_t s_checksum;            // Superblock checksum
} __attribute__((packed)) ext2_superblock_t;

// ext2 block group descriptor
typedef struct {
    uint32_t bg_block_bitmap;       // Block bitmap block
    uint32_t bg_inode_bitmap;       // Inode bitmap block
    uint32_t bg_inode_table;        // Inode table block
    uint16_t bg_free_blocks_count;  // Free blocks count
    uint16_t bg_free_inodes_count;  // Free inodes count
    uint16_t bg_used_dirs_count;    // Directories count
    uint16_t bg_flags;              // Flags
    uint32_t bg_exclude_bitmap_lo;  // Exclude bitmap low
    uint16_t bg_block_bitmap_csum_lo; // Block bitmap checksum
    uint16_t bg_inode_bitmap_csum_lo; // Inode bitmap checksum
    uint16_t bg_itable_unused;      // Unused inodes count
    uint16_t bg_checksum;           // Group descriptor checksum
} __attribute__((packed)) ext2_group_desc_t;

// ext2 inode
typedef struct {
    uint16_t i_mode;                // File mode
    uint16_t i_uid;                 // Owner UID
    uint32_t i_size;                // File size (low 32 bits)
    uint32_t i_atime;               // Access time
    uint32_t i_ctime;               // Creation time
    uint32_t i_mtime;               // Modification time
    uint32_t i_dtime;               // Deletion time
    uint16_t i_gid;                 // Group ID
    uint16_t i_links_count;         // Hard links count
    uint32_t i_blocks;              // Blocks count
    uint32_t i_flags;               // File flags
    uint32_t i_osd1;                // OS dependent
    uint32_t i_block[15];           // Pointers to blocks
    uint32_t i_generation;          // File version
    uint32_t i_file_acl;            // File ACL
    uint32_t i_dir_acl;             // Directory ACL / size high
    uint32_t i_faddr;               // Fragment address
    uint8_t  i_osd2[12];            // OS dependent
} __attribute__((packed)) ext2_inode_t;

// ext2 directory entry
typedef struct {
    uint32_t inode;                 // Inode number
    uint16_t rec_len;               // Directory entry length
    uint8_t  name_len;              // Name length
    uint8_t  file_type;             // File type
    char     name[];                // File name (variable length)
} __attribute__((packed)) ext2_dir_entry_t;

// ext2 filesystem context
typedef struct {
    ext2_superblock_t *superblock;  // Superblock
    ext2_group_desc_t *group_desc;  // Group descriptors
    uint32_t block_size;            // Block size in bytes
    uint32_t num_groups;            // Number of block groups
    uint32_t inodes_per_group;      // Inodes per group
    uint32_t blocks_per_group;      // Blocks per group
    void *device;                   // Block device
} ext2_fs_t;

// ==================== ext2/ext4 API ====================

// Initialization and mounting
int ext2_init(void);
int ext2_mount(const char *device, ext2_fs_t **fs);
int ext2_unmount(ext2_fs_t *fs);
int ext2_sync(ext2_fs_t *fs);

// Inode operations
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
int ext2_write_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_directory);
int ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int is_directory);

// File operations
int ext2_read_file(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, void *buffer);
int ext2_write_file(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, const void *buffer);

// Directory operations
int ext2_read_dir(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t index, ext2_dir_entry_t *entry);
int ext2_find_entry(ext2_fs_t *fs, ext2_inode_t *inode, const char *name, ext2_dir_entry_t *entry);
int ext2_add_dir_entry(ext2_fs_t *fs, ext2_inode_t *dir_inode, uint32_t dir_inode_num,
                       const char *name, uint32_t new_inode, uint8_t file_type);

// File/directory creation and deletion
int ext2_create(ext2_fs_t *fs, uint32_t parent_inode_num, const char *name,
                uint16_t mode, uint32_t *new_inode_num);

// Path resolution
int ext2_resolve_path(ext2_fs_t *fs, const char *path, ext2_inode_t *inode, uint32_t *inode_num);

// Block operations
int ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer);
int ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer);

// Utility functions
uint32_t ext2_get_block_size(ext2_superblock_t *sb);
uint32_t ext2_get_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_index);
uint32_t ext2_get_block_enhanced(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_index);

// Statistics and debug
void ext2_print_stats(ext2_fs_t *fs);

// ==================== JBD2 Journaling API ====================

/* Forward declaration for journal handle */
struct jbd2_handle;
typedef struct jbd2_handle jbd2_handle_t;

/* Journal operations */
int jbd2_init(ext2_fs_t *fs);
int jbd2_recover(ext2_fs_t *fs);
jbd2_handle_t *jbd2_start(ext2_fs_t *fs, int num_blocks);
int jbd2_get_write_access(jbd2_handle_t *handle, uint32_t block_num);
int jbd2_commit(jbd2_handle_t *handle);
void jbd2_abort(jbd2_handle_t *handle);

/* Journaled block write */
int ext2_write_block_journaled(ext2_fs_t *fs, uint32_t block_num, const void *buffer);

// ==================== EXT4 VFS Integration ====================

/* Initialize ext4 VFS integration and register with VFS layer */
int ext4_vfs_init(void);

/* Direct ext4 file operations (for use without VFS) */
int ext4_open_file(const char *mountpoint, const char *path, uint32_t mode);
int ext4_read_fd(int fd_idx, void *buffer, uint32_t size);
int ext4_write_fd(int fd_idx, const void *buffer, uint32_t size);
int ext4_seek_fd(int fd_idx, int32_t offset, int whence);
int ext4_close_fd(int fd_idx);
int ext4_list_dir(const char *mountpoint, const char *path);

#endif // EXT2_H
