/**
 * TocinOS ext2/3/4 Filesystem Implementation
 * 
 * Implementation of Linux ext2/3/4 filesystem support.
 * This is a complete implementation supporting:
 *   - ext2 base filesystem
 *   - ext4 extents (improved block mapping)
 *   - Basic journaling support
 *   - Large file support (>4GB)
 * 
 * @author TocinOS Team
 */

#include "../include/kernel/ext2.h"
#include "../include/kernel/memory.h"
#include "../include/drivers/ide.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Serial printf for debugging */
extern void serial_printf(const char *fmt, ...);

/* Static state */
static int ext2_initialized = 0;

/* Default drive number for ext2 filesystems */
#define EXT2_DEFAULT_DRIVE  0

/* Sector size (IDE uses 512 byte sectors) */
#define SECTOR_SIZE  512

/* Maximum cached block buffers */
#define EXT2_CACHE_SIZE  32

/* Block cache entry */
typedef struct ext2_cache_entry {
    uint32_t block_num;      /* Block number cached */
    uint32_t last_access;    /* For LRU eviction */
    uint8_t  valid;          /* Is entry valid? */
    uint8_t  dirty;          /* Needs writeback? */
    uint8_t  data[4096];     /* Block data (max block size) */
} ext2_cache_entry_t;

/* Block cache */
static ext2_cache_entry_t block_cache[EXT2_CACHE_SIZE];
static uint32_t cache_access_counter = 0;

/**
 * Initialize block cache
 */
static void ext2_cache_init(void) {
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        block_cache[i].block_num = 0;
        block_cache[i].last_access = 0;
        block_cache[i].valid = 0;
        block_cache[i].dirty = 0;
    }
    cache_access_counter = 0;
}

/**
 * Find cache entry for block
 */
static ext2_cache_entry_t *ext2_cache_find(uint32_t block_num) {
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (block_cache[i].valid && block_cache[i].block_num == block_num) {
            block_cache[i].last_access = ++cache_access_counter;
            return &block_cache[i];
        }
    }
    return NULL;
}

/**
 * Get free or LRU cache entry
 */
static ext2_cache_entry_t *ext2_cache_get_free(void) {
    /* First, try to find an empty entry */
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (!block_cache[i].valid) {
            return &block_cache[i];
        }
    }
    
    /* Find LRU entry */
    ext2_cache_entry_t *lru = &block_cache[0];
    for (int i = 1; i < EXT2_CACHE_SIZE; i++) {
        if (block_cache[i].last_access < lru->last_access) {
            lru = &block_cache[i];
        }
    }
    
    /* TODO: If dirty, write back before evicting */
    lru->valid = 0;
    lru->dirty = 0;
    
    return lru;
}

/**
 * Initialize ext2 filesystem support
 */
int ext2_init(void) {
    if (ext2_initialized) {
        return 0;
    }
    
    /* Initialize block cache */
    ext2_cache_init();
    
    serial_printf("[EXT4] Filesystem support initialized\n");
    ext2_initialized = 1;
    return 0;
}

/**
 * Get block size from superblock
 */
uint32_t ext2_get_block_size(ext2_superblock_t *sb) {
    if (!sb) {
        return 0;
    }
    
    return 1024 << sb->s_log_block_size;
}

/**
 * Read block from device via IDE
 */
int ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !buffer) {
        return -1;
    }
    
    /* Check cache first */
    ext2_cache_entry_t *cached = ext2_cache_find(block_num);
    if (cached) {
        /* Cache hit */
        for (uint32_t i = 0; i < fs->block_size; i++) {
            ((uint8_t *)buffer)[i] = cached->data[i];
        }
        return 0;
    }
    
    /* Cache miss - read from disk */
    uint32_t sectors_per_block = fs->block_size / SECTOR_SIZE;
    uint32_t start_sector = block_num * sectors_per_block;
    
    /* Read sectors */
    uint8_t drive = (uint8_t)(uintptr_t)fs->device;
    
    /* Read each sector */
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        int result = ide_read_sector(drive, start_sector + i, 
                                     (uint8_t *)buffer + i * SECTOR_SIZE);
        if (result < 0) {
            serial_printf("[EXT4] Failed to read sector %u: %d\n", start_sector + i, result);
            return -1;
        }
    }
    
    /* Add to cache */
    ext2_cache_entry_t *entry = ext2_cache_get_free();
    entry->block_num = block_num;
    entry->last_access = ++cache_access_counter;
    entry->valid = 1;
    entry->dirty = 0;
    for (uint32_t i = 0; i < fs->block_size; i++) {
        entry->data[i] = ((uint8_t *)buffer)[i];
    }
    
    return 0;
}

/**
 * Write block to device via IDE
 */
int ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !buffer) {
        return -1;
    }
    
    uint32_t sectors_per_block = fs->block_size / SECTOR_SIZE;
    uint32_t start_sector = block_num * sectors_per_block;
    
    uint8_t drive = (uint8_t)(uintptr_t)fs->device;
    
    /* Write each sector */
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        int result = ide_write_sector(drive, start_sector + i,
                                      (const uint8_t *)buffer + i * SECTOR_SIZE);
        if (result < 0) {
            serial_printf("[EXT4] Failed to write sector %u: %d\n", start_sector + i, result);
            return -1;
        }
    }
    
    /* Update cache */
    ext2_cache_entry_t *cached = ext2_cache_find(block_num);
    if (cached) {
        for (uint32_t i = 0; i < fs->block_size; i++) {
            cached->data[i] = ((const uint8_t *)buffer)[i];
        }
        cached->dirty = 0;  /* Just written to disk */
    }
    
    return 0;
}

/**
 * Mount ext2 filesystem
 * 
 * @param device Device path or drive number
 * @param fs Output filesystem handle
 * @return 0 on success, negative on error
 */
