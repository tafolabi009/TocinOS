# TocinOS Integration Guide

## New Features Integration

This guide explains how the newly implemented features integrate with the existing TocinOS codebase.

## 1. VESA Graphics Driver

### Integration Points

**Kernel Initialization** (`kernel/kernel.c`):
```c
kernel_print("[*] Initializing VESA Graphics...\n");
if (vesa_init() == 0) {
    kernel_print("    VESA graphics initialized\n");
} else {
    kernel_print("    VESA not available\n");
}
```

**MDF Registration** (`kernel/drivers/vesa_driver.c`):
```c
void vesa_driver_register(void) {
    mdf_register_driver("vesa", DRIVER_TYPE_CHARACTER, 
                       vesa_driver_init, vesa_driver_probe);
}
```

**Usage Example**:
```c
#include "include/drivers/vesa.h"

// Initialize VESA
vesa_init();

// Draw a red rectangle
color_t red = vesa_make_color(255, 0, 0, 255);
vesa_draw_rect(100, 100, 200, 150, red);

// Draw text
color_t white = vesa_make_color(255, 255, 255, 255);
color_t black = vesa_make_color(0, 0, 0, 255);
vesa_draw_string(10, 10, "Hello, TocinOS!", white, black);
```

---

## 2. FAT Filesystem

### Integration Points

**Disk I/O Bridge** (Required):
```c
// Implement these in IDE driver integration
int fat_read_sector(uint32_t sector, void *buffer) {
    return ide_read_sectors(0, sector, 1, buffer);
}

int fat_write_sector(uint32_t sector, const void *buffer) {
    return ide_write_sectors(0, sector, 1, buffer);
}
```

**Usage Example**:
```c
#include "include/kernel/fat.h"

// Mount FAT filesystem on first IDE drive
fat_mount(0);

// Open a file
fat_file_t file;
if (fat_open("/README.TXT", &file) == 0) {
    // Read file contents
    char buffer[512];
    int bytes_read = fat_read(&file, buffer, sizeof(buffer));
    
    // Close file
    fat_close(&file);
}

// List directory
fat_dir_entry_t entries[32];
int count = fat_list_dir("/", entries, 32);
for (int i = 0; i < count; i++) {
    // Process directory entries
}
```

---

## 3. User Mode Support

### Integration Points

**Kernel Initialization** (`kernel/kernel.c`):
```c
kernel_print("[*] Initializing User Mode Support...\n");
if (usermode_init() == 0) {
    kernel_print("    User mode support enabled\n");
}
```

**Creating User Processes**:
```c
#include "include/kernel/usermode.h"

// User mode function
void user_program(void) {
    // This runs in Ring 3
    while (1) {
        // User mode code
    }
}

// Create user process
uint32_t pid;
usermode_create_process(user_program, &pid);

// Switch to user mode
usermode_switch_to_user(user_program);
```

**System Call Integration**:
```c
// System calls from user mode automatically switch to Ring 0
// via INT 0x80 handler
```

---

## 4. IDE Disk Driver

### Integration Points

**Kernel Initialization** (`kernel/kernel.c`):
```c
kernel_print("[*] Initializing IDE Disk Driver...\n");
if (ide_init() == 0) {
    kernel_print("    IDE driver initialized\n");
}
```

**MDF Registration** (`kernel/drivers/ide_driver.c`):
```c
void ide_driver_register(void) {
    mdf_register_driver("ide", DRIVER_TYPE_BLOCK, 
                       ide_driver_init, ide_driver_probe);
}
```

**Usage Example**:
```c
#include "include/drivers/ide.h"

// Read sectors from first IDE drive
uint8_t buffer[512];
ide_read_sectors(0, 0, 1, buffer);  // Read boot sector

// Write sectors
ide_write_sectors(0, 100, 1, buffer);

// Get device information
ide_device_t *device = ide_get_device(0);
if (device && device->present) {
    // Access device->model, device->total_sectors, etc.
}
```

**Integration with FAT**:
```c
// Bridge FAT filesystem to IDE driver
int fat_read_sector(uint32_t sector, void *buffer) {
    return ide_read_sectors(0, sector, 1, buffer) > 0 ? 0 : -1;
}
```

---

## 5. Network Driver

### Integration Points

**Kernel Initialization** (`kernel/kernel.c`):
```c
kernel_print("[*] Initializing Network Driver...\n");
if (net_init() == 0) {
    kernel_print("    Network driver initialized\n");
}
```

**MDF Registration** (`kernel/drivers/net_driver.c`):
```c
void net_driver_register(void) {
    mdf_register_driver("net", DRIVER_TYPE_NETWORK, 
                       net_driver_init, net_driver_probe);
}
```

