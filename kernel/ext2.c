/**
 * TocinOS ext2/3/4 Filesystem Implementation
 * 
 * Implementation of Linux ext2/3/4 filesystem support
 */

#include "../include/kernel/ext2.h"
#include "../include/kernel/memory.h"

static int ext2_initialized = 0;

/**
 * Initialize ext2 filesystem support
 */
int ext2_init(void) {
    if (ext2_initialized) {
        return 0;
    }
    
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
 * Read block from device
 */
int ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !buffer) {
        return -1;
    }
    
    // TODO: Read block from device
    // uint32_t sector = block_num * (fs->block_size / 512);
    // return device_read(fs->device, sector, fs->block_size / 512, buffer);
    
    return -1;
}

/**
 * Write block to device
 */
int ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !buffer) {
        return -1;
    }
    
    // TODO: Write block to device
    // uint32_t sector = block_num * (fs->block_size / 512);
    // return device_write(fs->device, sector, fs->block_size / 512, buffer);
    
    return -1;
}

/**
 * Mount ext2 filesystem
 */
int ext2_mount(const char *device, ext2_fs_t **fs) {
    if (!ext2_initialized || !device || !fs) {
        return -1;
    }
    
    // Allocate filesystem structure
    ext2_fs_t *new_fs = (ext2_fs_t *)pmm_alloc_page();
    if (!new_fs) {
        return -1;
    }
    
    // Allocate superblock
    new_fs->superblock = (ext2_superblock_t *)pmm_alloc_page();
    if (!new_fs->superblock) {
        pmm_free_page(new_fs);
        return -1;
    }
    
    // Read superblock (at offset 1024 bytes)
    // TODO: Read from device
    // For now, we'll just initialize to zero
    for (int i = 0; i < sizeof(ext2_superblock_t); i++) {
        ((uint8_t *)new_fs->superblock)[i] = 0;
    }
    
    // Validate superblock
    if (new_fs->superblock->s_magic != EXT2_SUPER_MAGIC) {
        pmm_free_page(new_fs->superblock);
        pmm_free_page(new_fs);
        return -1;
    }
    
    // Calculate filesystem parameters
    new_fs->block_size = ext2_get_block_size(new_fs->superblock);
    new_fs->inodes_per_group = new_fs->superblock->s_inodes_per_group;
    new_fs->blocks_per_group = new_fs->superblock->s_blocks_per_group;
    new_fs->num_groups = (new_fs->superblock->s_blocks_count + new_fs->blocks_per_group - 1) / new_fs->blocks_per_group;
    
    // Allocate group descriptors
    uint32_t gdt_size = new_fs->num_groups * sizeof(ext2_group_desc_t);
    new_fs->group_desc = (ext2_group_desc_t *)pmm_alloc_page();
    if (!new_fs->group_desc) {
        pmm_free_page(new_fs->superblock);
        pmm_free_page(new_fs);
        return -1;
    }
    
    // Read group descriptors
    // TODO: Read from device
    
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
    
    // Free allocated memory
    if (fs->group_desc) {
        pmm_free_page(fs->group_desc);
    }
    
    if (fs->superblock) {
        pmm_free_page(fs->superblock);
    }
    
    pmm_free_page(fs);
    return 0;
}

/**
 * Read inode from disk
 */
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || !inode || inode_num == 0) {
        return -1;
    }
    
    // Calculate which block group contains the inode
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->num_groups) {
        return -1;
    }
    
    // Get inode table block
    uint32_t inode_table = fs->group_desc[group].bg_inode_table;
    
    // Calculate inode offset
    uint32_t inode_size = fs->superblock->s_inode_size;
    if (inode_size == 0) {
        inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    }
    
    uint32_t block_offset = (index * inode_size) / fs->block_size;
    uint32_t offset_in_block = (index * inode_size) % fs->block_size;
    
    // Read block containing inode
    void *block_buffer = pmm_alloc_page();
    if (!block_buffer) {
        return -1;
    }
    
    if (ext2_read_block(fs, inode_table + block_offset, block_buffer) != 0) {
        pmm_free_page(block_buffer);
        return -1;
    }
    
    // Copy inode data
    uint8_t *inode_ptr = (uint8_t *)block_buffer + offset_in_block;
    for (uint32_t i = 0; i < sizeof(ext2_inode_t); i++) {
        ((uint8_t *)inode)[i] = inode_ptr[i];
    }
    
    pmm_free_page(block_buffer);
    return 0;
}