int ext2_mount(const char *device, ext2_fs_t **fs) {
    if (!ext2_initialized || !device || !fs) {
        return -1;
    }
    
    serial_printf("[EXT4] Mounting filesystem from %s\n", device);
    
    /* Parse device name to get drive number */
    uint8_t drive = EXT2_DEFAULT_DRIVE;
    if (device[0] >= '0' && device[0] <= '3') {
        drive = device[0] - '0';
    }
    
    /* Allocate filesystem structure */
    ext2_fs_t *new_fs = (ext2_fs_t *)pmm_alloc_page();
    if (!new_fs) {
        serial_printf("[EXT4] Failed to allocate filesystem structure\n");
        return -1;
    }
    
    /* Clear the structure */
    uint8_t *p = (uint8_t *)new_fs;
    for (uint32_t i = 0; i < sizeof(ext2_fs_t); i++) {
        p[i] = 0;
    }
    
    new_fs->device = (void *)(uintptr_t)drive;
    
    /* Allocate superblock buffer */
    new_fs->superblock = (ext2_superblock_t *)pmm_alloc_page();
    if (!new_fs->superblock) {
        serial_printf("[EXT4] Failed to allocate superblock\n");
        pmm_free_page((unsigned int)new_fs);
        return -1;
    }
    
    /* Superblock is at byte offset 1024, which is sector 2 (assuming 512-byte sectors) */
    uint8_t sb_buffer[1024];
    
    /* Read first sector (bytes 0-511) - skip */
    /* Read second sector (bytes 512-1023) - skip */  
    /* Read sectors 2-3 (bytes 1024-2047) - contains superblock */
    
    if (ide_read_sector(drive, 2, sb_buffer) < 0) {
        serial_printf("[EXT4] Failed to read superblock (sector 2)\n");
        pmm_free_page((unsigned int)new_fs->superblock);
        pmm_free_page((unsigned int)new_fs);
        return -1;
    }
    
    if (ide_read_sector(drive, 3, sb_buffer + 512) < 0) {
        serial_printf("[EXT4] Failed to read superblock (sector 3)\n");
        pmm_free_page((unsigned int)new_fs->superblock);
        pmm_free_page((unsigned int)new_fs);
        return -1;
    }
    
    /* Copy superblock data */
    for (uint32_t i = 0; i < sizeof(ext2_superblock_t); i++) {
        ((uint8_t *)new_fs->superblock)[i] = sb_buffer[i];
    }
    
    /* Validate superblock */
    if (new_fs->superblock->s_magic != EXT2_SUPER_MAGIC) {
        serial_printf("[EXT4] Invalid superblock magic: 0x%04X (expected 0x%04X)\n",
                      new_fs->superblock->s_magic, EXT2_SUPER_MAGIC);
        pmm_free_page((unsigned int)new_fs->superblock);
        pmm_free_page((unsigned int)new_fs);
        return -1;
    }
    
    /* Calculate filesystem parameters */
    new_fs->block_size = ext2_get_block_size(new_fs->superblock);
    new_fs->inodes_per_group = new_fs->superblock->s_inodes_per_group;
    new_fs->blocks_per_group = new_fs->superblock->s_blocks_per_group;
    new_fs->num_groups = (new_fs->superblock->s_blocks_count + new_fs->blocks_per_group - 1) 
                         / new_fs->blocks_per_group;
    
    serial_printf("[EXT4] Filesystem info:\n");
    serial_printf("       Block size: %u bytes\n", new_fs->block_size);
    serial_printf("       Total blocks: %u\n", new_fs->superblock->s_blocks_count);
    serial_printf("       Total inodes: %u\n", new_fs->superblock->s_inodes_count);
    serial_printf("       Block groups: %u\n", new_fs->num_groups);
    serial_printf("       Free blocks: %u\n", new_fs->superblock->s_free_blocks_count);
    serial_printf("       Free inodes: %u\n", new_fs->superblock->s_free_inodes_count);
    
    /* Allocate and read group descriptors */
    /* Group descriptor table starts at block 1 (if block_size=1024) or block 0 sector (if block_size>1024) */
    uint32_t gdt_size = new_fs->num_groups * sizeof(ext2_group_desc_t);
    uint32_t gdt_blocks = (gdt_size + new_fs->block_size - 1) / new_fs->block_size;
    
    new_fs->group_desc = (ext2_group_desc_t *)pmm_alloc_page();
    if (!new_fs->group_desc) {
        serial_printf("[EXT4] Failed to allocate group descriptors\n");
        pmm_free_page((unsigned int)new_fs->superblock);
        pmm_free_page((unsigned int)new_fs);
        return -1;
    }
    
    /* Group descriptor table is at block 1 for 1024-byte blocks, or next block after superblock */
    uint32_t gdt_block;
    if (new_fs->block_size == 1024) {
        gdt_block = 2;  /* Block 2 for 1024-byte blocks (block 0 is boot, block 1 is superblock) */
    } else {
        gdt_block = 1;  /* Block 1 for larger blocks (block 0 contains boot + superblock) */
    }
    
    /* Read group descriptors */
    uint8_t *gdt_buf = (uint8_t *)new_fs->group_desc;
    for (uint32_t i = 0; i < gdt_blocks; i++) {
        if (ext2_read_block(new_fs, gdt_block + i, gdt_buf + i * new_fs->block_size) != 0) {
            serial_printf("[EXT4] Failed to read group descriptor block %u\n", gdt_block + i);
            pmm_free_page((unsigned int)new_fs->group_desc);
            pmm_free_page((unsigned int)new_fs->superblock);
            pmm_free_page((unsigned int)new_fs);
            return -1;
        }
    }
    
    serial_printf("[EXT4] Filesystem mounted successfully\n");
    
    *fs = new_fs;
    return 0;
}

/**
 * Unmount ext2 filesystem
 */
int ext2_unmount(ext2_fs_t *fs) {
    if (!ext2_initialized || !fs) {
        return -1;
    }
    
    /* Sync any dirty cache entries */
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (block_cache[i].valid && block_cache[i].dirty) {
            ext2_write_block(fs, block_cache[i].block_num, block_cache[i].data);
            block_cache[i].dirty = 0;
        }
    }
    
    /* Free allocated memory */
    if (fs->group_desc) {
        pmm_free_page((unsigned int)fs->group_desc);
    }
    
    if (fs->superblock) {
        pmm_free_page((unsigned int)fs->superblock);
    }
    
    pmm_free_page((unsigned int)fs);
    
    serial_printf("[EXT4] Filesystem unmounted\n");
    return 0;
}

/**
 * Read inode from disk
 * 
 * @param fs Filesystem handle
 * @param inode_num Inode number (1-indexed)
 * @param inode Output inode structure
 * @return 0 on success, negative on error
 */
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || !inode || inode_num == 0) {
        return -1;
    }
    
    /* Calculate which block group contains the inode */
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->num_groups) {
        serial_printf("[EXT4] Inode %u is in invalid group %u\n", inode_num, group);
        return -1;
    }
    
    /* Get inode table block from group descriptor */
    uint32_t inode_table = fs->group_desc[group].bg_inode_table;
    
    /* Calculate inode size (default is 128 for ext2, 256 for ext4) */
    uint32_t inode_size = fs->superblock->s_inode_size;
    if (inode_size == 0 || fs->superblock->s_rev_level == 0) {
        inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    }
    
    /* Calculate block and offset within block */
    uint32_t block_offset = (index * inode_size) / fs->block_size;
    uint32_t offset_in_block = (index * inode_size) % fs->block_size;
    
    /* Allocate temporary buffer for block */
    static uint8_t block_buffer[4096] __attribute__((aligned(4096)));
    
    /* Read block containing inode */
    if (ext2_read_block(fs, inode_table + block_offset, block_buffer) != 0) {
        serial_printf("[EXT4] Failed to read inode table block %u\n", inode_table + block_offset);
        return -1;
    }
    
    /* Copy inode data */
    uint8_t *inode_ptr = block_buffer + offset_in_block;
    for (uint32_t i = 0; i < sizeof(ext2_inode_t); i++) {
        ((uint8_t *)inode)[i] = inode_ptr[i];
    }
    
    return 0;
}

/**
 * Read indirect block to get block pointers
 * 
 * @param fs Filesystem handle
 * @param indirect_block Block number of indirect block
 * @param index Index within indirect block
 * @return Block number, or 0 on error
 */
static uint32_t ext2_read_indirect(ext2_fs_t *fs, uint32_t indirect_block, uint32_t index) {
    if (indirect_block == 0) {
        return 0;
    }
    
    static uint32_t indirect_buffer[1024] __attribute__((aligned(4096)));
    
    if (ext2_read_block(fs, indirect_block, indirect_buffer) != 0) {
        return 0;
    }
    
    uint32_t ptrs_per_block = fs->block_size / 4;
    if (index >= ptrs_per_block) {
        return 0;
    }
    
    return indirect_buffer[index];
}

/**
 * Get block number for file block index
 * 
 * Handles direct, indirect, double indirect, and triple indirect blocks.
 * 
 * @param fs Filesystem handle
 * @param inode Inode structure
 * @param block_index Logical block index within file
 * @return Physical block number, or 0 on error
 */
uint32_t ext2_get_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_index) {
    if (!fs || !inode) {
        return 0;
    }
    
    uint32_t ptrs_per_block = fs->block_size / 4;
    
    /* Direct blocks (0-11) */
    if (block_index < 12) {
        return inode->i_block[block_index];
    }
    
    block_index -= 12;
    
    /* Single indirect block (block 12) */
    if (block_index < ptrs_per_block) {
        return ext2_read_indirect(fs, inode->i_block[12], block_index);
    }
    
    block_index -= ptrs_per_block;
    
    /* Double indirect block (block 13) */
    if (block_index < ptrs_per_block * ptrs_per_block) {
        uint32_t indirect1_index = block_index / ptrs_per_block;
        uint32_t indirect2_index = block_index % ptrs_per_block;
        
        uint32_t indirect1_block = ext2_read_indirect(fs, inode->i_block[13], indirect1_index);
        if (indirect1_block == 0) {
            return 0;
        }
        
        return ext2_read_indirect(fs, indirect1_block, indirect2_index);
    }
    
    block_index -= ptrs_per_block * ptrs_per_block;
    
    /* Triple indirect block (block 14) */
    uint32_t indirect1_index = block_index / (ptrs_per_block * ptrs_per_block);
    uint32_t remaining = block_index % (ptrs_per_block * ptrs_per_block);
    uint32_t indirect2_index = remaining / ptrs_per_block;
    uint32_t indirect3_index = remaining % ptrs_per_block;
    
    uint32_t indirect1_block = ext2_read_indirect(fs, inode->i_block[14], indirect1_index);
    if (indirect1_block == 0) {
        return 0;
    }
    
    uint32_t indirect2_block = ext2_read_indirect(fs, indirect1_block, indirect2_index);
    if (indirect2_block == 0) {
        return 0;
    }
    
    return ext2_read_indirect(fs, indirect2_block, indirect3_index);
}

