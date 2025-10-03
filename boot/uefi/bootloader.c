/**
 * TocinOS UEFI Bootloader
 * 
 * UEFI application that loads the TocinOS kernel
 */

#include "../../include/boot/uefi.h"
#include "../../include/boot/boot_info.h"

// Global system table
static EFI_SYSTEM_TABLE *gSystemTable = 0;
static uefi_boot_info_t uefi_boot_info = {0};

/**
 * Print a string using UEFI console
 */
void uefi_print_string(const char *str) {
    if (!gSystemTable || !gSystemTable->con_out) {
        return;
    }
    
    // Convert ASCII to UTF-16
    uint16_t buffer[256];
    int i = 0;
    while (str[i] && i < 255) {
        buffer[i] = (uint16_t)str[i];
        i++;
    }
    buffer[i] = 0;
    
    gSystemTable->con_out->output_string(gSystemTable->con_out, buffer);
}

/**
 * Load kernel from filesystem
 */
EFI_STATUS load_kernel(void) {
    // This is a placeholder
    // Actual implementation would:
    // 1. Use Simple File System Protocol to access filesystem
    // 2. Open /EFI/TocinOS/kernel.bin
    // 3. Read kernel into memory
    // 4. Return physical address
    
    uefi_print_string("Loading kernel from filesystem...\r\n");
    
    // For now, assume kernel is already loaded
    return EFI_SUCCESS;
}

/**
 * Setup graphics mode
 */
EFI_STATUS setup_graphics(void) {
    uefi_print_string("Setting up graphics mode...\r\n");
    
    // This would use Graphics Output Protocol to:
    // 1. Enumerate available modes
    // 2. Select appropriate mode
    // 3. Get framebuffer information
    // 4. Store in uefi_boot_info
    
    // Placeholder values
    uefi_boot_info.framebuffer_base = 0;
    uefi_boot_info.framebuffer_width = 0;
    uefi_boot_info.framebuffer_height = 0;
    uefi_boot_info.framebuffer_pitch = 0;
    uefi_boot_info.framebuffer_bpp = 32;
    
    return EFI_SUCCESS;
}

/**
 * Get memory map
 */
EFI_STATUS get_memory_map(void) {
    uefi_print_string("Getting memory map...\r\n");
    
    // This would use Boot Services to:
    // 1. Call GetMemoryMap()
    // 2. Allocate buffer for memory map
    // 3. Store in uefi_boot_info
    
    return EFI_SUCCESS;
}

/**
 * Prepare boot info structure for kernel
 */
void prepare_boot_info(void) {
    boot_info_t *boot_info = (boot_info_t *)BOOT_INFO_ADDRESS;
    
    // Fill in boot info from UEFI info
    boot_info->magic = BOOT_INFO_MAGIC;
    boot_info->boot_flags = BOOT_FLAG_UEFI | BOOT_FLAG_64BIT;
    
    // Graphics info
    boot_info->framebuffer_addr = (uint32_t)uefi_boot_info.framebuffer_base;
    boot_info->framebuffer_width = uefi_boot_info.framebuffer_width;
    boot_info->framebuffer_height = uefi_boot_info.framebuffer_height;
    boot_info->framebuffer_pitch = uefi_boot_info.framebuffer_pitch;
    boot_info->framebuffer_bpp = uefi_boot_info.framebuffer_bpp;
    
    // Bootloader info
    const char *bootloader_name = "TocinOS UEFI Bootloader";
    for (int i = 0; i < 32 && bootloader_name[i]; i++) {
        boot_info->bootloader_name[i] = bootloader_name[i];
    }
    boot_info->bootloader_version = 0x00010000; // Version 1.0
}

/**
 * Jump to kernel
 */
void jump_to_kernel(void) {
    // Setup kernel entry point
    // This assumes kernel is loaded at 0x100000 (1MB)
    void (*kernel_entry)(void) = (void (*)(void))0x100000;
    
    uefi_print_string("Jumping to kernel...\r\n");
    
    // Call kernel
    kernel_entry();
    
    // Should never return
    while (1) {
        __asm__ volatile("hlt");
    }
}

/**
 * UEFI Application Entry Point
 */
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;
    
    gSystemTable = system_table;
    
    // Clear screen
    if (system_table->con_out) {
        system_table->con_out->clear_screen(system_table->con_out);
    }
    
    uefi_print_string("TocinOS UEFI Bootloader v1.0\r\n");
    uefi_print_string("================================\r\n\r\n");
    
    // Initialize UEFI boot info
    uefi_boot_info.magic = UEFI_BOOT_MAGIC;
    
    // Load kernel
    EFI_STATUS status = load_kernel();
    if (status != EFI_SUCCESS) {
        uefi_print_string("ERROR: Failed to load kernel\r\n");
        return status;
    }
    
    // Setup graphics
    status = setup_graphics();
    if (status != EFI_SUCCESS) {
        uefi_print_string("WARNING: Failed to setup graphics mode\r\n");
    }
    
    // Get memory map
    status = get_memory_map();
    if (status != EFI_SUCCESS) {
        uefi_print_string("ERROR: Failed to get memory map\r\n");
        return status;
    }
    
    // Prepare boot info structure
    prepare_boot_info();
    
    uefi_print_string("\r\nPress any key to continue...\r\n");
    
    // Exit boot services would go here
    // status = system_table->boot_services->ExitBootServices(image_handle, map_key);
    
    // Jump to kernel
    jump_to_kernel();
    
    return EFI_SUCCESS;
}
