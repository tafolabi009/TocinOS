/**
 * TocinOS IDE/ATA Disk Driver
 * 
 * Provides support for IDE/ATA hard disk drives
 */

#ifndef IDE_H
#define IDE_H

#include "../stdint.h"

// IDE I/O ports (primary channel)
#define IDE_PRIMARY_DATA        0x1F0
#define IDE_PRIMARY_ERROR       0x1F1
#define IDE_PRIMARY_FEATURES    0x1F1
#define IDE_PRIMARY_SECTOR_COUNT 0x1F2
#define IDE_PRIMARY_LBA_LOW     0x1F3
#define IDE_PRIMARY_LBA_MID     0x1F4
#define IDE_PRIMARY_LBA_HIGH    0x1F5
#define IDE_PRIMARY_DRIVE_HEAD  0x1F6
#define IDE_PRIMARY_STATUS      0x1F7
#define IDE_PRIMARY_COMMAND     0x1F7
#define IDE_PRIMARY_CONTROL     0x3F6

// IDE I/O ports (secondary channel)
#define IDE_SECONDARY_DATA      0x170
#define IDE_SECONDARY_ERROR     0x171
#define IDE_SECONDARY_FEATURES  0x171
#define IDE_SECONDARY_SECTOR_COUNT 0x172
#define IDE_SECONDARY_LBA_LOW   0x173
#define IDE_SECONDARY_LBA_MID   0x174
#define IDE_SECONDARY_LBA_HIGH  0x175
#define IDE_SECONDARY_DRIVE_HEAD 0x176
#define IDE_SECONDARY_STATUS    0x177
#define IDE_SECONDARY_COMMAND   0x177
#define IDE_SECONDARY_CONTROL   0x376

// IDE commands
#define IDE_CMD_READ_SECTORS    0x20
#define IDE_CMD_READ_SECTORS_EXT 0x24
#define IDE_CMD_WRITE_SECTORS   0x30
#define IDE_CMD_WRITE_SECTORS_EXT 0x34
#define IDE_CMD_CACHE_FLUSH     0xE7
#define IDE_CMD_IDENTIFY        0xEC

// IDE status register bits
#define IDE_STATUS_ERR          0x01
#define IDE_STATUS_IDX          0x02
#define IDE_STATUS_CORR         0x04
#define IDE_STATUS_DRQ          0x08
#define IDE_STATUS_SRV          0x10
#define IDE_STATUS_DF           0x20
#define IDE_STATUS_RDY          0x40
#define IDE_STATUS_BSY          0x80

// IDE drive selection
#define IDE_DRIVE_MASTER        0
#define IDE_DRIVE_SLAVE         1

// IDE device info
typedef struct {
    uint16_t type;              // Device type
    uint16_t cylinders;         // Number of cylinders
    uint16_t heads;             // Number of heads
    uint16_t sectors;           // Sectors per track
    uint32_t total_sectors;     // Total sectors (LBA)
    uint16_t capabilities;      // Capabilities
    char     model[41];         // Model string
    char     serial[21];        // Serial number
    uint8_t  present;           // Is device present?
} ide_device_t;

// IDE channel
typedef struct {
    uint16_t base;              // Base I/O port
    uint16_t ctrl;              // Control I/O port
    uint8_t  irq;               // IRQ number
    ide_device_t master;        // Master device
    ide_device_t slave;         // Slave device
} ide_channel_t;

// IDE driver API
int ide_init(void);
int ide_detect_devices(void);
int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer);
int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buffer);
int ide_read_sector(uint8_t drive, uint32_t lba, void *buffer);
int ide_write_sector(uint8_t drive, uint32_t lba, const void *buffer);
ide_device_t *ide_get_device(uint8_t drive);
int ide_identify(uint8_t drive);

// Internal functions
void ide_wait_ready(uint16_t base);
void ide_wait_drq(uint16_t base);
uint8_t ide_status(uint16_t base);
void ide_select_drive(uint16_t base, uint8_t drive);
int ide_wait_ready_timeout(uint16_t base, uint32_t timeout_ms);
int ide_wait_drq_timeout(uint16_t base, uint32_t timeout_ms);

#endif // IDE_H