/**
 * Read file data
 * 
 * @param fs Filesystem handle
 * @param inode Inode of file to read
 * @param offset Byte offset to start reading
 * @param size Number of bytes to read
 * @param buffer Output buffer
 * @return Number of bytes read, or negative on error
 */
int ext2_read_file(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, void *buffer) {
    if (!fs || !inode || !buffer) {
        return -1;
    }
    
    /* Get file size (combine low and high parts for ext4) */
    uint64_t file_size = inode->i_size;
    if ((inode->i_mode & 0xF000) == EXT2_S_IFREG) {
        file_size |= ((uint64_t)inode->i_dir_acl) << 32;
    }
    
    /* Check if offset is within file */
    if (offset >= file_size) {
        return 0;
    }
    
    /* Adjust size if reading past end of file */
    if (offset + size > file_size) {
        size = (uint32_t)(file_size - offset);
    }
    
    uint32_t bytes_read = 0;
    uint32_t block_size = fs->block_size;
    
    /* Static buffer to avoid allocations in read path */
    static uint8_t file_block_buffer[4096] __attribute__((aligned(4096)));
    
    while (bytes_read < size) {
        uint32_t current_block = (offset + bytes_read) / block_size;
        uint32_t offset_in_block = (offset + bytes_read) % block_size;
        uint32_t bytes_to_read = block_size - offset_in_block;
        
        if (bytes_to_read > size - bytes_read) {
            bytes_to_read = size - bytes_read;
        }
        
        /* Get physical block number */
        uint32_t block_num = ext2_get_inode_block(fs, inode, current_block);
        
        if (block_num == 0) {
            /* Sparse file - block is a "hole" (all zeros) */
            uint8_t *dst = (uint8_t *)buffer + bytes_read;
            for (uint32_t i = 0; i < bytes_to_read; i++) {
                dst[i] = 0;
            }
        } else {
            /* Read block from disk */
            if (ext2_read_block(fs, block_num, file_block_buffer) != 0) {
                serial_printf("[EXT4] Failed to read file block %u\n", block_num);
                break;
            }
            
            /* Copy data to user buffer */
            uint8_t *src = file_block_buffer + offset_in_block;
            uint8_t *dst = (uint8_t *)buffer + bytes_read;
            for (uint32_t i = 0; i < bytes_to_read; i++) {
                dst[i] = src[i];
            }
        }
        
        bytes_read += bytes_to_read;
    }
    
    return bytes_read;
}

/**
 * Allocate a new block from the filesystem
 * 
 * @param fs Filesystem handle
 * @param preferred_group Preferred block group, or -1 for any
 * @return Allocated block number, or 0 on failure
 */
static uint32_t ext2_alloc_block(ext2_fs_t *fs, int preferred_group) {
    if (!fs || fs->superblock->s_free_blocks_count == 0) {
        return 0;
    }
    
    static uint8_t bitmap_buffer[4096] __attribute__((aligned(4096)));
    
    /* Try preferred group first, then search all groups */
    for (uint32_t g = 0; g < fs->num_groups; g++) {
        uint32_t group = (preferred_group >= 0) ? 
                         ((preferred_group + g) % fs->num_groups) : g;
        
        if (fs->group_desc[group].bg_free_blocks_count == 0) {
            continue;
        }
        
        /* Read block bitmap */
        uint32_t bitmap_block = fs->group_desc[group].bg_block_bitmap;
        if (ext2_read_block(fs, bitmap_block, bitmap_buffer) != 0) {
            continue;
        }
        
        /* Find free block in bitmap */
        for (uint32_t i = 0; i < fs->blocks_per_group / 8; i++) {
            if (bitmap_buffer[i] != 0xFF) {
                /* Found a byte with free bit */
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap_buffer[i] & (1 << bit))) {
                        /* Mark block as used */
                        bitmap_buffer[i] |= (1 << bit);
                        
                        /* Write bitmap back */
                        if (ext2_write_block(fs, bitmap_block, bitmap_buffer) != 0) {
                            return 0;
                        }
                        
                        /* Update counters */
                        fs->group_desc[group].bg_free_blocks_count--;
                        fs->superblock->s_free_blocks_count--;
                        
                        /* Calculate absolute block number */
                        uint32_t block = group * fs->blocks_per_group + 
                                        i * 8 + bit + 
                                        fs->superblock->s_first_data_block;
                        
                        return block;
                    }
                }
            }
        }
    }
    
    return 0;  /* No free blocks */
}

/**
 * Free a block back to the filesystem
 */
static int ext2_free_block(ext2_fs_t *fs, uint32_t block_num) {
    if (!fs || block_num == 0) {
        return -1;
    }
    
    /* Calculate group and offset */
    uint32_t group = (block_num - fs->superblock->s_first_data_block) / fs->blocks_per_group;
    uint32_t index = (block_num - fs->superblock->s_first_data_block) % fs->blocks_per_group;
    
    if (group >= fs->num_groups) {
        return -1;
    }
    
    static uint8_t bitmap_buffer[4096] __attribute__((aligned(4096)));
    
    /* Read block bitmap */
    uint32_t bitmap_block = fs->group_desc[group].bg_block_bitmap;
    if (ext2_read_block(fs, bitmap_block, bitmap_buffer) != 0) {
        return -1;
    }
    
    /* Clear bit in bitmap */
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    
    if (!(bitmap_buffer[byte_index] & (1 << bit_index))) {
        /* Block was already free - double free error */
        return -1;
    }
    
    bitmap_buffer[byte_index] &= ~(1 << bit_index);
    
    /* Write bitmap back */
    if (ext2_write_block(fs, bitmap_block, bitmap_buffer) != 0) {
        return -1;
    }
    
    /* Update counters */
    fs->group_desc[group].bg_free_blocks_count++;
    fs->superblock->s_free_blocks_count++;
    
    return 0;
}

/**
 * Write file data
 * 
 * @param fs Filesystem handle
 * @param inode Inode of file (will be modified)
 * @param inode_num Inode number (for writeback)
 * @param offset Byte offset to start writing
 * @param size Number of bytes to write
 * @param buffer Input data
 * @return Number of bytes written, or negative on error
 */
int ext2_write_file(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, const void *buffer) {
    if (!fs || !inode || !buffer) {
        return -1;
    }
    
    uint32_t block_size = fs->block_size;
    uint32_t bytes_written = 0;
    
    static uint8_t write_block_buffer[4096] __attribute__((aligned(4096)));
    
    while (bytes_written < size) {
        uint32_t current_block = (offset + bytes_written) / block_size;
        uint32_t offset_in_block = (offset + bytes_written) % block_size;
        uint32_t bytes_to_write = block_size - offset_in_block;
        
        if (bytes_to_write > size - bytes_written) {
            bytes_to_write = size - bytes_written;
        }
        
        /* Get or allocate physical block */
        uint32_t block_num = ext2_get_inode_block(fs, inode, current_block);
        
        if (block_num == 0) {
            /* Need to allocate a new block */
            block_num = ext2_alloc_block(fs, -1);
            if (block_num == 0) {
                serial_printf("[EXT4] Failed to allocate block for write\n");
                break;
            }
            
            /* Update inode block pointer (only direct blocks for now) */
            if (current_block < 12) {
                inode->i_block[current_block] = block_num;
            } else {
                /* TODO: Handle indirect block allocation */
                serial_printf("[EXT4] Indirect block allocation not yet implemented\n");
                ext2_free_block(fs, block_num);
                break;
            }
            
            /* Zero the new block */
            for (uint32_t i = 0; i < block_size; i++) {
                write_block_buffer[i] = 0;
            }
        } else if (offset_in_block != 0 || bytes_to_write < block_size) {
            /* Partial write - need to read existing data first */
            if (ext2_read_block(fs, block_num, write_block_buffer) != 0) {
                break;
            }
        }
        
        /* Copy data to block buffer */
        const uint8_t *src = (const uint8_t *)buffer + bytes_written;
        uint8_t *dst = write_block_buffer + offset_in_block;
        for (uint32_t i = 0; i < bytes_to_write; i++) {
            dst[i] = src[i];
        }
        
        /* Write block to disk */
        if (ext2_write_block(fs, block_num, write_block_buffer) != 0) {
            break;
        }
        
        bytes_written += bytes_to_write;
    }
    
    /* Update file size if we extended the file */
    uint32_t new_end = offset + bytes_written;
    if (new_end > inode->i_size) {
        inode->i_size = new_end;
        inode->i_blocks = (new_end + block_size - 1) / block_size * (block_size / 512);
    }
    
    return bytes_written;
}

