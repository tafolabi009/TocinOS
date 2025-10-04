/**
 * TocinOS AHCI/SATA Driver Implementation
 * 
 * Implementation of AHCI driver for modern SATA controllers
 */

#include "../include/drivers/ahci.h"
#include "../include/drivers/mdf.h"
#include "../include/kernel/memory.h"

static ahci_controller_t ahci_ctrl = {0};
static int ahci_initialized = 0;

/**
 * Read port register
 */
static inline uint32_t ahci_port_read(ahci_hba_port_t *port, uint32_t reg) {
    volatile uint32_t *ptr = (volatile uint32_t *)((uint8_t *)port + reg);
    return *ptr;
}

/**
 * Write port register
 */
static inline void ahci_port_write(ahci_hba_port_t *port, uint32_t reg, uint32_t value) {
    volatile uint32_t *ptr = (volatile uint32_t *)((uint8_t *)port + reg);
    *ptr = value;
}

/**
 * Check device type on port
 */
int ahci_check_type(ahci_hba_port_t *port) {
    uint32_t ssts = ahci_port_read(port, AHCI_PORT_SSTS);
    
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    if (det != 3) {  // Check device detection
        return AHCI_DEV_NULL;
    }
    if (ipm != 1) {  // Check interface power management
        return AHCI_DEV_NULL;
    }
    
    uint32_t sig = ahci_port_read(port, AHCI_PORT_SIG);
    
    switch (sig) {
        case 0xEB140101:  // SATAPI signature
            return AHCI_DEV_SATAPI;
        case 0xC33C0101:  // SEMB signature
            return AHCI_DEV_SEMB;
        case 0x96690101:  // Port multiplier signature
            return AHCI_DEV_PM;
        case 0x00000101:  // SATA drive signature
            return AHCI_DEV_SATA;
        default:
            return AHCI_DEV_NULL;
    }
}

/**
 * Start port command engine
 */
int ahci_port_start(ahci_hba_port_t *port) {
    // Wait until CR (bit 15) is cleared
    while (ahci_port_read(port, AHCI_PORT_CMD) & AHCI_PORT_CMD_CR) {
        // Wait
    }
    
    // Set FRE (bit 4) and ST (bit 0)
    uint32_t cmd = ahci_port_read(port, AHCI_PORT_CMD);
    cmd |= AHCI_PORT_CMD_FRE;
    ahci_port_write(port, AHCI_PORT_CMD, cmd);
    
    cmd |= AHCI_PORT_CMD_ST;
    ahci_port_write(port, AHCI_PORT_CMD, cmd);
    
    return 0;
}

/**
 * Stop port command engine
 */
int ahci_port_stop(ahci_hba_port_t *port) {
    // Clear ST (bit 0)
    uint32_t cmd = ahci_port_read(port, AHCI_PORT_CMD);
    cmd &= ~AHCI_PORT_CMD_ST;
    ahci_port_write(port, AHCI_PORT_CMD, cmd);
    
    // Wait until CR (bit 15) is cleared
    while (ahci_port_read(port, AHCI_PORT_CMD) & AHCI_PORT_CMD_CR) {
        // Wait
    }
    
    // Clear FRE (bit 4)
    cmd = ahci_port_read(port, AHCI_PORT_CMD);
    cmd &= ~AHCI_PORT_CMD_FRE;
    ahci_port_write(port, AHCI_PORT_CMD, cmd);
    
    // Wait until FR (bit 14) is cleared
    while (ahci_port_read(port, AHCI_PORT_CMD) & AHCI_PORT_CMD_FR) {
        // Wait
    }
    
    return 0;
}

/**
 * Rebase port (allocate command list and FIS memory)
 */
int ahci_port_rebase(ahci_controller_t *ctrl, int port_num) {
    if (!ctrl || port_num >= 32) {
        return -1;
    }
    
    ahci_hba_port_t *port = &ctrl->abar->ports[port_num];
    
    // Stop command engine
    ahci_port_stop(port);
    
    // Allocate command list (1KB aligned)
    void *clb = pmm_alloc_page();
    if (!clb) {
        return -1;
    }
    
    ahci_port_write(port, AHCI_PORT_CLB, (uint32_t)(uintptr_t)clb);
    ahci_port_write(port, AHCI_PORT_CLBU, 0);
    
    // Clear command list
    for (int i = 0; i < 1024; i++) {
        ((uint8_t *)clb)[i] = 0;
    }
    
    // Allocate FIS (256-byte aligned)
    void *fb = pmm_alloc_page();
    if (!fb) {
        pmm_free_page(clb);
        return -1;
    }
    
    ahci_port_write(port, AHCI_PORT_FB, (uint32_t)(uintptr_t)fb);
    ahci_port_write(port, AHCI_PORT_FBU, 0);
    
    // Clear FIS
    for (int i = 0; i < 256; i++) {
        ((uint8_t *)fb)[i] = 0;
    }
    
    // Allocate command tables
    ahci_cmd_header_t *cmdheader = (ahci_cmd_header_t *)clb;
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 8; // 8 PRDT entries per command table
        
        void *ctba = pmm_alloc_page();
        if (!ctba) {
            // TODO: Free previously allocated pages
            return -1;
        }
        
        cmdheader[i].ctba = (uint32_t)(uintptr_t)ctba;
        cmdheader[i].ctbau = 0;
        
        // Clear command table
        for (int j = 0; j < 256; j++) {
            ((uint8_t *)ctba)[j] = 0;
        }
    }
    
    // Start command engine
    ahci_port_start(port);
    
    return 0;
}

