/**
 * TocinOS UEFI Support Header
 * 
 * Provides structures and definitions for UEFI boot support
 */

#ifndef UEFI_H
#define UEFI_H

#include "../stdint.h"

// UEFI data types
typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;

// UEFI Status codes
#define EFI_SUCCESS              0
#define EFI_LOAD_ERROR           1
#define EFI_INVALID_PARAMETER    2
#define EFI_UNSUPPORTED          3
#define EFI_BAD_BUFFER_SIZE      4
#define EFI_BUFFER_TOO_SMALL     5
#define EFI_NOT_READY            6
#define EFI_DEVICE_ERROR         7
#define EFI_WRITE_PROTECTED      8
#define EFI_OUT_OF_RESOURCES     9
#define EFI_NOT_FOUND            14

// EFI GUID structure
typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} EFI_GUID;

// EFI Memory types
#define EFI_RESERVED_MEMORY_TYPE      0
#define EFI_LOADER_CODE               1
#define EFI_LOADER_DATA               2
#define EFI_BOOT_SERVICES_CODE        3
#define EFI_BOOT_SERVICES_DATA        4
#define EFI_RUNTIME_SERVICES_CODE     5
#define EFI_RUNTIME_SERVICES_DATA     6
#define EFI_CONVENTIONAL_MEMORY       7
#define EFI_UNUSABLE_MEMORY           8
#define EFI_ACPI_RECLAIM_MEMORY       9
#define EFI_ACPI_MEMORY_NVS          10
#define EFI_MEMORY_MAPPED_IO         11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                 13

// EFI Memory descriptor
typedef struct {
    uint32_t type;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} EFI_MEMORY_DESCRIPTOR;

// Graphics Output Protocol Mode Info
typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    struct {
        uint32_t red_mask;
        uint32_t green_mask;
        uint32_t blue_mask;
        uint32_t reserved_mask;
    } pixel_information;
    uint32_t pixels_per_scan_line;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

// Graphics Output Protocol Mode
typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    uint64_t size_of_info;
    EFI_PHYSICAL_ADDRESS frame_buffer_base;
    uint64_t frame_buffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

// Simple Text Output Protocol
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *reset;
    EFI_STATUS (*output_string)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *this, uint16_t *string);
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// EFI System Table
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con_out;
    EFI_HANDLE standard_error_handle;
    void *std_err;
    void *runtime_services;
    void *boot_services;
    uint64_t number_of_table_entries;
    void *configuration_table;
} EFI_SYSTEM_TABLE;

// UEFI boot info structure (for passing to kernel)
typedef struct {
    uint32_t magic;                     // Magic: 0xUEF1B007
    EFI_MEMORY_DESCRIPTOR *memory_map;  // Memory map
    uint64_t memory_map_size;           // Memory map size
    uint64_t memory_map_descriptor_size;// Descriptor size
    uint32_t memory_map_descriptor_version; // Descriptor version
    
    // Graphics info
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    
    // ACPI tables
    void *acpi_table;
    void *acpi2_table;
    
    // Kernel location
    void *kernel_base;
    uint64_t kernel_size;
} uefi_boot_info_t;

// UEFI magic number
#define UEFI_BOOT_MAGIC 0xUEF1B007

// Function prototypes
int uefi_init(uefi_boot_info_t *boot_info);
void uefi_print(const char *str);
void *uefi_allocate_pages(uint64_t pages);
void uefi_free_pages(void *address, uint64_t pages);

#endif // UEFI_H