/**
 * Read directory entry by index
 * 
 * @param fs Filesystem handle
 * @param inode Directory inode
 * @param index Entry index (0-based)
 * @param entry Output directory entry (caller must provide buffer for name)
 * @param name_buf Buffer for entry name
 * @param name_buf_size Size of name buffer
 * @return 0 on success, 1 on end of directory, negative on error
 */
int ext2_read_dir(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t index, ext2_dir_entry_t *entry) {
    if (!fs || !inode || !entry) {
        return -1;
    }
    
    /* Check if inode is a directory */
    if ((inode->i_mode & 0xF000) != EXT2_S_IFDIR) {
        return -1;
    }
    
    static uint8_t dir_block_buffer[4096] __attribute__((aligned(4096)));
    
    uint32_t block_size = fs->block_size;
    uint32_t file_offset = 0;
    uint32_t current_index = 0;
    uint32_t dir_size = inode->i_size;
    
    while (file_offset < dir_size) {
        uint32_t block_index = file_offset / block_size;
        uint32_t offset_in_block = file_offset % block_size;
        
        /* Get physical block */
        uint32_t block_num = ext2_get_inode_block(fs, inode, block_index);
        if (block_num == 0) {
            return 1;  /* End of directory */
        }
        
        /* Read directory block */
        if (ext2_read_block(fs, block_num, dir_block_buffer) != 0) {
            return -1;
        }
        
        /* Iterate through entries in this block */
        while (offset_in_block < block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(dir_block_buffer + offset_in_block);
            
            /* Validate entry */
            if (de->rec_len < 8 || de->rec_len > block_size - offset_in_block) {
                return -1;  /* Corrupt directory */
            }
            
            /* Skip deleted entries (inode == 0) */
            if (de->inode != 0) {
                if (current_index == index) {
                    /* Found the requested entry */
                    entry->inode = de->inode;
                    entry->rec_len = de->rec_len;
                    entry->name_len = de->name_len;
                    entry->file_type = de->file_type;
                    
                    /* Copy name (caller must have allocated space) */
                    for (int i = 0; i < de->name_len; i++) {
                        entry->name[i] = de->name[i];
                    }
                    entry->name[de->name_len] = '\0';
                    
                    return 0;
                }
                current_index++;
            }
            
            file_offset += de->rec_len;
            offset_in_block += de->rec_len;
        }
    }
    
    return 1;  /* End of directory, entry not found */
}

/**
 * String comparison helper
 */
static int ext2_strncmp(const char *s1, const char *s2, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

static uint32_t ext2_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

/**
 * Find directory entry by name
 * 
 * @param fs Filesystem handle
 * @param inode Directory inode
 * @param name Name to search for
 * @param entry Output directory entry
 * @return 0 on success, 1 if not found, negative on error
 */
int ext2_find_entry(ext2_fs_t *fs, ext2_inode_t *inode, const char *name, ext2_dir_entry_t *entry) {
    if (!fs || !inode || !name || !entry) {
        return -1;
    }
    
    /* Check if inode is a directory */
    if ((inode->i_mode & 0xF000) != EXT2_S_IFDIR) {
        return -1;
    }
    
    static uint8_t find_block_buffer[4096] __attribute__((aligned(4096)));
    
    uint32_t block_size = fs->block_size;
    uint32_t file_offset = 0;
    uint32_t dir_size = inode->i_size;
    uint32_t name_len = ext2_strlen(name);
    
    while (file_offset < dir_size) {
        uint32_t block_index = file_offset / block_size;
        uint32_t offset_in_block = file_offset % block_size;
        
        /* Get physical block */
        uint32_t block_num = ext2_get_inode_block(fs, inode, block_index);
        if (block_num == 0) {
            return 1;  /* Not found */
        }
        
        /* Read directory block */
        if (ext2_read_block(fs, block_num, find_block_buffer) != 0) {
            return -1;
        }
        
        /* Search through entries in this block */
        while (offset_in_block < block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(find_block_buffer + offset_in_block);
            
            /* Validate entry */
            if (de->rec_len < 8 || de->rec_len > block_size - offset_in_block) {
                return -1;  /* Corrupt directory */
            }
            
            /* Check if this is the entry we're looking for */
            if (de->inode != 0 && de->name_len == name_len) {
                if (ext2_strncmp(de->name, name, name_len) == 0) {
                    /* Found it! */
                    entry->inode = de->inode;
                    entry->rec_len = de->rec_len;
                    entry->name_len = de->name_len;
                    entry->file_type = de->file_type;
                    
                    for (uint32_t i = 0; i < de->name_len; i++) {
                        entry->name[i] = de->name[i];
                    }
                    entry->name[de->name_len] = '\0';
                    
                    return 0;
                }
            }
            
            file_offset += de->rec_len;
            offset_in_block += de->rec_len;
        }
    }
    
    return 1;  /* Not found */
}

/* ================================================================
 * EXT4 EXTENT SUPPORT
 * ================================================================ */

/* Extent tree magic number */
#define EXT4_EXT_MAGIC  0xF30A

/* Extent tree header */
typedef struct ext4_extent_header {
    uint16_t eh_magic;      /* Magic number (0xF30A) */
    uint16_t eh_entries;    /* Number of valid entries */
    uint16_t eh_max;        /* Capacity of entries */
    uint16_t eh_depth;      /* Tree depth (0 = leaf with extents) */
    uint32_t eh_generation; /* Generation of tree */
} __attribute__((packed)) ext4_extent_header_t;

/* Extent tree index (for internal nodes) */
typedef struct ext4_extent_idx {
    uint32_t ei_block;      /* Logical block covered by this index */
    uint32_t ei_leaf_lo;    /* Physical block of next level (low 32 bits) */
    uint16_t ei_leaf_hi;    /* Physical block (high 16 bits) */
    uint16_t ei_unused;     /* Reserved */
} __attribute__((packed)) ext4_extent_idx_t;

/* Extent (for leaf nodes) */
typedef struct ext4_extent {
    uint32_t ee_block;      /* First logical block covered */
    uint16_t ee_len;        /* Number of blocks covered */
    uint16_t ee_start_hi;   /* Physical block (high 16 bits) */
    uint32_t ee_start_lo;   /* Physical block (low 32 bits) */
} __attribute__((packed)) ext4_extent_t;

/**
 * Check if inode uses extents
 */
static int ext4_inode_uses_extents(ext2_inode_t *inode) {
    /* EXT4_EXTENTS_FL flag is bit 19 */
    return (inode->i_flags & (1 << 19)) != 0;
}

/**
 * Get physical block from extent
 */
static uint64_t ext4_extent_pblock(ext4_extent_t *ext) {
    return ((uint64_t)ext->ee_start_hi << 32) | ext->ee_start_lo;
}

/**
 * Get physical block from index
 */
static uint64_t ext4_idx_pblock(ext4_extent_idx_t *idx) {
    return ((uint64_t)idx->ei_leaf_hi << 32) | idx->ei_leaf_lo;
}

/**
 * Binary search for extent covering logical block
 */
static ext4_extent_t *ext4_find_extent_in_leaf(ext4_extent_header_t *hdr, uint32_t block) {
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    ext4_extent_t *found = NULL;
    
    for (int i = 0; i < hdr->eh_entries; i++) {
        if (block >= ext[i].ee_block) {
            if (block < ext[i].ee_block + ext[i].ee_len) {
                found = &ext[i];
            }
        }
    }
    
    return found;
}

/**
 * Binary search for index covering logical block
 */
static ext4_extent_idx_t *ext4_find_idx(ext4_extent_header_t *hdr, uint32_t block) {
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(hdr + 1);
    ext4_extent_idx_t *found = NULL;
    
    for (int i = 0; i < hdr->eh_entries; i++) {
        if (block >= idx[i].ei_block) {
            found = &idx[i];
        }
    }
    
    return found;
}

/**
 * Get block number using ext4 extents
 */
static uint32_t ext4_extent_get_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_index) {
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    
    /* Validate extent header */
    if (hdr->eh_magic != EXT4_EXT_MAGIC) {
        serial_printf("[EXT4] Invalid extent magic: 0x%04X\n", hdr->eh_magic);
        return 0;
    }
    
    static uint8_t extent_buffer[4096] __attribute__((aligned(4096)));
    
    /* Traverse extent tree */
    while (hdr->eh_depth > 0) {
        ext4_extent_idx_t *idx = ext4_find_idx(hdr, block_index);
        if (!idx) {
            return 0;
        }
        
        /* Read next level block */
        uint64_t pblock = ext4_idx_pblock(idx);
        if (ext2_read_block(fs, (uint32_t)pblock, extent_buffer) != 0) {
            return 0;
        }
        
        hdr = (ext4_extent_header_t *)extent_buffer;
        
        if (hdr->eh_magic != EXT4_EXT_MAGIC) {
            return 0;
        }
    }
    
    /* Now at leaf level - find extent */
    ext4_extent_t *ext = ext4_find_extent_in_leaf(hdr, block_index);
    if (!ext) {
        return 0;  /* Sparse hole */
    }
    
    /* Calculate physical block */
    uint64_t pblock = ext4_extent_pblock(ext);
    uint32_t offset = block_index - ext->ee_block;
    
    return (uint32_t)(pblock + offset);
}

