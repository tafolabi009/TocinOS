/**
 * TocinOS Network Driver Implementation (NE2000)
 * 
 * Implements basic NE2000 compatible network card driver
 */

#include "../include/drivers/net.h"
#include "../include/drivers/mdf.h"

// Global network device
static net_device_t net_device = {0};
static int net_initialized = 0;

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
 * Small delay
 */
static void net_delay(void) {
    for (volatile int i = 0; i < 10000; i++);
}

/**
 * Reset network card
 */
void net_reset(void) {
    uint16_t base = net_device.base_addr;
    
    // Read reset port
    inb(base + NE2000_RESET);
    net_delay();
    
    // Write to reset port
    outb(base + NE2000_RESET, 0xFF);
    net_delay();
    
    // Wait for reset to complete
    while (!(inb(base + NE2000_ISR) & NE2000_ISR_RST)) {
        // Wait
    }
    
    // Clear ISR
    outb(base + NE2000_ISR, 0xFF);
}

/**
 * Start network card
 */
void net_start(void) {
    uint16_t base = net_device.base_addr;
    
    // Stop the card
    outb(base + NE2000_COMMAND, NE2000_CMD_STOP | NE2000_CMD_NODMA | NE2000_CMD_PAGE0);
    net_delay();
    
    // Initialize DCR
    outb(base + NE2000_DCR, 0x49);
    
    // Clear remote byte count
    outb(base + NE2000_RBCR0, 0);
    outb(base + NE2000_RBCR1, 0);
    
    // Set RCR to monitor mode
    outb(base + NE2000_RCR, 0x20);
    
    // Set TCR to internal loopback
    outb(base + NE2000_TCR, 0x02);
    
    // Set page start and stop
    outb(base + NE2000_PAGESTART, 0x40);
    outb(base + NE2000_PAGESTOP, 0x80);
    outb(base + NE2000_BOUNDARY, 0x40);
    
    // Clear ISR
    outb(base + NE2000_ISR, 0xFF);
    
    // Enable interrupts
    outb(base + NE2000_IMR, 0x0F);
    
    // Start the card
    outb(base + NE2000_COMMAND, NE2000_CMD_START | NE2000_CMD_NODMA | NE2000_CMD_PAGE0);
    
    // Set RCR to accept broadcast
    outb(base + NE2000_RCR, 0x04);
    
    // Set TCR to normal mode
    outb(base + NE2000_TCR, 0x00);
}

/**
 * Stop network card
 */
void net_stop(void) {
    uint16_t base = net_device.base_addr;
    outb(base + NE2000_COMMAND, NE2000_CMD_STOP | NE2000_CMD_NODMA | NE2000_CMD_PAGE0);
}

/**
 * Detect network card
 */
int net_detect(void) {
    // Try common base addresses
    uint16_t addresses[] = {0x300, 0x320, 0x340, 0x360};
    
    for (int i = 0; i < 4; i++) {
        net_device.base_addr = addresses[i];
        
        // Try to reset card
        net_reset();
        
        // Check if card responds
        uint8_t test = inb(net_device.base_addr + NE2000_COMMAND);
        if (test != 0xFF) {
            net_device.present = 1;
            net_device.irq = 10; // Default IRQ
            return 0;
        }
    }
    
    net_device.present = 0;
    return -1;
}

/**
 * Read MAC address
 */
int net_get_mac_address(uint8_t *mac) {
    if (!net_initialized || !net_device.present || !mac) {
        return -1;
    }
    
    uint16_t base = net_device.base_addr;
    
    // Switch to page 1
    outb(base + NE2000_COMMAND, NE2000_CMD_STOP | NE2000_CMD_NODMA | NE2000_CMD_PAGE1);
    
    // Read MAC address from PAR0-PAR5
    for (int i = 0; i < 6; i++) {
        mac[i] = inb(base + 0x01 + i);
        net_device.mac_addr[i] = mac[i];
    }
    
    // Switch back to page 0
    outb(base + NE2000_COMMAND, NE2000_CMD_START | NE2000_CMD_NODMA | NE2000_CMD_PAGE0);
    
    return 0;
}

/**
 * Initialize network driver
 */
int net_init(void) {
    if (net_initialized) {
        return 0;
    }
    
    // Detect network card
    if (net_detect() != 0) {
        return -1;
    }
    
    // Start network card
    net_start();
    
    // Get MAC address
    net_get_mac_address(net_device.mac_addr);
    
    net_initialized = 1;
    return 0;
}

