/**
 * TocinOS AHCI/SATA Driver
 * 
 * Advanced Host Controller Interface driver for modern SATA controllers
 */

#ifndef AHCI_H
#define AHCI_H

#include "../stdint.h"

// AHCI Generic Host Control Registers
#define AHCI_HBA_CAP        0x00    // Host Capabilities
#define AHCI_HBA_GHC        0x04    // Global Host Control
#define AHCI_HBA_IS         0x08    // Interrupt Status
#define AHCI_HBA_PI         0x0C    // Ports Implemented
#define AHCI_HBA_VS         0x10    // Version
#define AHCI_HBA_CCC_CTL    0x14    // Command Completion Coalescing Control
#define AHCI_HBA_CCC_PORTS  0x18    // Command Completion Coalescing Ports
#define AHCI_HBA_EM_LOC     0x1C    // Enclosure Management Location
#define AHCI_HBA_EM_CTL     0x20    // Enclosure Management Control
#define AHCI_HBA_CAP2       0x24    // Host Capabilities Extended
#define AHCI_HBA_BOHC       0x28    // BIOS/OS Handoff Control and Status

// Port Registers (offset 0x100 + port_num * 0x80)
#define AHCI_PORT_CLB       0x00    // Command List Base Address
#define AHCI_PORT_CLBU      0x04    // Command List Base Address Upper
#define AHCI_PORT_FB        0x08    // FIS Base Address
#define AHCI_PORT_FBU       0x0C    // FIS Base Address Upper
#define AHCI_PORT_IS        0x10    // Interrupt Status
#define AHCI_PORT_IE        0x14    // Interrupt Enable
#define AHCI_PORT_CMD       0x18    // Command and Status
#define AHCI_PORT_TFD       0x20    // Task File Data
#define AHCI_PORT_SIG       0x24    // Signature
#define AHCI_PORT_SSTS      0x28    // SATA Status
#define AHCI_PORT_SCTL      0x2C    // SATA Control
#define AHCI_PORT_SERR      0x30    // SATA Error
#define AHCI_PORT_SACT      0x34    // SATA Active
#define AHCI_PORT_CI        0x38    // Command Issue

// GHC bits
#define AHCI_GHC_HR         (1 << 0)  // HBA Reset
#define AHCI_GHC_IE         (1 << 1)  // Interrupt Enable
#define AHCI_GHC_AE         (1 << 31) // AHCI Enable

// Port CMD bits
#define AHCI_PORT_CMD_ST    (1 << 0)  // Start
#define AHCI_PORT_CMD_FRE   (1 << 4)  // FIS Receive Enable
#define AHCI_PORT_CMD_FR    (1 << 14) // FIS Receive Running
#define AHCI_PORT_CMD_CR    (1 << 15) // Command List Running

// Device types
#define AHCI_DEV_NULL       0
#define AHCI_DEV_SATA       1
#define AHCI_DEV_SATAPI     2
#define AHCI_DEV_SEMB       3
#define AHCI_DEV_PM         4

// FIS Types
#define FIS_TYPE_REG_H2D    0x27  // Register FIS - host to device
#define FIS_TYPE_REG_D2H    0x34  // Register FIS - device to host
#define FIS_TYPE_DMA_ACT    0x39  // DMA activate FIS
#define FIS_TYPE_DMA_SETUP  0x41  // DMA setup FIS
#define FIS_TYPE_DATA       0x46  // Data FIS
#define FIS_TYPE_BIST       0x58  // BIST activate FIS
#define FIS_TYPE_PIO_SETUP  0x5F  // PIO setup FIS
#define FIS_TYPE_DEV_BITS   0xA1  // Set device bits FIS

// ATA Commands
#define ATA_CMD_READ_DMA    0xC8
#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA   0xCA
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_IDENTIFY    0xEC

// FIS Register - Host to Device
typedef struct {
    uint8_t  fis_type;      // FIS_TYPE_REG_H2D
    uint8_t  pmport:4;      // Port multiplier
    uint8_t  rsv0:3;        // Reserved
    uint8_t  c:1;           // 1: Command, 0: Control
    uint8_t  command;       // Command register
    uint8_t  featurel;      // Feature register, 7:0
    
    uint8_t  lba0;          // LBA low register, 7:0
    uint8_t  lba1;          // LBA mid register, 15:8
    uint8_t  lba2;          // LBA high register, 23:16
    uint8_t  device;        // Device register
    
    uint8_t  lba3;          // LBA register, 31:24
    uint8_t  lba4;          // LBA register, 39:32
    uint8_t  lba5;          // LBA register, 47:40
    uint8_t  featureh;      // Feature register, 15:8
    
    uint8_t  countl;        // Count register, 7:0
    uint8_t  counth;        // Count register, 15:8
    uint8_t  icc;           // Isochronous command completion
    uint8_t  control;       // Control register
    
    uint8_t  rsv1[4];       // Reserved
} __attribute__((packed)) fis_reg_h2d_t;

// FIS Register - Device to Host
typedef struct {
    uint8_t  fis_type;      // FIS_TYPE_REG_D2H
    uint8_t  pmport:4;      // Port multiplier
    uint8_t  rsv0:2;        // Reserved
    uint8_t  i:1;           // Interrupt bit
    uint8_t  rsv1:1;        // Reserved
    uint8_t  status;        // Status register
    uint8_t  error;         // Error register
    
    uint8_t  lba0;          // LBA low register, 7:0
    uint8_t  lba1;          // LBA mid register, 15:8
    uint8_t  lba2;          // LBA high register, 23:16
    uint8_t  device;        // Device register
    
    uint8_t  lba3;          // LBA register, 31:24
    uint8_t  lba4;          // LBA register, 39:32
    uint8_t  lba5;          // LBA register, 47:40
    uint8_t  rsv2;          // Reserved
    
    uint8_t  countl;        // Count register, 7:0
    uint8_t  counth;        // Count register, 15:8
    uint8_t  rsv3[2];       // Reserved
    
    uint8_t  rsv4[4];       // Reserved
} __attribute__((packed)) fis_reg_d2h_t;