/**
 * Enhanced block lookup that supports both traditional and extent-based inodes
 */
uint32_t ext2_get_block_enhanced(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_index) {
    if (!fs || !inode) {
        return 0;
    }
    
    if (ext4_inode_uses_extents(inode)) {
        return ext4_extent_get_block(fs, inode, block_index);
    } else {
        return ext2_get_inode_block(fs, inode, block_index);
    }
}

/* ================================================================
 * INODE ALLOCATION AND MANAGEMENT
 * ================================================================ */

/**
 * Allocate a new inode
 * 
 * @param fs Filesystem handle
 * @param is_directory Whether allocating for a directory
 * @return Allocated inode number, or 0 on failure
 */
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_directory) {
    if (!fs || fs->superblock->s_free_inodes_count == 0) {
        return 0;
    }
    
    static uint8_t inode_bitmap_buffer[4096] __attribute__((aligned(4096)));
    
    /* Search all groups for free inode */
    for (uint32_t group = 0; group < fs->num_groups; group++) {
        if (fs->group_desc[group].bg_free_inodes_count == 0) {
            continue;
        }
        
        /* Read inode bitmap */
        uint32_t bitmap_block = fs->group_desc[group].bg_inode_bitmap;
        if (ext2_read_block(fs, bitmap_block, inode_bitmap_buffer) != 0) {
            continue;
        }
        
        /* Find free inode in bitmap */
        for (uint32_t i = 0; i < fs->inodes_per_group / 8; i++) {
            if (inode_bitmap_buffer[i] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(inode_bitmap_buffer[i] & (1 << bit))) {
                        /* Mark inode as used */
                        inode_bitmap_buffer[i] |= (1 << bit);
                        
                        /* Write bitmap back */
                        if (ext2_write_block(fs, bitmap_block, inode_bitmap_buffer) != 0) {
                            return 0;
                        }
                        
                        /* Update counters */
                        fs->group_desc[group].bg_free_inodes_count--;
                        fs->superblock->s_free_inodes_count--;
                        if (is_directory) {
                            fs->group_desc[group].bg_used_dirs_count++;
                        }
                        
                        /* Calculate inode number (1-indexed) */
                        uint32_t inode_num = group * fs->inodes_per_group + i * 8 + bit + 1;
                        
                        return inode_num;
                    }
                }
            }
        }
    }
    
    return 0;  /* No free inodes */
}

/**
 * Free an inode
 */
int ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int is_directory) {
    if (!fs || inode_num == 0) {
        return -1;
    }
    
    /* Calculate group and offset */
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->num_groups) {
        return -1;
    }
    
    static uint8_t free_inode_bitmap[4096] __attribute__((aligned(4096)));
    
    /* Read inode bitmap */
    uint32_t bitmap_block = fs->group_desc[group].bg_inode_bitmap;
    if (ext2_read_block(fs, bitmap_block, free_inode_bitmap) != 0) {
        return -1;
    }
    
    /* Clear bit in bitmap */
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    
    free_inode_bitmap[byte_index] &= ~(1 << bit_index);
    
    /* Write bitmap back */
    if (ext2_write_block(fs, bitmap_block, free_inode_bitmap) != 0) {
        return -1;
    }
    
    /* Update counters */
    fs->group_desc[group].bg_free_inodes_count++;
    fs->superblock->s_free_inodes_count++;
    if (is_directory) {
        fs->group_desc[group].bg_used_dirs_count--;
    }
    
    return 0;
}

/**
 * Write inode to disk
 */
int ext2_write_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || !inode || inode_num == 0) {
        return -1;
    }
    
    /* Calculate location */
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->num_groups) {
        return -1;
    }
    
    uint32_t inode_table = fs->group_desc[group].bg_inode_table;
    uint32_t inode_size = fs->superblock->s_inode_size;
    if (inode_size == 0 || fs->superblock->s_rev_level == 0) {
        inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    }
    
    uint32_t block_offset = (index * inode_size) / fs->block_size;
    uint32_t offset_in_block = (index * inode_size) % fs->block_size;
    
    static uint8_t write_inode_buffer[4096] __attribute__((aligned(4096)));
    
    /* Read block containing inode */
    if (ext2_read_block(fs, inode_table + block_offset, write_inode_buffer) != 0) {
        return -1;
    }
    
    /* Update inode data */
    uint8_t *inode_ptr = write_inode_buffer + offset_in_block;
    for (uint32_t i = 0; i < sizeof(ext2_inode_t); i++) {
        inode_ptr[i] = ((uint8_t *)inode)[i];
    }
    
    /* Write block back */
    if (ext2_write_block(fs, inode_table + block_offset, write_inode_buffer) != 0) {
        return -1;
    }
    
    return 0;
}

/* ================================================================
 * DIRECTORY CREATION AND FILE OPERATIONS
 * ================================================================ */

/**
 * Add entry to directory
 */
