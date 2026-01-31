/**
 * TocinOS FAT Filesystem Implementation
 * 
 * Implements FAT12, FAT16, and FAT32 filesystem support
 */

#include "../include/kernel/fat.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/serial.h"
#include "../include/drivers/ide.h"

// Global FAT info
static fat_info_t fat_info = {0};
static int fat_mounted = 0;
static uint8_t fat_current_drive = 0;
static uint32_t fat_partition_offset = 0;  // Starting sector of partition

// Sector buffer
static uint8_t sector_buffer[512];

// MBR partition entry structure
typedef struct {
    uint8_t  status;           // 0x80 = active/bootable
    uint8_t  chs_first[3];     // CHS address of first sector
    uint8_t  type;             // Partition type
    uint8_t  chs_last[3];      // CHS address of last sector
    uint32_t lba_first;        // LBA of first sector
    uint32_t sector_count;     // Number of sectors
} __attribute__((packed)) mbr_partition_t;

// MBR structure
typedef struct {
    uint8_t         bootstrap[446];
    mbr_partition_t partitions[4];
    uint16_t        signature;      // 0xAA55
} __attribute__((packed)) mbr_t;

// FAT partition type codes
#define PART_TYPE_FAT12       0x01
#define PART_TYPE_FAT16_SMALL 0x04
#define PART_TYPE_FAT16       0x06
#define PART_TYPE_FAT32       0x0B
#define PART_TYPE_FAT32_LBA   0x0C
#define PART_TYPE_FAT16_LBA   0x0E

/**
 * Check if partition type is FAT
 */
static int is_fat_partition(uint8_t type) {
    return (type == PART_TYPE_FAT12 ||
            type == PART_TYPE_FAT16_SMALL ||
            type == PART_TYPE_FAT16 ||
            type == PART_TYPE_FAT32 ||
            type == PART_TYPE_FAT32_LBA ||
            type == PART_TYPE_FAT16_LBA);
}

/**
 * Detect and mount first FAT partition from MBR
 * Returns partition start LBA, or 0 if no partition found (try raw disk)
 */
static uint32_t fat_find_partition(uint8_t drive) {
    // Read MBR (sector 0)
    if (ide_read_sector(drive, 0, sector_buffer) <= 0) {
        return 0;
    }
    
    // Check MBR signature
    if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA) {
        return 0; // Invalid MBR, try as raw FAT image
    }
    
    mbr_t *mbr = (mbr_t *)sector_buffer;
    
    // Check if this is already a FAT boot sector (no MBR)
    // FAT boot sectors have "FAT" at offset 0x36 (FAT12/16) or 0x52 (FAT32)
    if ((sector_buffer[0x36] == 'F' && sector_buffer[0x37] == 'A' && sector_buffer[0x38] == 'T') ||
        (sector_buffer[0x52] == 'F' && sector_buffer[0x53] == 'A' && sector_buffer[0x54] == 'T')) {
        return 0; // Already a FAT boot sector, use as-is
    }
    
    // Search partition table for FAT partition
    for (int i = 0; i < 4; i++) {
        if (is_fat_partition(mbr->partitions[i].type) && 
            mbr->partitions[i].lba_first > 0) {
            return mbr->partitions[i].lba_first;
        }
    }
    
    return 0; // No FAT partition found, try as raw FAT image
}

/**
 * Initialize FAT on drive - auto-detect partitions
 */
int fat_init(uint8_t drive) {
    serial_printf("[FAT] Initializing FAT on drive %u...\n", (uint32_t)drive);
    
    // Try to find FAT partition
    uint32_t partition_start = fat_find_partition(drive);
    
    serial_printf("[FAT] Partition offset: %u sectors\n", partition_start);
    
    // Set partition offset
    fat_set_partition_offset(partition_start);
    
    // Mount FAT filesystem
    int result = fat_mount(drive);
    if (result == 0) {
        serial_printf("[FAT] Mount successful!\n");
    } else {
        serial_printf("[FAT] Mount failed!\n");
    }
    
    return result;
}

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
    // Store current drive
    fat_current_drive = drive;
    
    // Read boot sector (sector 0 of the partition)
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
 * Convert character to uppercase
 */
static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 32;
    }
    return c;
}

/**
 * Convert a regular filename to FAT 8.3 format
 * Input: "hello.txt" or "/hello.txt"
 * Output: "HELLO   TXT" (11 chars, space-padded)
 */
static void fat_name_to_83(const char *name, char *out83) {
    // Skip leading slash
    if (name[0] == '/') {
        name++;
    }
    
    // Initialize with spaces
    for (int i = 0; i < 11; i++) {
        out83[i] = ' ';
    }
    
    // Find the dot (if any)
    int dot_pos = -1;
    for (int i = 0; name[i] && i < 12; i++) {
        if (name[i] == '.') {
            dot_pos = i;
            break;
        }
    }
    
    // Copy base name (up to 8 chars)
    int copy_len = (dot_pos >= 0) ? dot_pos : 8;
    for (int i = 0; i < 8 && i < copy_len && name[i]; i++) {
        out83[i] = to_upper(name[i]);
    }
    
    // Copy extension (up to 3 chars)
    if (dot_pos >= 0 && name[dot_pos + 1]) {
        const char *ext = &name[dot_pos + 1];
        for (int i = 0; i < 3 && ext[i]; i++) {
            out83[8 + i] = to_upper(ext[i]);
        }
    }
}

