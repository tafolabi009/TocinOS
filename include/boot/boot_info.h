/**
 * TocinOS Boot Information Structure
 * 
 * This structure is passed from the bootloader to the kernel
 * Contains essential system information detected during boot
 */

#ifndef BOOT_INFO_H
#define BOOT_INFO_H

// Boot flags
#define BOOT_FLAG_BIOS      0x01
#define BOOT_FLAG_UEFI      0x02
#define BOOT_FLAG_64BIT     0x04
#define BOOT_FLAG_32BIT     0x08

// Memory map entry types
#define MEMORY_TYPE_USABLE          1
#define MEMORY_TYPE_RESERVED        2
#define MEMORY_TYPE_ACPI_RECLAIMABLE 3
#define MEMORY_TYPE_ACPI_NVS        4
#define MEMORY_TYPE_BAD             5

/**
 * Memory map entry
 */
typedef struct {
    unsigned long long base;        // Base address
    unsigned long long length;      // Length in bytes
    unsigned int type;              // Memory type
    unsigned int reserved;          // Reserved for alignment
} memory_map_entry_t;

/**
 * Boot information structure
 * Located at a fixed address (0x8000) for easy access
 */
typedef struct {
    unsigned int magic;             // Magic number: 0xB007DA7A
    unsigned int boot_flags;        // Boot flags (BIOS/UEFI, 32/64-bit)
    
    // Memory information
    unsigned int memory_lower;      // Lower memory in KB (0-1MB)
    unsigned int memory_upper;      // Upper memory in KB (>1MB)
    unsigned int memory_map_count;  // Number of memory map entries
    memory_map_entry_t *memory_map; // Pointer to memory map
    
    // Boot device information
    unsigned int boot_device;       // Boot device number
    unsigned int boot_partition;    // Boot partition number
    
    // CPU information
    unsigned int cpu_vendor[3];     // CPU vendor string (12 chars)
    unsigned int cpu_features_edx;  // CPU features from CPUID (EDX)
    unsigned int cpu_features_ecx;  // CPU features from CPUID (ECX)
    unsigned int cpu_extended_features; // Extended CPU features
    
    // Framebuffer information (for graphics mode)
    unsigned int framebuffer_addr;  // Framebuffer physical address
    unsigned int framebuffer_pitch; // Framebuffer pitch
    unsigned int framebuffer_width; // Framebuffer width
    unsigned int framebuffer_height;// Framebuffer height
    unsigned int framebuffer_bpp;   // Bits per pixel
    
    // Kernel load information
    unsigned int kernel_start;      // Kernel start address
    unsigned int kernel_end;        // Kernel end address
    unsigned int kernel_size;       // Kernel size in bytes
    
    // Bootloader information
    char bootloader_name[32];       // Bootloader name
    unsigned int bootloader_version;// Bootloader version
    
    // Reserved for future use
    unsigned int reserved[8];
} boot_info_t;

// Boot info magic number
#define BOOT_INFO_MAGIC 0xB007DA7A

// Boot info location in memory
#define BOOT_INFO_ADDRESS 0x8000

#endif // BOOT_INFO_H