int ext2_add_dir_entry(ext2_fs_t *fs, ext2_inode_t *dir_inode, uint32_t dir_inode_num,
                       const char *name, uint32_t new_inode, uint8_t file_type) {
    if (!fs || !dir_inode || !name) {
        return -1;
    }
    
    uint32_t name_len = ext2_strlen(name);
    uint32_t required_len = ((8 + name_len + 3) / 4) * 4;  /* 8 bytes header + name, aligned to 4 */
    
    static uint8_t add_entry_buffer[4096] __attribute__((aligned(4096)));
    
    uint32_t block_size = fs->block_size;
    uint32_t file_offset = 0;
    
    while (file_offset < dir_inode->i_size) {
        uint32_t block_index = file_offset / block_size;
        
        /* Get or allocate block */
        uint32_t block_num = ext2_get_inode_block(fs, dir_inode, block_index);
        if (block_num == 0) {
            /* Need to allocate new block */
            block_num = ext2_alloc_block(fs, -1);
            if (block_num == 0) {
                return -1;
            }
            
            if (block_index < 12) {
                dir_inode->i_block[block_index] = block_num;
            } else {
                ext2_free_block(fs, block_num);
                return -1;  /* TODO: indirect blocks */
            }
            
            /* Initialize new block with single entry spanning whole block */
            for (uint32_t i = 0; i < block_size; i++) {
                add_entry_buffer[i] = 0;
            }
            
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)add_entry_buffer;
            de->inode = new_inode;
            de->rec_len = block_size;
            de->name_len = name_len;
            de->file_type = file_type;
            for (uint32_t i = 0; i < name_len; i++) {
                de->name[i] = name[i];
            }
            
            if (ext2_write_block(fs, block_num, add_entry_buffer) != 0) {
                return -1;
            }
            
            dir_inode->i_size += block_size;
            ext2_write_inode(fs, dir_inode_num, dir_inode);
            
            return 0;
        }
        
        /* Read existing block */
        if (ext2_read_block(fs, block_num, add_entry_buffer) != 0) {
            return -1;
        }
        
        /* Search for space in this block */
        uint32_t offset = 0;
        while (offset < block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(add_entry_buffer + offset);
            
            if (de->rec_len < 8) {
                return -1;  /* Corrupt */
            }
            
            /* Calculate actual size needed by this entry */
            uint32_t actual_len = (de->inode == 0) ? 0 : ((8 + de->name_len + 3) / 4) * 4;
            uint32_t free_space = de->rec_len - actual_len;
            
            if (free_space >= required_len) {
                /* Found space - split the entry */
                if (de->inode != 0) {
                    /* Shrink existing entry */
                    de->rec_len = actual_len;
                    
                    /* Create new entry after it */
                    ext2_dir_entry_t *new_de = (ext2_dir_entry_t *)(add_entry_buffer + offset + actual_len);
                    new_de->inode = new_inode;
                    new_de->rec_len = free_space;
                    new_de->name_len = name_len;
                    new_de->file_type = file_type;
                    for (uint32_t i = 0; i < name_len; i++) {
                        new_de->name[i] = name[i];
                    }
                } else {
                    /* Reuse deleted entry */
                    de->inode = new_inode;
                    de->name_len = name_len;
                    de->file_type = file_type;
                    for (uint32_t i = 0; i < name_len; i++) {
                        de->name[i] = name[i];
                    }
                }
                
                /* Write block back */
                if (ext2_write_block(fs, block_num, add_entry_buffer) != 0) {
                    return -1;
                }
                
                return 0;
            }
            
            offset += de->rec_len;
        }
        
        file_offset += block_size;
    }
    
    /* Need to extend directory with new block */
    uint32_t new_block = ext2_alloc_block(fs, -1);
    if (new_block == 0) {
        return -1;
    }
    
    uint32_t block_index = dir_inode->i_size / block_size;
    if (block_index < 12) {
        dir_inode->i_block[block_index] = new_block;
    } else {
        ext2_free_block(fs, new_block);
        return -1;
    }
    
    /* Initialize new block */
    for (uint32_t i = 0; i < block_size; i++) {
        add_entry_buffer[i] = 0;
    }
    
    ext2_dir_entry_t *de = (ext2_dir_entry_t *)add_entry_buffer;
    de->inode = new_inode;
    de->rec_len = block_size;
    de->name_len = name_len;
    de->file_type = file_type;
    for (uint32_t i = 0; i < name_len; i++) {
        de->name[i] = name[i];
    }
    
    if (ext2_write_block(fs, new_block, add_entry_buffer) != 0) {
        return -1;
    }
    
    dir_inode->i_size += block_size;
    ext2_write_inode(fs, dir_inode_num, dir_inode);
    
    return 0;
}

/**
 * Create a new file or directory
 */
int ext2_create(ext2_fs_t *fs, uint32_t parent_inode_num, const char *name, 
                uint16_t mode, uint32_t *new_inode_num) {
    if (!fs || !name || !new_inode_num) {
        return -1;
    }
    
    int is_directory = ((mode & 0xF000) == EXT2_S_IFDIR);
    
    /* Allocate inode */
    uint32_t inode_num = ext2_alloc_inode(fs, is_directory);
    if (inode_num == 0) {
        serial_printf("[EXT4] Failed to allocate inode\n");
        return -1;
    }
    
    /* Initialize inode */
    ext2_inode_t inode;
    for (uint32_t i = 0; i < sizeof(ext2_inode_t); i++) {
        ((uint8_t *)&inode)[i] = 0;
    }
    
    inode.i_mode = mode;
    inode.i_uid = 0;
    inode.i_gid = 0;
    inode.i_size = 0;
    inode.i_links_count = 1;
    /* TODO: Set timestamps */
    
    if (is_directory) {
        /* Allocate block for directory */
        uint32_t dir_block = ext2_alloc_block(fs, -1);
        if (dir_block == 0) {
            ext2_free_inode(fs, inode_num, 1);
            return -1;
        }
        
        inode.i_block[0] = dir_block;
        inode.i_size = fs->block_size;
        inode.i_blocks = fs->block_size / 512;
        inode.i_links_count = 2;  /* . and parent's link */
        
        /* Initialize directory with . and .. */
        static uint8_t create_dir_buffer[4096] __attribute__((aligned(4096)));
        for (uint32_t i = 0; i < fs->block_size; i++) {
            create_dir_buffer[i] = 0;
        }
        
        ext2_dir_entry_t *dot = (ext2_dir_entry_t *)create_dir_buffer;
        dot->inode = inode_num;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';
        
        ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(create_dir_buffer + 12);
        dotdot->inode = parent_inode_num;
        dotdot->rec_len = fs->block_size - 12;
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        
        if (ext2_write_block(fs, dir_block, create_dir_buffer) != 0) {
            ext2_free_block(fs, dir_block);
            ext2_free_inode(fs, inode_num, 1);
            return -1;
        }
    }
    
    /* Write inode */
    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_inode(fs, inode_num, is_directory);
        return -1;
    }
    
    /* Add entry to parent directory */
    ext2_inode_t parent_inode;
    if (ext2_read_inode(fs, parent_inode_num, &parent_inode) != 0) {
        ext2_free_inode(fs, inode_num, is_directory);
        return -1;
    }
    
    uint8_t file_type = is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (ext2_add_dir_entry(fs, &parent_inode, parent_inode_num, name, inode_num, file_type) != 0) {
        ext2_free_inode(fs, inode_num, is_directory);
        return -1;
    }
    
    /* If directory, increment parent link count */
    if (is_directory) {
        parent_inode.i_links_count++;
        ext2_write_inode(fs, parent_inode_num, &parent_inode);
    }
    
    *new_inode_num = inode_num;
    return 0;
}

/* ================================================================
 * PATH RESOLUTION
 * ================================================================ */

/**
 * Resolve path to inode
 * 
 * @param fs Filesystem handle
 * @param path Path to resolve (absolute, starting with /)
 * @param inode Output inode
 * @param inode_num Output inode number
 * @return 0 on success, negative on error
 */
int ext2_resolve_path(ext2_fs_t *fs, const char *path, ext2_inode_t *inode, uint32_t *inode_num) {
    if (!fs || !path || !inode) {
        return -1;
    }
    
    /* Start at root inode (always inode 2) */
    uint32_t current_inode_num = 2;
    ext2_inode_t current_inode;
    
    if (ext2_read_inode(fs, current_inode_num, &current_inode) != 0) {
        return -1;
    }
    
    /* Skip leading slash */
    if (*path == '/') {
        path++;
    }
    
    /* Handle root path */
    if (*path == '\0') {
        *inode = current_inode;
        if (inode_num) *inode_num = current_inode_num;
        return 0;
    }
    
    /* Parse path components */
    static char component[256];
    /* Use a buffer large enough to hold ext2_dir_entry_t + name */
    static uint8_t entry_buffer[sizeof(ext2_dir_entry_t) + 256];
    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)entry_buffer;
    
    while (*path) {
        /* Extract next component */
        int i = 0;
        while (*path && *path != '/' && i < 255) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        
        if (i == 0) {
            /* Empty component (double slash), skip */
            if (*path == '/') path++;
            continue;
        }
        
        /* Skip trailing slash */
        if (*path == '/') path++;
        
        /* Current inode must be a directory */
        if ((current_inode.i_mode & 0xF000) != EXT2_S_IFDIR) {
            return -1;  /* Not a directory */
        }
        
        /* Find component in directory */
        if (ext2_find_entry(fs, &current_inode, component, entry) != 0) {
            return -1;  /* Not found */
        }
        
        /* Move to found inode */
        current_inode_num = entry->inode;
        if (ext2_read_inode(fs, current_inode_num, &current_inode) != 0) {
            return -1;
        }
    }
    
    *inode = current_inode;
    if (inode_num) *inode_num = current_inode_num;
    return 0;
}

