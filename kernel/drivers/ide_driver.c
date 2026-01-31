/**
 * TocinOS IDE/ATA Driver Implementation
 * 
 * Implements IDE/ATA disk driver for storage access
 */

#include "../include/drivers/ide.h"
#include "../include/drivers/mdf.h"

// IDE channels
static ide_channel_t ide_channels[2] = {
    {IDE_PRIMARY_DATA, IDE_PRIMARY_CONTROL, 14, {0}, {0}},    // Primary
    {IDE_SECONDARY_DATA, IDE_SECONDARY_CONTROL, 15, {0}, {0}} // Secondary
};

static int ide_initialized = 0;

// Default timeout in milliseconds
#define IDE_TIMEOUT_MS 5000

// Error codes
#define IDE_ERR_NONE        0
#define IDE_ERR_TIMEOUT    -1
#define IDE_ERR_NO_DRIVE   -2
#define IDE_ERR_READ_FAIL  -3
#define IDE_ERR_WRITE_FAIL -4
#define IDE_ERR_INVALID    -5

/**
 * Read byte from port
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Write byte to port
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read word from port
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Write word to port
 */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * 400ns delay by reading alternate status register
 */
static void ide_400ns_delay(uint16_t ctrl) {
    inb(ctrl);
    inb(ctrl);
    inb(ctrl);
    inb(ctrl);
}

/**
 * Wait for drive to be ready with timeout
 * Returns 0 on success, -1 on timeout
 */
int ide_wait_ready_timeout(uint16_t base, uint32_t timeout_ms) {
    volatile uint32_t timeout = timeout_ms * 1000; // Approximate loop count
    while (timeout > 0) {
        uint8_t status = inb(base + 7);
        if (!(status & IDE_STATUS_BSY)) {
            return 0; // Ready
        }
        timeout--;
    }
    return -1; // Timeout
}

/**
 * Wait for data request with timeout
 * Returns 0 on success, -1 on timeout, -2 on error
 */
int ide_wait_drq_timeout(uint16_t base, uint32_t timeout_ms) {
    volatile uint32_t timeout = timeout_ms * 1000;
    while (timeout > 0) {
        uint8_t status = inb(base + 7);
        if (status & IDE_STATUS_ERR) {
            return -2; // Error
        }
        if (status & IDE_STATUS_DF) {
            return -2; // Drive fault
        }
        if (status & IDE_STATUS_DRQ) {
            return 0; // Data ready
        }
        timeout--;
    }
    return -1; // Timeout
}

/**
 * Wait for drive to be ready
 */
void ide_wait_ready(uint16_t base) {
    while (inb(base + 7) & IDE_STATUS_BSY) {
        // Wait
    }
}

/**
 * Wait for data request
 */
void ide_wait_drq(uint16_t base) {
    while (!(inb(base + 7) & IDE_STATUS_DRQ)) {
        // Wait
    }
}

/**
 * Get status
 */
uint8_t ide_status(uint16_t base) {
    return inb(base + 7);
}

/**
 * Select drive
 */
void ide_select_drive(uint16_t base, uint8_t drive) {
    outb(base + 6, (drive == IDE_DRIVE_MASTER) ? 0xA0 : 0xB0);
    ide_wait_ready(base);
}

/**
 * Identify drive
 */
int ide_identify(uint8_t drive) {
    uint8_t channel = drive / 2;
    uint8_t slave = drive % 2;
    uint16_t base = ide_channels[channel].base;
    
    // Select drive
    ide_select_drive(base, slave);
    
    // Send IDENTIFY command
    outb(base + 7, IDE_CMD_IDENTIFY);
    
    // Check if drive exists
    if (inb(base + 7) == 0) {
        return -1; // No drive
    }
    
    // Wait for response
    ide_wait_ready(base);
    
    uint8_t status = ide_status(base);
    if (status & IDE_STATUS_ERR) {
        return -1; // Error
    }
    
    // Wait for DRQ
    ide_wait_drq(base);
    
    // Read identification data
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(base);
    }
    
    // Store device info
    ide_device_t *device = (slave == 0) ? &ide_channels[channel].master : &ide_channels[channel].slave;
    device->present = 1;
    device->type = identify[0];
    device->cylinders = identify[1];
    device->heads = identify[3];
    device->sectors = identify[6];
    device->total_sectors = *(uint32_t *)&identify[60];
    device->capabilities = identify[49];
    
    // Extract model string
    for (int i = 0; i < 20; i++) {
        device->model[i * 2] = (identify[27 + i] >> 8) & 0xFF;
        device->model[i * 2 + 1] = identify[27 + i] & 0xFF;
    }
    device->model[40] = 0;
    
    return 0;
}

/**
 * Initialize IDE driver
 */
int ide_init(void) {
    if (ide_initialized) {
        return 0;
    }
    
    // Detect devices
    ide_detect_devices();
    
    ide_initialized = 1;
    return 0;
}

/**
 * Detect IDE devices
 */
int ide_detect_devices(void) {
    // Try to identify all 4 possible drives
    for (int i = 0; i < 4; i++) {
        ide_identify(i);
    }
    
    return 0;
}

/**
 * Read sectors from disk
 */