**Usage Example**:
```c
#include "include/drivers/net.h"

// Get MAC address
uint8_t mac[6];
net_get_mac_address(mac);

// Send packet
uint8_t packet[64];
// ... fill packet data ...
net_send_packet(packet, sizeof(packet));

// Receive packet (polling)
uint8_t rx_buffer[1500];
int received = net_receive_packet(rx_buffer, sizeof(rx_buffer));
if (received > 0) {
    // Process received packet
}

// Get network statistics
net_device_t *dev = net_get_device();
// Access dev->rx_packets, dev->tx_packets, etc.
```

---

## 6. UEFI Boot Support

### Integration Points

**Boot Information Structure** (`include/boot/boot_info.h`):
```c
// UEFI bootloader fills this structure
boot_info_t *boot_info = (boot_info_t *)BOOT_INFO_ADDRESS;

// Check if booted via UEFI
if (boot_info->boot_flags & BOOT_FLAG_UEFI) {
    // UEFI boot
    // Access framebuffer, memory map, etc.
}
```

**Framebuffer Access**:
```c
if (boot_info->framebuffer_addr != 0) {
    // Use UEFI-provided framebuffer
    vesa_ctx.framebuffer = (uint32_t *)boot_info->framebuffer_addr;
    vesa_ctx.width = boot_info->framebuffer_width;
    vesa_ctx.height = boot_info->framebuffer_height;
}
```

---

## Complete Integration Example

### Disk-Based File Loading

```c
#include "include/drivers/ide.h"
#include "include/kernel/fat.h"

void load_file_from_disk(void) {
    // 1. Initialize IDE driver
    if (ide_init() != 0) {
        kernel_print("ERROR: IDE initialization failed\n");
        return;
    }
    
    // 2. Mount FAT filesystem
    if (fat_mount(0) != 0) {
        kernel_print("ERROR: Failed to mount filesystem\n");
        return;
    }
    
    // 3. Open file
    fat_file_t file;
    if (fat_open("/KERNEL.BIN", &file) != 0) {
        kernel_print("ERROR: File not found\n");
        return;
    }
    
    // 4. Read file
    uint8_t *buffer = pmm_alloc_page();
    int bytes_read = fat_read(&file, buffer, 4096);
    
    kernel_print("Read %d bytes from file\n", bytes_read);
    
    // 5. Close file
    fat_close(&file);
    
    // 6. Free buffer
    pmm_free_page(buffer);
}
```

### User Mode with System Calls

```c
#include "include/kernel/usermode.h"
#include "include/kernel/syscall.h"

void user_program(void) {
    // This runs in Ring 3
    while (1) {
        // Use system call to print
        syscall_write(1, "Hello from user mode!\n", 23);
        
        // Sleep
        syscall_sleep(100);
    }
}

void start_user_mode(void) {
    // 1. Initialize user mode
    usermode_init();
    
    // 2. Create user process
    uint32_t pid;
    usermode_create_process(user_program, &pid);
    
    // 3. Switch to user mode
    usermode_switch_to_user(user_program);
}
```

---

## Build System Integration

### Updated Makefile

The Makefile has been updated to include new source files:

```makefile
KERNEL_C_SOURCES = $(wildcard $(KERNEL_DIR)/*.c) \
                   $(wildcard $(KERNEL_DIR)/mm/*.c) \
                   $(wildcard $(KERNEL_DIR)/task/*.c) \
                   $(wildcard $(KERNEL_DIR)/drivers/*.c) \
                   $(KERNEL_DIR)/fat.c \
                   $(KERNEL_DIR)/usermode.c
```

All new drivers are automatically included via the wildcard:
- `kernel/drivers/vesa_driver.c`
- `kernel/drivers/ide_driver.c`
- `kernel/drivers/net_driver.c`

---

## Testing Checklist

- [ ] Build completes without errors
- [ ] Kernel boots and displays initialization messages
- [ ] VESA driver detects framebuffer (if available)
- [ ] IDE driver detects hard drives (if present)
- [ ] Network driver detects NE2000 card (if present)
- [ ] User mode initialization succeeds
- [ ] FAT filesystem can be mounted (with disk I/O bridge)
- [ ] System calls work from user mode
- [ ] No kernel panics or crashes

---

## Troubleshooting

### VESA Not Available
- Check if boot_info->framebuffer_addr is set
- Ensure bootloader sets up graphics mode
- Try different resolutions

### IDE Not Detected
- Verify IDE controller is present
- Check I/O port addresses
- Try different drives (master/slave)

### Network Not Detected
- Verify NE2000 compatible card is present
- Check base I/O addresses (0x300, 0x320, 0x340, 0x360)
- Ensure IRQ is not conflicting

### User Mode Issues
- Verify TSS is properly set up
- Check GDT entries for user code/data segments
- Ensure system call handler is installed

---

**Last Updated**: December 2024