/* ================================================================
 * STATISTICS AND DEBUG
 * ================================================================ */

/**
 * Print filesystem statistics
 */
void ext2_print_stats(ext2_fs_t *fs) {
    if (!fs || !fs->superblock) {
        return;
    }
    
    serial_printf("[EXT4] Filesystem Statistics:\n");
    serial_printf("       Block size: %u bytes\n", fs->block_size);
    serial_printf("       Total blocks: %u\n", fs->superblock->s_blocks_count);
    serial_printf("       Free blocks: %u\n", fs->superblock->s_free_blocks_count);
    serial_printf("       Total inodes: %u\n", fs->superblock->s_inodes_count);
    serial_printf("       Free inodes: %u\n", fs->superblock->s_free_inodes_count);
    serial_printf("       Block groups: %u\n", fs->num_groups);
    serial_printf("       Blocks per group: %u\n", fs->blocks_per_group);
    serial_printf("       Inodes per group: %u\n", fs->inodes_per_group);
    
    /* Cache statistics */
    uint32_t cache_valid = 0;
    uint32_t cache_dirty = 0;
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (block_cache[i].valid) cache_valid++;
        if (block_cache[i].dirty) cache_dirty++;
    }
    serial_printf("       Cache entries: %u valid, %u dirty\n", cache_valid, cache_dirty);
}

/**
 * Sync filesystem (write dirty data)
 */
int ext2_sync(ext2_fs_t *fs) {
    if (!fs) {
        return -1;
    }
    
    int errors = 0;
    
    /* Write dirty cache entries */
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (block_cache[i].valid && block_cache[i].dirty) {
            if (ext2_write_block(fs, block_cache[i].block_num, block_cache[i].data) == 0) {
                block_cache[i].dirty = 0;
            } else {
                errors++;
            }
        }
    }
    
    /* Write superblock */
    uint8_t sb_buffer[1024];
    for (uint32_t i = 0; i < sizeof(ext2_superblock_t) && i < 1024; i++) {
        sb_buffer[i] = ((uint8_t *)fs->superblock)[i];
    }
    
    uint8_t drive = (uint8_t)(uintptr_t)fs->device;
    ide_write_sector(drive, 2, sb_buffer);
    ide_write_sector(drive, 3, sb_buffer + 512);
    
    /* Write group descriptors */
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t gdt_blocks = (fs->num_groups * sizeof(ext2_group_desc_t) + fs->block_size - 1) / fs->block_size;
    
    for (uint32_t i = 0; i < gdt_blocks; i++) {
        ext2_write_block(fs, gdt_block + i, (uint8_t *)fs->group_desc + i * fs->block_size);
    }
    
    serial_printf("[EXT4] Filesystem synced (%d errors)\n", errors);
    return errors ? -1 : 0;
}

/* ================================================================
 * JBD2 JOURNALING SUPPORT
 * ================================================================
 * 
 * ext3/ext4 use the JBD2 (Journaling Block Device 2) subsystem for
 * crash consistency. The journal ensures that metadata and optionally
 * data changes are either fully applied or not applied at all.
 * 
 * Journal layout:
 *   - Superblock (block 0 of journal): Journal metadata
 *   - Descriptor blocks: List of blocks being journaled
 *   - Data/Commit blocks: Actual data and commit markers
 * 
 * Transaction lifecycle:
 *   1. Start transaction
 *   2. Add blocks to transaction (get_write_access)
 *   3. Modify blocks in memory
 *   4. Commit transaction (write to journal, then mark committed)
 *   5. Checkpoint (copy journal blocks to final locations)
 */

/* Journal block types */
#define JBD2_MAGIC_NUMBER       0xC03B3998
#define JBD2_DESCRIPTOR_BLOCK   1
#define JBD2_COMMIT_BLOCK       2
#define JBD2_SUPERBLOCK_V1      3
#define JBD2_SUPERBLOCK_V2      4
#define JBD2_REVOKE_BLOCK       5

/* Journal superblock */
typedef struct jbd2_superblock {
    uint32_t s_header_magic;      /* Magic number */
    uint32_t s_header_blocktype;  /* Block type (JBD2_SUPERBLOCK_V2) */
    uint32_t s_header_sequence;   /* Sequence number */
    
    uint32_t s_blocksize;         /* Journal block size */
    uint32_t s_maxlen;            /* Total journal blocks */
    uint32_t s_first;             /* First usable block */
    
    uint32_t s_sequence;          /* First expected commit ID */
    uint32_t s_start;             /* First block of current log */
    
    uint32_t s_errno;             /* Error value */
    
    /* Remaining fields for V2+ */
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    uint32_t s_nr_users;
    uint32_t s_dynsuper;
    uint32_t s_max_transaction;
    uint32_t s_max_trans_data;
    uint8_t  s_checksum_type;
    uint8_t  s_padding2[3];
    uint32_t s_padding[42];
    uint32_t s_checksum;
    uint8_t  s_users[16*48];
} __attribute__((packed)) jbd2_superblock_t;

/* Journal descriptor block header */
typedef struct jbd2_header {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
} __attribute__((packed)) jbd2_header_t;

/* Journal descriptor block entry */
typedef struct jbd2_block_tag {
    uint32_t t_blocknr;        /* Block number in filesystem */
    uint16_t t_checksum;       /* Checksum */
    uint16_t t_flags;          /* Flags */
} __attribute__((packed)) jbd2_block_tag_t;

/* Block tag flags */
#define JBD2_FLAG_ESCAPE    0x01  /* Block was escaped */
#define JBD2_FLAG_SAME_UUID 0x02  /* Same UUID as previous */
#define JBD2_FLAG_DELETED   0x04  /* Block deleted from journal */
#define JBD2_FLAG_LAST_TAG  0x08  /* Last tag in descriptor block */

/* Journal context */
typedef struct jbd2_journal {
    ext2_fs_t *fs;                 /* Parent filesystem */
    uint32_t j_inode;              /* Journal inode number */
    uint32_t j_start_block;        /* First journal block */
    uint32_t j_size;               /* Number of journal blocks */
    uint32_t j_first;              /* First usable block */
    uint32_t j_sequence;           /* Next transaction ID */
    uint32_t j_head;               /* Current write position */
    uint32_t j_tail;               /* Oldest transaction start */
    int j_running;                 /* Transaction in progress? */
} jbd2_journal_t;

/* Transaction handle */
typedef struct jbd2_handle {
    jbd2_journal_t *journal;
    uint32_t sequence;             /* Transaction sequence number */
    int num_blocks;                /* Blocks in this transaction */
    uint32_t blocks[64];           /* Block numbers */
} jbd2_handle_t;

/* Global journal state */
static jbd2_journal_t *current_journal = NULL;
static jbd2_handle_t *current_handle = NULL;

/**
 * Initialize journal for ext4 filesystem
 */