int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer) {
    if (!ide_initialized || !buffer || count == 0) {
        return IDE_ERR_INVALID;
    }
    
    uint8_t channel = drive / 2;
    uint8_t slave = drive % 2;
    uint16_t base = ide_channels[channel].base;
    uint16_t ctrl = ide_channels[channel].ctrl;
    
    // Check if device exists
    ide_device_t *device = (slave == 0) ? &ide_channels[channel].master : &ide_channels[channel].slave;
    if (!device->present) {
        return IDE_ERR_NO_DRIVE;
    }
    
    // Wait for drive to be ready
    if (ide_wait_ready_timeout(base, IDE_TIMEOUT_MS) < 0) {
        return IDE_ERR_TIMEOUT;
    }
    
    // Select drive and set LBA mode (bit 6 = LBA mode, bit 4 = drive select)
    outb(base + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    
    // 400ns delay after drive select
    ide_400ns_delay(ctrl);
    
    // Set sector count
    outb(base + 2, count);
    
    // Set LBA address
    outb(base + 3, lba & 0xFF);
    outb(base + 4, (lba >> 8) & 0xFF);
    outb(base + 5, (lba >> 16) & 0xFF);
    
    // Send read command
    outb(base + 7, IDE_CMD_READ_SECTORS);
    
    // Read data
    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        // Wait for BSY to clear and DRQ to set
        int result = ide_wait_drq_timeout(base, IDE_TIMEOUT_MS);
        if (result < 0) {
            return (result == -1) ? IDE_ERR_TIMEOUT : IDE_ERR_READ_FAIL;
        }
        
        // Read 256 words (512 bytes) per sector
        for (int j = 0; j < 256; j++) {
            buf[i * 256 + j] = inw(base);
        }
        
        // 400ns delay between sectors
        ide_400ns_delay(ctrl);
    }
    
    return count;
}

/**
 * Write sectors to disk
 */
int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buffer) {
    if (!ide_initialized || !buffer || count == 0) {
        return IDE_ERR_INVALID;
    }
    
    uint8_t channel = drive / 2;
    uint8_t slave = drive % 2;
    uint16_t base = ide_channels[channel].base;
    uint16_t ctrl = ide_channels[channel].ctrl;
    
    // Check if device exists
    ide_device_t *device = (slave == 0) ? &ide_channels[channel].master : &ide_channels[channel].slave;
    if (!device->present) {
        return IDE_ERR_NO_DRIVE;
    }
    
    // Wait for drive to be ready
    if (ide_wait_ready_timeout(base, IDE_TIMEOUT_MS) < 0) {
        return IDE_ERR_TIMEOUT;
    }
    
    // Select drive and set LBA mode
    outb(base + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    
    // 400ns delay after drive select
    ide_400ns_delay(ctrl);
    
    // Set sector count
    outb(base + 2, count);
    
    // Set LBA address
    outb(base + 3, lba & 0xFF);
    outb(base + 4, (lba >> 8) & 0xFF);
    outb(base + 5, (lba >> 16) & 0xFF);
    
    // Send write command
    outb(base + 7, IDE_CMD_WRITE_SECTORS);
    
    // Write data
    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        // Wait for DRQ
        int result = ide_wait_drq_timeout(base, IDE_TIMEOUT_MS);
        if (result < 0) {
            return (result == -1) ? IDE_ERR_TIMEOUT : IDE_ERR_WRITE_FAIL;
        }
        
        // Write 256 words (512 bytes) per sector
        for (int j = 0; j < 256; j++) {
            outw(base, buf[i * 256 + j]);
        }
        
        // 400ns delay between sectors
        ide_400ns_delay(ctrl);
    }
    
    // Flush cache
    outb(base + 7, IDE_CMD_CACHE_FLUSH);
    if (ide_wait_ready_timeout(base, IDE_TIMEOUT_MS) < 0) {
        return IDE_ERR_TIMEOUT;
    }
    
    return count;
}

/**
 * Read a single sector from disk (convenience wrapper)
 */
int ide_read_sector(uint8_t drive, uint32_t lba, void *buffer) {
    return ide_read_sectors(drive, lba, 1, buffer);
}

/**
 * Write a single sector to disk (convenience wrapper)
 */
int ide_write_sector(uint8_t drive, uint32_t lba, const void *buffer) {
    return ide_write_sectors(drive, lba, 1, buffer);
}

/**
 * Get device info
 */
ide_device_t *ide_get_device(uint8_t drive) {
    if (drive >= 4) {
        return 0;
    }
    
    uint8_t channel = drive / 2;
    uint8_t slave = drive % 2;
    
    return (slave == 0) ? &ide_channels[channel].master : &ide_channels[channel].slave;
}

/**
 * IDE driver initialization for MDF
 */
static int ide_driver_init(void) {
    return ide_init();
}

/**
 * IDE driver probe
 */
static int ide_driver_probe(void) {
    // IDE is always present on x86 systems
    return 0;
}

/**
 * Register IDE driver with MDF
 */
void ide_driver_register(void) {
    mdf_register_driver("ide", DRIVER_TYPE_BLOCK, ide_driver_init, ide_driver_probe);
}
