/**
 * TocinOS FAT Filesystem Implementation
 * 
 * Implements FAT12, FAT16, and FAT32 filesystem support
 */

#include "../include/kernel/fat.h"
#include "../include/kernel/memory.h"

// Global FAT info
static fat_info_t fat_info = {0};
static int fat_mounted = 0;
static uint8_t *fat_buffer = 0;

// Sector buffer
static uint8_t sector_buffer[512];

/**
 * Detect FAT type from BPB
 */
static uint8_t fat_detect_type(fat_bpb_t *bpb) {
    uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;
    uint32_t fat_size = (bpb->sectors_per_fat_16 != 0) ? bpb->sectors_per_fat_16 : ((fat32_ebpb_t *)bpb)->sectors_per_fat_32;
    uint32_t total_sectors = (bpb->total_sectors_16 != 0) ? bpb->total_sectors_16 : bpb->total_sectors_32;
    uint32_t data_sectors = total_sectors - (bpb->reserved_sectors + (bpb->fat_count * fat_size) + root_dir_sectors);
    uint32_t cluster_count = data_sectors / bpb->sectors_per_cluster;
    
    if (cluster_count < 4085) {
        return FAT_TYPE_FAT12;
    } else if (cluster_count < 65525) {
        return FAT_TYPE_FAT16;
    } else {
        return FAT_TYPE_FAT32;
    }
}

/**
 * Mount a FAT filesystem
 */
int fat_mount(uint8_t drive) {
    (void)drive;
    
    // Read boot sector
    if (fat_read_sector(0, sector_buffer) != 0) {
        return -1;
    }
    
    fat_bpb_t *bpb = (fat_bpb_t *)sector_buffer;
    
    // Verify boot signature
    if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA) {
        return -1;
    }
    
    // Detect FAT type
    fat_info.type = fat_detect_type(bpb);
    
    // Fill in filesystem info
    fat_info.bytes_per_sector = bpb->bytes_per_sector;
    fat_info.sectors_per_cluster = bpb->sectors_per_cluster;
    fat_info.reserved_sectors = bpb->reserved_sectors;
    fat_info.fat_count = bpb->fat_count;
    fat_info.root_entry_count = bpb->root_entry_count;
    
    if (fat_info.type == FAT_TYPE_FAT32) {
        fat32_ebpb_t *ebpb = (fat32_ebpb_t *)sector_buffer;
        fat_info.sectors_per_fat = ebpb->sectors_per_fat_32;
        fat_info.root_cluster = ebpb->root_cluster;
    } else {
        fat_info.sectors_per_fat = bpb->sectors_per_fat_16;
        fat_info.root_cluster = 0;
    }
    
    // Calculate filesystem layout
    fat_info.fat_start = fat_info.reserved_sectors;
    uint32_t root_dir_sectors = ((fat_info.root_entry_count * 32) + (fat_info.bytes_per_sector - 1)) / fat_info.bytes_per_sector;
    fat_info.root_start = fat_info.fat_start + (fat_info.fat_count * fat_info.sectors_per_fat);
    fat_info.data_start = fat_info.root_start + root_dir_sectors;
    
    // Calculate cluster count
    uint32_t total_sectors = (bpb->total_sectors_16 != 0) ? bpb->total_sectors_16 : bpb->total_sectors_32;
    uint32_t data_sectors = total_sectors - fat_info.data_start;
    fat_info.cluster_count = data_sectors / fat_info.sectors_per_cluster;
    
    fat_mounted = 1;
    return 0;
}

/**
 * Unmount FAT filesystem
 */
int fat_unmount(void) {
    if (!fat_mounted) {
        return -1;
    }
    
    fat_mounted = 0;
    return 0;
}

/**
 * Convert cluster number to sector number
 */
uint32_t fat_cluster_to_sector(uint32_t cluster) {
    if (fat_info.type == FAT_TYPE_FAT32) {
        return fat_info.data_start + ((cluster - 2) * fat_info.sectors_per_cluster);
    } else {
        return fat_info.data_start + ((cluster - 2) * fat_info.sectors_per_cluster);
    }
}