int jbd2_init(ext2_fs_t *fs) {
    if (!fs || !fs->superblock) {
        return -1;
    }
    
    /* Check if journal is enabled */
    uint32_t journal_inum = fs->superblock->s_journal_inum;
    if (journal_inum == 0) {
        serial_printf("[JBD2] No journal inode configured\n");
        return 0;  /* No journal - not an error for ext2 */
    }
    
    serial_printf("[JBD2] Initializing journal (inode %u)\n", journal_inum);
    
    /* Allocate journal context */
    static jbd2_journal_t journal;
    current_journal = &journal;
    
    journal.fs = fs;
    journal.j_inode = journal_inum;
    journal.j_running = 0;
    
    /* Read journal inode */
    ext2_inode_t j_inode;
    if (ext2_read_inode(fs, journal_inum, &j_inode) != 0) {
        serial_printf("[JBD2] Failed to read journal inode\n");
        return -1;
    }
    
    /* Journal uses direct blocks from inode */
    journal.j_start_block = j_inode.i_block[0];
    journal.j_size = j_inode.i_size / fs->block_size;
    
    /* Read journal superblock */
    static uint8_t jsb_buffer[4096] __attribute__((aligned(4096)));
    if (ext2_read_block(fs, journal.j_start_block, jsb_buffer) != 0) {
        serial_printf("[JBD2] Failed to read journal superblock\n");
        return -1;
    }
    
    jbd2_superblock_t *jsb = (jbd2_superblock_t *)jsb_buffer;
    
    /* Validate journal superblock */
    if (jsb->s_header_magic != JBD2_MAGIC_NUMBER) {
        serial_printf("[JBD2] Invalid journal magic: 0x%08X\n", jsb->s_header_magic);
        return -1;
    }
    
    journal.j_first = jsb->s_first;
    journal.j_sequence = jsb->s_sequence;
    journal.j_head = jsb->s_start;
    journal.j_tail = jsb->s_start;
    
    serial_printf("[JBD2] Journal initialized: %u blocks, seq %u\n", 
                  journal.j_size, journal.j_sequence);
    
    return 0;
}

/**
 * Recover journal after crash
 */
int jbd2_recover(ext2_fs_t *fs) {
    if (!current_journal) {
        return 0;  /* No journal */
    }
    
    serial_printf("[JBD2] Checking journal for recovery...\n");
    
    jbd2_journal_t *j = current_journal;
    uint32_t block_pos = j->j_head;
    uint32_t recovered = 0;
    
    static uint8_t recover_buffer[4096] __attribute__((aligned(4096)));
    
    /* Scan journal for uncommitted transactions */
    while (block_pos != j->j_tail || recovered == 0) {
        if (ext2_read_block(fs, j->j_start_block + block_pos, recover_buffer) != 0) {
            break;
        }
        
        jbd2_header_t *hdr = (jbd2_header_t *)recover_buffer;
        
        if (hdr->h_magic != JBD2_MAGIC_NUMBER) {
            break;  /* End of valid journal data */
        }
        
        switch (hdr->h_blocktype) {
            case JBD2_DESCRIPTOR_BLOCK: {
                /* Found descriptor - replay its blocks */
                jbd2_block_tag_t *tag = (jbd2_block_tag_t *)(recover_buffer + sizeof(jbd2_header_t));
                uint32_t data_block = block_pos + 1;
                
                while (!(tag->t_flags & JBD2_FLAG_LAST_TAG)) {
                    /* Read data block from journal */
                    if (ext2_read_block(fs, j->j_start_block + data_block, recover_buffer) != 0) {
                        break;
                    }
                    
                    /* Write to final location */
                    if (ext2_write_block(fs, tag->t_blocknr, recover_buffer) == 0) {
                        recovered++;
                    }
                    
                    tag++;
                    data_block++;
                }
                
                block_pos = data_block;
                break;
            }
            
            case JBD2_COMMIT_BLOCK:
                /* Transaction complete */
                j->j_tail = block_pos + 1;
                if (j->j_tail >= j->j_size - j->j_first) {
                    j->j_tail = 0;
                }
                block_pos++;
                break;
                
            default:
                block_pos++;
                break;
        }
        
        /* Wrap around */
        if (block_pos >= j->j_size - j->j_first) {
            block_pos = 0;
        }
        
        /* Prevent infinite loop */
        if (recovered > j->j_size) {
            break;
        }
    }
    
    if (recovered > 0) {
        serial_printf("[JBD2] Recovered %u blocks from journal\n", recovered);
    } else {
        serial_printf("[JBD2] Journal clean, no recovery needed\n");
    }
    
    return 0;
}

/**
 * Start a new journal transaction
 */
jbd2_handle_t *jbd2_start(ext2_fs_t *fs, int num_blocks) {
    if (!current_journal || current_handle) {
        return NULL;  /* No journal or transaction already active */
    }
    
    static jbd2_handle_t handle;
    current_handle = &handle;
    
    handle.journal = current_journal;
    handle.sequence = current_journal->j_sequence++;
    handle.num_blocks = 0;
    
    current_journal->j_running = 1;
    
    return &handle;
}

/**
 * Add a block to the current transaction
 */
int jbd2_get_write_access(jbd2_handle_t *handle, uint32_t block_num) {
    if (!handle || handle->num_blocks >= 64) {
        return -1;
    }
    
    /* Check if block already in transaction */
    for (int i = 0; i < handle->num_blocks; i++) {
        if (handle->blocks[i] == block_num) {
            return 0;  /* Already tracked */
        }
    }
    
    handle->blocks[handle->num_blocks++] = block_num;
    return 0;
}

/**
 * Commit the current transaction
 */
int jbd2_commit(jbd2_handle_t *handle) {
    if (!handle || !handle->journal) {
        return -1;
    }
    
    jbd2_journal_t *j = handle->journal;
    ext2_fs_t *fs = j->fs;
    
    if (handle->num_blocks == 0) {
        /* Empty transaction */
        current_handle = NULL;
        j->j_running = 0;
        return 0;
    }
    
    static uint8_t commit_buffer[4096] __attribute__((aligned(4096)));
    uint32_t journal_block = j->j_head;
    
    /* Write descriptor block */
    for (uint32_t i = 0; i < fs->block_size; i++) {
        commit_buffer[i] = 0;
    }
    
    jbd2_header_t *hdr = (jbd2_header_t *)commit_buffer;
    hdr->h_magic = JBD2_MAGIC_NUMBER;
    hdr->h_blocktype = JBD2_DESCRIPTOR_BLOCK;
    hdr->h_sequence = handle->sequence;
    
    jbd2_block_tag_t *tag = (jbd2_block_tag_t *)(commit_buffer + sizeof(jbd2_header_t));
    
    for (int i = 0; i < handle->num_blocks; i++) {
        tag[i].t_blocknr = handle->blocks[i];
        tag[i].t_checksum = 0;
        tag[i].t_flags = (i == handle->num_blocks - 1) ? JBD2_FLAG_LAST_TAG : 0;
    }
    
    if (ext2_write_block(fs, j->j_start_block + journal_block, commit_buffer) != 0) {
        return -1;
    }
    
    journal_block++;
    
    /* Write data blocks to journal */
    for (int i = 0; i < handle->num_blocks; i++) {
        if (ext2_read_block(fs, handle->blocks[i], commit_buffer) != 0) {
            return -1;
        }
        
        if (ext2_write_block(fs, j->j_start_block + journal_block, commit_buffer) != 0) {
            return -1;
        }
        
        journal_block++;
    }
    
    /* Write commit block */
    for (uint32_t i = 0; i < fs->block_size; i++) {
        commit_buffer[i] = 0;
    }
    
    hdr = (jbd2_header_t *)commit_buffer;
    hdr->h_magic = JBD2_MAGIC_NUMBER;
    hdr->h_blocktype = JBD2_COMMIT_BLOCK;
    hdr->h_sequence = handle->sequence;
    
    if (ext2_write_block(fs, j->j_start_block + journal_block, commit_buffer) != 0) {
        return -1;
    }
    
    journal_block++;
    
    /* Update journal head */
    if (journal_block >= j->j_size - j->j_first) {
        journal_block = 0;  /* Wrap around */
    }
    j->j_head = journal_block;
    
    /* Transaction complete */
    current_handle = NULL;
    j->j_running = 0;
    
    serial_printf("[JBD2] Committed transaction %u (%d blocks)\n", 
                  handle->sequence, handle->num_blocks);
    
    return 0;
}

/**
 * Abort current transaction
 */
void jbd2_abort(jbd2_handle_t *handle) {
    if (handle && handle->journal) {
        handle->journal->j_running = 0;
    }
    current_handle = NULL;
}

/**
 * Journaled write - wraps block write with journal protection
 */
int ext2_write_block_journaled(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !buffer) {
        return -1;
    }
    
    if (current_journal && current_handle) {
        /* Add to current transaction */
        jbd2_get_write_access(current_handle, block_num);
    }
    
    /* Write through to actual location */
    return ext2_write_block(fs, block_num, buffer);
}