/**
 * Get block number for file block index
 */
uint32_t ext2_get_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_index) {
    if (!fs || !inode) {
        return 0;
    }
    
    // Direct blocks (0-11)
    if (block_index < 12) {
        return inode->i_block[block_index];
    }
    
    // Single indirect block (12)
    uint32_t ptrs_per_block = fs->block_size / 4;
    if (block_index < 12 + ptrs_per_block) {
        // TODO: Read indirect block
        return 0;
    }
    
    // Double indirect block (13)
    if (block_index < 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block) {
        // TODO: Read double indirect block
        return 0;
    }
    
    // Triple indirect block (14)
    // TODO: Read triple indirect block
    
    return 0;
}

/**
 * Read file data
 */
int ext2_read_file(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, void *buffer) {
    if (!fs || !inode || !buffer) {
        return -1;
    }
    
    // Check if offset is within file
    if (offset >= inode->i_size) {
        return 0;
    }
    
    // Adjust size if reading past end of file
    if (offset + size > inode->i_size) {
        size = inode->i_size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint32_t block_size = fs->block_size;
    void *block_buffer = pmm_alloc_page();
    
    if (!block_buffer) {
        return -1;
    }
    
    while (bytes_read < size) {
        uint32_t current_block = (offset + bytes_read) / block_size;
        uint32_t offset_in_block = (offset + bytes_read) % block_size;
        uint32_t bytes_to_read = block_size - offset_in_block;
        
        if (bytes_to_read > size - bytes_read) {
            bytes_to_read = size - bytes_read;
        }
        
        // Get block number
        uint32_t block_num = ext2_get_inode_block(fs, inode, current_block);
        if (block_num == 0) {
            break;
        }
        
        // Read block
        if (ext2_read_block(fs, block_num, block_buffer) != 0) {
            break;
        }
        
        // Copy data
        uint8_t *src = (uint8_t *)block_buffer + offset_in_block;
        uint8_t *dst = (uint8_t *)buffer + bytes_read;
        for (uint32_t i = 0; i < bytes_to_read; i++) {
            dst[i] = src[i];
        }
        
        bytes_read += bytes_to_read;
    }
    
    pmm_free_page(block_buffer);
    return bytes_read;
}

/**
 * Write file data
 */
int ext2_write_file(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, const void *buffer) {
    if (!fs || !inode || !buffer) {
        return -1;
    }
    
    // TODO: Implement file writing
    
    return -1;
}

/**
 * Read directory entry
 */
int ext2_read_dir(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t index, ext2_dir_entry_t *entry) {
    if (!fs || !inode || !entry) {
        return -1;
    }
    
    // Check if inode is a directory
    if ((inode->i_mode & 0xF000) != EXT2_S_IFDIR) {
        return -1;
    }
    
    // TODO: Read directory entries
    
    return -1;
}

/**
 * Find directory entry by name
 */
int ext2_find_entry(ext2_fs_t *fs, ext2_inode_t *inode, const char *name, ext2_dir_entry_t *entry) {
    if (!fs || !inode || !name || !entry) {
        return -1;
    }
    
    // Check if inode is a directory
    if ((inode->i_mode & 0xF000) != EXT2_S_IFDIR) {
        return -1;
    }
    
    // TODO: Search directory for entry
    
    return -1;
}