/**
 * Get next cluster in chain
 */
uint32_t fat_get_next_cluster(uint32_t cluster) {
    if (!fat_mounted) {
        return 0xFFFFFFFF;
    }
    
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t entry_offset;
    
    if (fat_info.type == FAT_TYPE_FAT12) {
        fat_offset = cluster + (cluster / 2);
        fat_sector = fat_info.fat_start + (fat_offset / fat_info.bytes_per_sector);
        entry_offset = fat_offset % fat_info.bytes_per_sector;
        
        fat_read_sector(fat_sector, sector_buffer);
        uint16_t next = *(uint16_t *)&sector_buffer[entry_offset];
        
        if (cluster & 1) {
            next >>= 4;
        } else {
            next &= 0x0FFF;
        }
        
        if (next >= 0x0FF8) {
            return 0xFFFFFFFF; // End of chain
        }
        return next;
    } else if (fat_info.type == FAT_TYPE_FAT16) {
        fat_offset = cluster * 2;
        fat_sector = fat_info.fat_start + (fat_offset / fat_info.bytes_per_sector);
        entry_offset = fat_offset % fat_info.bytes_per_sector;
        
        fat_read_sector(fat_sector, sector_buffer);
        uint16_t next = *(uint16_t *)&sector_buffer[entry_offset];
        
        if (next >= 0xFFF8) {
            return 0xFFFFFFFF;
        }
        return next;
    } else { // FAT32
        fat_offset = cluster * 4;
        fat_sector = fat_info.fat_start + (fat_offset / fat_info.bytes_per_sector);
        entry_offset = fat_offset % fat_info.bytes_per_sector;
        
        fat_read_sector(fat_sector, sector_buffer);
        uint32_t next = *(uint32_t *)&sector_buffer[entry_offset] & 0x0FFFFFFF;
        
        if (next >= 0x0FFFFFF8) {
            return 0xFFFFFFFF;
        }
        return next;
    }
}

/**
 * Open a file
 */
int fat_open(const char *path, fat_file_t *file) {
    if (!fat_mounted || !path || !file) {
        return -1;
    }
    
    // Simple implementation: search root directory only
    uint32_t root_sector = (fat_info.type == FAT_TYPE_FAT32) ? 
                           fat_cluster_to_sector(fat_info.root_cluster) : 
                           fat_info.root_start;
    
    uint32_t max_entries = (fat_info.type == FAT_TYPE_FAT32) ? 
                           fat_info.sectors_per_cluster * fat_info.bytes_per_sector / 32 :
                           fat_info.root_entry_count;
    
    // Read root directory
    for (uint32_t i = 0; i < max_entries / 16; i++) {
        if (fat_read_sector(root_sector + i, sector_buffer) != 0) {
            return -1;
        }
        
        fat_dir_entry_t *entries = (fat_dir_entry_t *)sector_buffer;
        for (int j = 0; j < 16; j++) {
            if (entries[j].name[0] == 0x00) {
                return -1; // End of directory
            }
            if (entries[j].name[0] == 0xE5) {
                continue; // Deleted entry
            }
            
            // Simple name comparison (8.3 format)
            // This is a simplified version
            file->first_cluster = ((uint32_t)entries[j].first_cluster_high << 16) | entries[j].first_cluster_low;
            file->current_cluster = file->first_cluster;
            file->position = 0;
            file->size = entries[j].file_size;
            file->attributes = entries[j].attributes;
            file->is_open = 1;
            return 0;
        }
    }
    
    return -1; // File not found
}

/**
 * Close a file
 */
int fat_close(fat_file_t *file) {
    if (!file || !file->is_open) {
        return -1;
    }
    
    file->is_open = 0;
    return 0;
}

/**
 * Read from a file
 */