/**
 * Send packet
 */
int net_send_packet(const void *data, uint16_t length) {
    if (!net_initialized || !net_device.present || !data || length == 0) {
        return -1;
    }
    
    uint16_t base = net_device.base_addr;
    
    // Ensure minimum packet size
    if (length < 60) {
        length = 60;
    }
    
    // Setup remote DMA for write
    outb(base + NE2000_COMMAND, NE2000_CMD_START | NE2000_CMD_NODMA | NE2000_CMD_PAGE0);
    outb(base + NE2000_RSAR0, 0x00);
    outb(base + NE2000_RSAR1, 0x40);
    outb(base + NE2000_RBCR0, length & 0xFF);
    outb(base + NE2000_RBCR1, (length >> 8) & 0xFF);
    outb(base + NE2000_COMMAND, NE2000_CMD_START | NE2000_CMD_RWRITE);
    
    // Write data
    const uint16_t *buf = (const uint16_t *)data;
    for (uint16_t i = 0; i < (length + 1) / 2; i++) {
        outb(base + NE2000_DATA, buf[i] & 0xFF);
        outb(base + NE2000_DATA, (buf[i] >> 8) & 0xFF);
    }
    
    // Wait for DMA to complete
    while (!(inb(base + NE2000_ISR) & NE2000_ISR_RDC)) {
        // Wait
    }
    outb(base + NE2000_ISR, NE2000_ISR_RDC);
    
    // Transmit packet
    outb(base + NE2000_TPSR, 0x40);
    outb(base + NE2000_TBCR0, length & 0xFF);
    outb(base + NE2000_TBCR1, (length >> 8) & 0xFF);
    outb(base + NE2000_COMMAND, NE2000_CMD_START | NE2000_CMD_TRANSMIT | NE2000_CMD_NODMA);
    
    // Wait for transmission to complete
    while (!(inb(base + NE2000_ISR) & NE2000_ISR_PTX)) {
        if (inb(base + NE2000_ISR) & NE2000_ISR_TXE) {
            net_device.tx_errors++;
            return -1;
        }
    }
    
    outb(base + NE2000_ISR, NE2000_ISR_PTX);
    net_device.tx_packets++;
    
    return length;
}

/**
 * Receive packet
 */
int net_receive_packet(void *buffer, uint16_t max_length) {
    if (!net_initialized || !net_device.present || !buffer) {
        return -1;
    }
    
    uint16_t base = net_device.base_addr;
    
    // Check if packet received
    if (!(inb(base + NE2000_ISR) & NE2000_ISR_PRX)) {
        return 0; // No packet
    }
    
    // Clear interrupt
    outb(base + NE2000_ISR, NE2000_ISR_PRX);
    net_device.rx_packets++;
    
    // This is a simplified version
    // Real implementation would read packet header and data
    (void)max_length;
    
    return 0;
}

/**
 * Get network device info
 */
net_device_t *net_get_device(void) {
    return &net_device;
}

/**
 * IRQ handler
 */
void net_irq_handler(void) {
    if (!net_initialized || !net_device.present) {
        return;
    }
    
    uint16_t base = net_device.base_addr;
    uint8_t isr = inb(base + NE2000_ISR);
    
    // Handle interrupts
    if (isr & NE2000_ISR_PRX) {
        // Packet received
        net_device.rx_packets++;
    }
    
    if (isr & NE2000_ISR_PTX) {
        // Packet transmitted
        net_device.tx_packets++;
    }
    
    if (isr & NE2000_ISR_RXE) {
        // Receive error
        net_device.rx_errors++;
    }
    
    if (isr & NE2000_ISR_TXE) {
        // Transmit error
        net_device.tx_errors++;
    }
    
    // Clear ISR
    outb(base + NE2000_ISR, isr);
}

/**
 * Network driver initialization for MDF
 */
static int net_driver_init(void) {
    return net_init();
}

/**
 * Network driver probe
 */
static int net_driver_probe(void) {
    return net_detect();
}

/**
 * Register network driver with MDF
 */
void net_driver_register(void) {
    mdf_register_driver("net", DRIVER_TYPE_NETWORK, net_driver_init, net_driver_probe);
}