/**
 * Initialize AHCI controller
 */
int ahci_init(void) {
    if (ahci_initialized) {
        return 0;
    }
    
    // TODO: Find AHCI controller via PCI
    // For now, assume it's at a known address
    ahci_ctrl.abar = 0; // NULL for now
    
    if (!ahci_ctrl.abar) {
        return -1;
    }
    
    // Enable AHCI mode
    uint32_t ghc = ahci_ctrl.abar->ghc;
    ghc |= AHCI_GHC_AE;
    ahci_ctrl.abar->ghc = ghc;
    
    // Determine number of ports
    uint32_t pi = ahci_ctrl.abar->pi;
    ahci_ctrl.num_ports = 0;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            ahci_ctrl.num_ports++;
        }
    }
    
    ahci_initialized = 1;
    return 0;
}

/**
 * Probe for AHCI devices
 */
int ahci_probe(void) {
    if (!ahci_initialized) {
        return -1;
    }
    
    return ahci_detect_devices(&ahci_ctrl);
}

/**
 * Detect devices on all ports
 */
int ahci_detect_devices(ahci_controller_t *ctrl) {
    if (!ctrl) {
        return -1;
    }
    
    uint32_t pi = ctrl->abar->pi;
    
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1 << i))) {
            continue;
        }
        
        ahci_hba_port_t *port = &ctrl->abar->ports[i];
        int type = ahci_check_type(port);
        
        if (type == AHCI_DEV_SATA || type == AHCI_DEV_SATAPI) {
            // Rebase port
            if (ahci_port_rebase(ctrl, i) != 0) {
                continue;
            }
            
            ctrl->devices[i].port_num = i;
            ctrl->devices[i].type = type;
            
            // TODO: Get device info via IDENTIFY command
        }
    }
    
    return 0;
}

/**
 * Read sectors from AHCI device
 */
int ahci_read(uint8_t port, uint64_t lba, uint32_t count, void *buffer) {
    if (!ahci_initialized || port >= 32 || !buffer) {
        return -1;
    }
    
    ahci_hba_port_t *hba_port = &ahci_ctrl.abar->ports[port];
    
    // Clear interrupt status
    ahci_port_write(hba_port, AHCI_PORT_IS, (uint32_t)-1);
    
    // Get command header
    uint32_t clb = ahci_port_read(hba_port, AHCI_PORT_CLB);
    ahci_cmd_header_t *cmdheader = (ahci_cmd_header_t *)(uintptr_t)clb;
    
    // Find free command slot
    uint32_t ci = ahci_port_read(hba_port, AHCI_PORT_CI);
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        if (!(ci & (1 << i))) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return -1; // No free slots
    }
    
    // Setup command header
    cmdheader[slot].cfl = sizeof(fis_reg_h2d_t) / 4;
    cmdheader[slot].w = 0; // Read
    cmdheader[slot].prdtl = 1; // One PRDT entry
    
    // Get command table
    ahci_cmd_table_t *cmdtbl = (ahci_cmd_table_t *)(uintptr_t)cmdheader[slot].ctba;
    
    // Setup PRDT
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = count * 512 - 1;
    cmdtbl->prdt_entry[0].i = 1;
    
    // Setup command FIS
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)cmdtbl->cfis;
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = ATA_CMD_READ_DMA_EX;
    
    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    
    cmdfis->device = 1 << 6; // LBA mode
    
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;
    
    // Issue command
    ahci_port_write(hba_port, AHCI_PORT_CI, 1 << slot);
    
    // Wait for completion
    while (ahci_port_read(hba_port, AHCI_PORT_CI) & (1 << slot)) {
        // TODO: Add timeout
    }
    
    return count;
}

/**
 * Write sectors to AHCI device
 */
int ahci_write(uint8_t port, uint64_t lba, uint32_t count, const void *buffer) {
    if (!ahci_initialized || port >= 32 || !buffer) {
        return -1;
    }
    
    // TODO: Implement write (similar to read)
    
    return -1;
}

/**
 * Identify AHCI device
 */
int ahci_identify(uint8_t port, void *buffer) {
    if (!ahci_initialized || port >= 32 || !buffer) {
        return -1;
    }
    
    // TODO: Send IDENTIFY command
    
    return -1;
}