/**
 * Compare two 8.3 filenames
 * Returns 0 if equal, non-zero otherwise
 */
static int fat_compare_83(const char *a, const char *b) {
    for (int i = 0; i < 11; i++) {
        if (a[i] != b[i]) {
            return 1;
        }
    }
    return 0;
}

/**
 * Open a file
 */
int fat_open(const char *path, fat_file_t *file) {
    if (!fat_mounted || !path || !file) {
        return -1;
    }
    
    // Convert input path to 8.3 format
    char name83[12];
    fat_name_to_83(path, name83);
    name83[11] = 0;
    
    // Simple implementation: search root directory only
    uint32_t root_sector = (fat_info.type == FAT_TYPE_FAT32) ? 
                           fat_cluster_to_sector(fat_info.root_cluster) : 
                           fat_info.root_start;
    
    uint32_t max_entries = (fat_info.type == FAT_TYPE_FAT32) ? 
                           fat_info.sectors_per_cluster * fat_info.bytes_per_sector / 32 :
                           fat_info.root_entry_count;
    
    // Read root directory - limit to first 4 sectors for efficiency
    uint32_t search_sectors = (max_entries / 16 > 4) ? 4 : max_entries / 16;
    for (uint32_t i = 0; i < search_sectors; i++) {
        if (fat_read_sector(root_sector + i, sector_buffer) != 0) {
            serial_printf("[FAT] Failed to read sector %u\n", root_sector + i);
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
            if (entries[j].attributes & FAT_ATTR_LONG_NAME) {
                continue; // Skip long filename entries
            }
            if (entries[j].attributes & FAT_ATTR_VOLUME_ID) {
                continue; // Skip volume label
            }
            
            // Compare filename in 8.3 format
            if (fat_compare_83((const char *)entries[j].name, name83) == 0) {
                // Found the file!
                file->first_cluster = ((uint32_t)entries[j].first_cluster_high << 16) | entries[j].first_cluster_low;
                file->current_cluster = file->first_cluster;
                file->position = 0;
                file->size = entries[j].file_size;
                file->attributes = entries[j].attributes;
                file->is_open = 1;
                
                // Copy the name
                for (int k = 0; path[k] && k < 255; k++) {
                    file->name[k] = path[k];
                    file->name[k + 1] = 0;
                }
                
                serial_printf("[FAT] Opened: size=%u\n", file->size);
                return 0;
            }
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
    
    // Clamp position to file size
    if (position > file->size) {
        position = file->size;
    }
    
    file->position = position;
    
    // Recalculate current cluster based on position
    uint32_t cluster_size = fat_info.sectors_per_cluster * fat_info.bytes_per_sector;
    uint32_t target_cluster_index = position / cluster_size;
    
    // Walk the cluster chain from the beginning
    file->current_cluster = file->first_cluster;
    for (uint32_t i = 0; i < target_cluster_index && file->current_cluster != 0xFFFFFFFF; i++) {
        file->current_cluster = fat_get_next_cluster(file->current_cluster);
    }
    
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
 * Read a sector using IDE driver
 */
extern void serial_printf(const char *fmt, ...);
int fat_read_sector(uint32_t sector, void *buffer) {
    if (!buffer) {
        return -1;
    }
    
    // Add partition offset to get absolute sector on disk
    uint32_t abs_sector = sector + fat_partition_offset;
    
    // Use IDE driver to read sector
    int result = ide_read_sector(fat_current_drive, abs_sector, buffer);
    return (result > 0) ? 0 : -1;
}

/**
 * Write a sector using IDE driver
 */
int fat_write_sector(uint32_t sector, const void *buffer) {
    if (!buffer) {
        return -1;
    }
    
    // Add partition offset to get absolute sector on disk
    uint32_t abs_sector = sector + fat_partition_offset;
    
    // Use IDE driver to write sector
    int result = ide_write_sector(fat_current_drive, abs_sector, buffer);
    return (result > 0) ? 0 : -1;
}

/**
 * Set the partition offset for FAT filesystem
 * (for accessing partition instead of raw disk)
 */
void fat_set_partition_offset(uint32_t offset) {
    fat_partition_offset = offset;
}

/**
 * Get FAT info structure (for debugging)
 */
fat_info_t *fat_get_info(void) {
    return fat_mounted ? &fat_info : (fat_info_t *)0;
}

/**
 * Check if FAT filesystem is mounted
 */
int fat_is_mounted(void) {
    return fat_mounted;
}