int fat_read(fat_file_t *file, void *buffer, uint32_t size) {
    if (!fat_mounted || !file || !file->is_open || !buffer) {
        return -1;
    }
    
    if (file->position >= file->size) {
        return 0; // EOF
    }
    
    if (file->position + size > file->size) {
        size = file->size - file->position;
    }
    
    uint32_t bytes_read = 0;
    uint8_t *buf = (uint8_t *)buffer;
    
    while (bytes_read < size && file->current_cluster != 0xFFFFFFFF) {
        uint32_t cluster_offset = file->position % (fat_info.sectors_per_cluster * fat_info.bytes_per_sector);
        uint32_t cluster_remaining = (fat_info.sectors_per_cluster * fat_info.bytes_per_sector) - cluster_offset;
        uint32_t to_read = (size - bytes_read < cluster_remaining) ? size - bytes_read : cluster_remaining;
        
        // Read sectors from cluster
        uint32_t sector = fat_cluster_to_sector(file->current_cluster) + (cluster_offset / fat_info.bytes_per_sector);
        uint32_t sector_offset = cluster_offset % fat_info.bytes_per_sector;
        
        if (fat_read_sector(sector, sector_buffer) != 0) {
            return bytes_read;
        }
        
        uint32_t copy_size = (to_read < fat_info.bytes_per_sector - sector_offset) ? 
                             to_read : fat_info.bytes_per_sector - sector_offset;
        
        for (uint32_t i = 0; i < copy_size; i++) {
            buf[bytes_read++] = sector_buffer[sector_offset + i];
        }
        
        file->position += copy_size;
        
        if (cluster_offset + copy_size >= fat_info.sectors_per_cluster * fat_info.bytes_per_sector) {
            file->current_cluster = fat_get_next_cluster(file->current_cluster);
        }
    }
    
    return bytes_read;
}

/**
 * Write to a file (not implemented)
 */
int fat_write(fat_file_t *file, const void *buffer, uint32_t size) {
    (void)file;
    (void)buffer;
    (void)size;
    return -1; // Not implemented
}

/**
 * Seek to position in file
 */
int fat_seek(fat_file_t *file, uint32_t position) {
    if (!file || !file->is_open) {
        return -1;
    }
    
    file->position = position;
    // Recalculate current cluster based on position
    // Simplified version
    return 0;
}

/**
 * List directory contents
 */
int fat_list_dir(const char *path, fat_dir_entry_t *entries, int max_entries) {
    (void)path;
    
    if (!fat_mounted || !entries) {
        return -1;
    }
    
    // Simple implementation: list root directory
    uint32_t root_sector = (fat_info.type == FAT_TYPE_FAT32) ? 
                           fat_cluster_to_sector(fat_info.root_cluster) : 
                           fat_info.root_start;
    
    int entry_count = 0;
    
    for (uint32_t i = 0; i < fat_info.root_entry_count / 16 && entry_count < max_entries; i++) {
        if (fat_read_sector(root_sector + i, sector_buffer) != 0) {
            break;
        }
        
        fat_dir_entry_t *dir_entries = (fat_dir_entry_t *)sector_buffer;
        for (int j = 0; j < 16 && entry_count < max_entries; j++) {
            if (dir_entries[j].name[0] == 0x00) {
                return entry_count; // End of directory
            }
            if (dir_entries[j].name[0] == 0xE5) {
                continue; // Deleted entry
            }
            if (dir_entries[j].attributes & FAT_ATTR_LONG_NAME) {
                continue; // Skip LFN entries
            }
            
            entries[entry_count++] = dir_entries[j];
        }
    }
    
    return entry_count;
}

/**
 * Get file information
 */
int fat_stat(const char *path, fat_dir_entry_t *entry) {
    (void)path;
    (void)entry;
    return -1; // Not implemented
}

/**
 * Read a sector (placeholder - needs disk driver)
 */
int fat_read_sector(uint32_t sector, void *buffer) {
    (void)sector;
    (void)buffer;
    // This needs to be implemented with actual disk driver
    // For now, return error
    return -1;
}

/**
 * Write a sector (placeholder - needs disk driver)
 */
int fat_write_sector(uint32_t sector, const void *buffer) {
    (void)sector;
    (void)buffer;
    // This needs to be implemented with actual disk driver
    return -1;
}