// Command Header
typedef struct {
    uint8_t  cfl:5;         // Command FIS length in DWORDS, 2 ~ 16
    uint8_t  a:1;           // ATAPI
    uint8_t  w:1;           // Write, 1: H2D, 0: D2H
    uint8_t  p:1;           // Prefetchable
    
    uint8_t  r:1;           // Reset
    uint8_t  b:1;           // BIST
    uint8_t  c:1;           // Clear busy upon R_OK
    uint8_t  rsv0:1;        // Reserved
    uint8_t  pmp:4;         // Port multiplier port
    
    uint16_t prdtl;         // Physical region descriptor table length
    uint32_t prdbc;         // Physical region descriptor byte count
    uint32_t ctba;          // Command table descriptor base address
    uint32_t ctbau;         // Command table descriptor base address upper 32 bits
    uint32_t rsv1[4];       // Reserved
} __attribute__((packed)) ahci_cmd_header_t;

// Physical Region Descriptor Table Entry
typedef struct {
    uint32_t dba;           // Data base address
    uint32_t dbau;          // Data base address upper 32 bits
    uint32_t rsv0;          // Reserved
    uint32_t dbc:22;        // Byte count, 4M max
    uint32_t rsv1:9;        // Reserved
    uint32_t i:1;           // Interrupt on completion
} __attribute__((packed)) ahci_prdt_entry_t;

// Command Table
typedef struct {
    uint8_t  cfis[64];      // Command FIS
    uint8_t  acmd[16];      // ATAPI command, 12 or 16 bytes
    uint8_t  rsv[48];       // Reserved
    ahci_prdt_entry_t prdt_entry[1]; // Physical region descriptor table entries (1 ~ 65535)
} __attribute__((packed)) ahci_cmd_table_t;

// HBA Port
typedef struct {
    uint32_t clb;           // Command list base address, 1K-byte aligned
    uint32_t clbu;          // Command list base address upper 32 bits
    uint32_t fb;            // FIS base address, 256-byte aligned
    uint32_t fbu;           // FIS base address upper 32 bits
    uint32_t is;            // Interrupt status
    uint32_t ie;            // Interrupt enable
    uint32_t cmd;           // Command and status
    uint32_t rsv0;          // Reserved
    uint32_t tfd;           // Task file data
    uint32_t sig;           // Signature
    uint32_t ssts;          // SATA status (SCR0:SStatus)
    uint32_t sctl;          // SATA control (SCR2:SControl)
    uint32_t serr;          // SATA error (SCR1:SError)
    uint32_t sact;          // SATA active (SCR3:SActive)
    uint32_t ci;            // Command issue
    uint32_t sntf;          // SATA notification (SCR4:SNotification)
    uint32_t fbs;           // FIS-based switch control
    uint32_t rsv1[11];      // Reserved
    uint32_t vendor[4];     // Vendor specific
} __attribute__((packed)) ahci_hba_port_t;

// HBA Memory Registers
typedef struct {
    uint32_t cap;           // Host capability
    uint32_t ghc;           // Global host control
    uint32_t is;            // Interrupt status
    uint32_t pi;            // Port implemented
    uint32_t vs;            // Version
    uint32_t ccc_ctl;       // Command completion coalescing control
    uint32_t ccc_pts;       // Command completion coalescing ports
    uint32_t em_loc;        // Enclosure management location
    uint32_t em_ctl;        // Enclosure management control
    uint32_t cap2;          // Host capabilities extended
    uint32_t bohc;          // BIOS/OS handoff control and status
    uint8_t  rsv[0xA0 - 0x2C]; // Reserved
    uint8_t  vendor[0x100 - 0xA0]; // Vendor specific registers
    ahci_hba_port_t ports[32]; // Port control registers (up to 32 ports)
} __attribute__((packed)) ahci_hba_mem_t;

// AHCI device info
typedef struct {
    uint8_t  port_num;      // Port number
    uint8_t  type;          // Device type
    uint32_t sectors;       // Number of sectors
    char     model[41];     // Model string
    char     serial[21];    // Serial number
} ahci_device_t;

// AHCI controller
typedef struct {
    ahci_hba_mem_t *abar;   // HBA memory base address
    uint32_t num_ports;     // Number of ports
    ahci_device_t devices[32]; // Device info for each port
} ahci_controller_t;

// AHCI API
int ahci_init(void);
int ahci_probe(void);
int ahci_detect_devices(ahci_controller_t *ctrl);
int ahci_port_rebase(ahci_controller_t *ctrl, int port);

// I/O operations
int ahci_read(uint8_t port, uint64_t lba, uint32_t count, void *buffer);
int ahci_write(uint8_t port, uint64_t lba, uint32_t count, const void *buffer);
int ahci_identify(uint8_t port, void *buffer);

// Port management
int ahci_port_start(ahci_hba_port_t *port);
int ahci_port_stop(ahci_hba_port_t *port);
int ahci_check_type(ahci_hba_port_t *port);

#endif // AHCI_H
