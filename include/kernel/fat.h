/**
 * TocinOS FAT Filesystem Support
 * 
 * Provides support for FAT12, FAT16, and FAT32 filesystems
 */

#ifndef FAT_H
#define FAT_H

#include "../stdint.h"

// FAT types
#define FAT_TYPE_FAT12  12
#define FAT_TYPE_FAT16  16
#define FAT_TYPE_FAT32  32

// FAT attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  0x0F

// FAT boot sector (common fields)
typedef struct {
    uint8_t  jump[3];               // Jump instruction
    uint8_t  oem_name[8];           // OEM name
    uint16_t bytes_per_sector;      // Bytes per sector
    uint8_t  sectors_per_cluster;   // Sectors per cluster
    uint16_t reserved_sectors;      // Reserved sectors
    uint8_t  fat_count;             // Number of FATs
    uint16_t root_entry_count;      // Root directory entries (FAT12/16)
    uint16_t total_sectors_16;      // Total sectors (if < 65536)
    uint8_t  media_type;            // Media descriptor
    uint16_t sectors_per_fat_16;    // Sectors per FAT (FAT12/16)
    uint16_t sectors_per_track;     // Sectors per track
    uint16_t head_count;            // Number of heads
    uint32_t hidden_sectors;        // Hidden sectors
    uint32_t total_sectors_32;      // Total sectors (if >= 65536)
} __attribute__((packed)) fat_bpb_t;

// FAT32 extended boot sector
typedef struct {
    fat_bpb_t bpb;                  // Common BPB
    uint32_t sectors_per_fat_32;    // Sectors per FAT (FAT32)
    uint16_t flags;                 // Flags
    uint16_t version;               // Version
    uint32_t root_cluster;          // Root directory cluster
    uint16_t fsinfo_sector;         // FSInfo sector
    uint16_t backup_boot_sector;    // Backup boot sector
    uint8_t  reserved[12];          // Reserved
    uint8_t  drive_number;          // Drive number
    uint8_t  reserved1;             // Reserved
    uint8_t  boot_signature;        // Boot signature (0x29)
    uint32_t volume_id;             // Volume ID
    uint8_t  volume_label[11];      // Volume label
    uint8_t  fs_type[8];            // Filesystem type
} __attribute__((packed)) fat32_ebpb_t;

// FAT12/16 extended boot sector
typedef struct {
    fat_bpb_t bpb;                  // Common BPB
    uint8_t  drive_number;          // Drive number
    uint8_t  reserved1;             // Reserved
    uint8_t  boot_signature;        // Boot signature (0x29)
    uint32_t volume_id;             // Volume ID
    uint8_t  volume_label[11];      // Volume label
    uint8_t  fs_type[8];            // Filesystem type
} __attribute__((packed)) fat16_ebpb_t;

// FAT directory entry
typedef struct {
    uint8_t  name[11];              // 8.3 filename
    uint8_t  attributes;            // File attributes
    uint8_t  reserved;              // Reserved
    uint8_t  creation_time_tenth;   // Creation time (tenths of second)
    uint16_t creation_time;         // Creation time
    uint16_t creation_date;         // Creation date
    uint16_t last_access_date;      // Last access date
    uint16_t first_cluster_high;    // First cluster (high word, FAT32)
    uint16_t last_write_time;       // Last write time
    uint16_t last_write_date;       // Last write date
    uint16_t first_cluster_low;     // First cluster (low word)
    uint32_t file_size;             // File size in bytes
} __attribute__((packed)) fat_dir_entry_t;

// FAT filesystem info
typedef struct {
    uint8_t  type;                  // FAT type (12, 16, or 32)
    uint32_t fat_start;             // First FAT sector
    uint32_t data_start;            // First data sector
    uint32_t root_start;            // Root directory start (FAT12/16)
    uint32_t root_cluster;          // Root directory cluster (FAT32)
    uint32_t cluster_count;         // Total clusters
    uint16_t bytes_per_sector;      // Bytes per sector
    uint8_t  sectors_per_cluster;   // Sectors per cluster
    uint16_t reserved_sectors;      // Reserved sectors
    uint8_t  fat_count;             // Number of FATs
    uint32_t sectors_per_fat;       // Sectors per FAT
    uint16_t root_entry_count;      // Root entries (FAT12/16)
} fat_info_t;

// File handle
typedef struct {
    uint32_t first_cluster;         // First cluster
    uint32_t current_cluster;       // Current cluster
    uint32_t position;              // Current position
    uint32_t size;                  // File size
    uint8_t  attributes;            // File attributes
    char     name[256];             // Full filename
    int      is_open;               // Is file open?
} fat_file_t;

// FAT filesystem API
int fat_init(uint8_t drive);
int fat_mount(uint8_t drive);
int fat_unmount(void);
int fat_open(const char *path, fat_file_t *file);
int fat_close(fat_file_t *file);
int fat_read(fat_file_t *file, void *buffer, uint32_t size);
int fat_write(fat_file_t *file, const void *buffer, uint32_t size);
int fat_seek(fat_file_t *file, uint32_t position);
int fat_list_dir(const char *path, fat_dir_entry_t *entries, int max_entries);
int fat_stat(const char *path, fat_dir_entry_t *entry);
void fat_set_partition_offset(uint32_t offset);
fat_info_t *fat_get_info(void);
int fat_is_mounted(void);

// Internal functions
uint32_t fat_get_next_cluster(uint32_t cluster);
uint32_t fat_cluster_to_sector(uint32_t cluster);
int fat_read_sector(uint32_t sector, void *buffer);
int fat_write_sector(uint32_t sector, const void *buffer);

#endif // FAT_H
