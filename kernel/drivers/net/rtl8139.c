/**
 * TocinOS Realtek RTL8139 Fast Ethernet Driver
 * 
 * Supports RTL8139 10/100 Mbps Ethernet NICs.
 * Common in older hardware and virtual machines.
 * 
 * @author TocinOS Team
 */

#include "../../../include/drivers/net.h"
#include "../../../include/kernel/memory.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* External functions */
extern void serial_printf(const char *fmt, ...);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* Port I/O */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %w1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ================================================================
 * RTL8139 PCI IDENTIFIERS
 * ================================================================ */

#define RTL8139_VENDOR_ID       0x10EC  /* Realtek */
#define RTL8139_DEVICE_ID       0x8139

/* ================================================================
 * RTL8139 REGISTER DEFINITIONS
 * ================================================================ */

/* MAC Address registers */
#define RTL8139_IDR0            0x00    /* MAC address bytes 0-3 */
#define RTL8139_IDR4            0x04    /* MAC address bytes 4-5 */

/* Multicast registers */
#define RTL8139_MAR0            0x08    /* Multicast filter 0-3 */
#define RTL8139_MAR4            0x0C    /* Multicast filter 4-7 */

/* Transmit status registers (4 descriptors) */
#define RTL8139_TSD0            0x10    /* TX Status Descriptor 0 */
#define RTL8139_TSD1            0x14
#define RTL8139_TSD2            0x18
#define RTL8139_TSD3            0x1C

/* Transmit start address registers */
#define RTL8139_TSAD0           0x20    /* TX Start Address Descriptor 0 */
#define RTL8139_TSAD1           0x24
#define RTL8139_TSAD2           0x28
#define RTL8139_TSAD3           0x2C

/* Receive buffer start address */
#define RTL8139_RBSTART         0x30

/* Early RX byte count */
#define RTL8139_ERBCR           0x34

/* Early RX status */
#define RTL8139_ERSR            0x36

/* Command register */
#define RTL8139_CR              0x37
#define RTL8139_CR_RST          (1 << 4)  /* Reset */
#define RTL8139_CR_RE           (1 << 3)  /* Receiver Enable */
#define RTL8139_CR_TE           (1 << 2)  /* Transmitter Enable */
#define RTL8139_CR_BUFE         (1 << 0)  /* Buffer Empty */

/* Current address of packet read */
#define RTL8139_CAPR            0x38

/* Current buffer address */
#define RTL8139_CBR             0x3A

/* Interrupt mask register */
#define RTL8139_IMR             0x3C

/* Interrupt status register */
#define RTL8139_ISR             0x3E
#define RTL8139_INT_ROK         (1 << 0)   /* Receive OK */
#define RTL8139_INT_RER         (1 << 1)   /* Receive Error */
#define RTL8139_INT_TOK         (1 << 2)   /* Transmit OK */
#define RTL8139_INT_TER         (1 << 3)   /* Transmit Error */
#define RTL8139_INT_RXOVW       (1 << 4)   /* RX Buffer Overflow */
#define RTL8139_INT_PUN         (1 << 5)   /* Packet Underrun/Link Change */
#define RTL8139_INT_FOVW        (1 << 6)   /* RX FIFO Overflow */
#define RTL8139_INT_LENCHG      (1 << 13)  /* Cable Length Change */
#define RTL8139_INT_TIMEOUT     (1 << 14)  /* Timeout */
#define RTL8139_INT_SERR        (1 << 15)  /* System Error */

/* Transmit configuration register */
#define RTL8139_TCR             0x40
#define RTL8139_TCR_CLRABT      (1 << 0)   /* Clear Abort */
#define RTL8139_TCR_MXDMA_2048  (7 << 8)   /* Max DMA Burst 2048 bytes */
#define RTL8139_TCR_IFG_NORMAL  (3 << 24)  /* Interframe gap normal */

/* Receive configuration register */
#define RTL8139_RCR             0x44
#define RTL8139_RCR_AAP         (1 << 0)   /* Accept All Packets */
#define RTL8139_RCR_APM         (1 << 1)   /* Accept Physical Match */
#define RTL8139_RCR_AM          (1 << 2)   /* Accept Multicast */
#define RTL8139_RCR_AB          (1 << 3)   /* Accept Broadcast */
#define RTL8139_RCR_AR          (1 << 4)   /* Accept Runt */
#define RTL8139_RCR_AER         (1 << 5)   /* Accept Error Packets */
#define RTL8139_RCR_WRAP        (1 << 7)   /* Wrap */
#define RTL8139_RCR_MXDMA_UNLIM (7 << 8)   /* Max DMA Burst Unlimited */
#define RTL8139_RCR_RBLEN_8K    (0 << 11)  /* Buffer Length 8K + 16 */
#define RTL8139_RCR_RBLEN_16K   (1 << 11)  /* Buffer Length 16K + 16 */
#define RTL8139_RCR_RBLEN_32K   (2 << 11)  /* Buffer Length 32K + 16 */
#define RTL8139_RCR_RBLEN_64K   (3 << 11)  /* Buffer Length 64K + 16 */
#define RTL8139_RCR_RXFTH_NONE  (7 << 13)  /* No RX threshold */

/* Timer count register */
#define RTL8139_TCTR            0x48

/* Missed packet counter */
#define RTL8139_MPC             0x4C

/* 93C46 command register */
#define RTL8139_9346CR          0x50
#define RTL8139_9346CR_EEM_NORMAL   0x00
#define RTL8139_9346CR_EEM_AUTOLOAD 0x40
#define RTL8139_9346CR_EEM_PROGRAM  0x80
#define RTL8139_9346CR_EEM_CONFIG   0xC0

/* Config registers */
#define RTL8139_CONFIG0         0x51
#define RTL8139_CONFIG1         0x52
#define RTL8139_CONFIG3         0x59
#define RTL8139_CONFIG4         0x5A

/* Timer interrupt register */
#define RTL8139_TIMER           0x54

/* Media status register */
#define RTL8139_MSR             0x58
#define RTL8139_MSR_RXPF        (1 << 0)   /* RX Pause Flag */
#define RTL8139_MSR_TXPF        (1 << 1)   /* TX Pause Flag */
#define RTL8139_MSR_LINKB       (1 << 2)   /* Inverse of Link Status */
#define RTL8139_MSR_SPEED_10    (1 << 3)   /* 10 Mbps */
#define RTL8139_MSR_RXFCE       (1 << 6)   /* RX Flow Control Enable */
#define RTL8139_MSR_TXFCE       (1 << 7)   /* TX Flow Control Enable */

/* Basic mode control register */
#define RTL8139_BMCR            0x62
#define RTL8139_BMSR            0x64

/* ================================================================
 * PACKET HEADER
 * ================================================================ */

#define RTL8139_ROK             (1 << 0)   /* Receive OK */
#define RTL8139_FAE             (1 << 1)   /* Frame Alignment Error */
#define RTL8139_CRC             (1 << 2)   /* CRC Error */
#define RTL8139_LONG            (1 << 3)   /* Long Packet */
#define RTL8139_RUNT            (1 << 4)   /* Runt Packet */
#define RTL8139_ISE             (1 << 5)   /* Invalid Symbol Error */
#define RTL8139_BAR             (1 << 13)  /* Broadcast Address Received */
#define RTL8139_PAM             (1 << 14)  /* Physical Address Matched */
#define RTL8139_MAR             (1 << 15)  /* Multicast Address Received */

/* TX Status bits */
#define RTL8139_TSD_OWN         (1 << 13)  /* DMA completed */
#define RTL8139_TSD_TUN         (1 << 14)  /* TX FIFO Underrun */
#define RTL8139_TSD_TOK         (1 << 15)  /* TX OK */
#define RTL8139_TSD_OWC         (1 << 29)  /* Out of Window Collision */
#define RTL8139_TSD_TABT        (1 << 30)  /* TX Abort */
#define RTL8139_TSD_CRS         (1 << 31)  /* Carrier Sense Lost */

/* ================================================================
 * DRIVER STRUCTURES
 * ================================================================ */

#define RTL8139_RX_BUFFER_SIZE  (32 * 1024 + 16 + 1500)  /* 32K + 16 + wrap */
#define RTL8139_TX_BUFFER_SIZE  1536
#define RTL8139_NUM_TX_DESC     4

typedef struct {
    /* Device identification */
    int detected;
    uint16_t io_base;
    uint8_t irq;
    
    /* MAC address */
    uint8_t mac_addr[6];
    
    /* Receive buffer */
    uint8_t *rx_buffer;
    uint32_t rx_cur;  /* Current position in rx_buffer */
    
    /* Transmit buffers */
    uint8_t *tx_buffers[RTL8139_NUM_TX_DESC];
    uint32_t tx_cur;  /* Current TX descriptor */
    
    /* Statistics */
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t errors;
    uint32_t missed;
    
    /* Link status */
    int link_up;
    int speed;  /* 10 or 100 */
} rtl8139_device_t;

static rtl8139_device_t rtl8139_dev;

/* ================================================================
 * INITIALIZATION
 * ================================================================ */

/**
 * Read MAC address
 */
static void rtl8139_read_mac(void) {
    uint32_t mac_lo = inl(rtl8139_dev.io_base + RTL8139_IDR0);
    uint16_t mac_hi = inw(rtl8139_dev.io_base + RTL8139_IDR4);
    
    rtl8139_dev.mac_addr[0] = mac_lo & 0xFF;
    rtl8139_dev.mac_addr[1] = (mac_lo >> 8) & 0xFF;
    rtl8139_dev.mac_addr[2] = (mac_lo >> 16) & 0xFF;
    rtl8139_dev.mac_addr[3] = (mac_lo >> 24) & 0xFF;
    rtl8139_dev.mac_addr[4] = mac_hi & 0xFF;
    rtl8139_dev.mac_addr[5] = (mac_hi >> 8) & 0xFF;
    
    serial_printf("[RTL8139] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  rtl8139_dev.mac_addr[0], rtl8139_dev.mac_addr[1],
                  rtl8139_dev.mac_addr[2], rtl8139_dev.mac_addr[3],
                  rtl8139_dev.mac_addr[4], rtl8139_dev.mac_addr[5]);
}

/**
 * Update link status
 */
static void rtl8139_update_link(void) {
    uint8_t msr = inb(rtl8139_dev.io_base + RTL8139_MSR);
    
    rtl8139_dev.link_up = !(msr & RTL8139_MSR_LINKB);
    rtl8139_dev.speed = (msr & RTL8139_MSR_SPEED_10) ? 10 : 100;
    
    if (rtl8139_dev.link_up) {
        serial_printf("[RTL8139] Link up: %d Mbps\n", rtl8139_dev.speed);
    } else {
        serial_printf("[RTL8139] Link down\n");
    }
}

/**
 * Detect and initialize RTL8139 NIC
 */
int rtl8139_init(uint16_t io_base, uint8_t irq) {
    serial_printf("[RTL8139] Initializing Realtek Fast Ethernet...\n");
    
    rtl8139_dev.io_base = io_base;
    rtl8139_dev.irq = irq;
    
    /* Power on */
    outb(io_base + RTL8139_CONFIG1, 0x00);
    
    /* Software reset */
    outb(io_base + RTL8139_CR, RTL8139_CR_RST);
    
    /* Wait for reset to complete */
    int timeout = 1000;
    while ((inb(io_base + RTL8139_CR) & RTL8139_CR_RST) && timeout-- > 0) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout <= 0) {
        serial_printf("[RTL8139] Reset timeout\n");
        return -1;
    }
    
    /* Read MAC address */
    rtl8139_read_mac();
    
    /* Allocate receive buffer (must be contiguous and 4-byte aligned) */
    rtl8139_dev.rx_buffer = kmalloc(RTL8139_RX_BUFFER_SIZE + 16);
    if (!rtl8139_dev.rx_buffer) {
        serial_printf("[RTL8139] Failed to allocate RX buffer\n");
        return -1;
    }
    /* Align to 4 bytes */
    rtl8139_dev.rx_buffer = (uint8_t *)(((uint32_t)rtl8139_dev.rx_buffer + 3) & ~3);
    
    /* Set receive buffer address */
    outl(io_base + RTL8139_RBSTART, (uint32_t)rtl8139_dev.rx_buffer);
    
    /* Allocate transmit buffers */
    for (int i = 0; i < RTL8139_NUM_TX_DESC; i++) {
        rtl8139_dev.tx_buffers[i] = kmalloc(RTL8139_TX_BUFFER_SIZE + 16);
        if (!rtl8139_dev.tx_buffers[i]) {
            serial_printf("[RTL8139] Failed to allocate TX buffer %d\n", i);
            return -1;
        }
        /* Align to 4 bytes */
        rtl8139_dev.tx_buffers[i] = (uint8_t *)(((uint32_t)rtl8139_dev.tx_buffers[i] + 3) & ~3);
    }
    
    /* Set TX buffer addresses */
    outl(io_base + RTL8139_TSAD0, (uint32_t)rtl8139_dev.tx_buffers[0]);
    outl(io_base + RTL8139_TSAD1, (uint32_t)rtl8139_dev.tx_buffers[1]);
    outl(io_base + RTL8139_TSAD2, (uint32_t)rtl8139_dev.tx_buffers[2]);
    outl(io_base + RTL8139_TSAD3, (uint32_t)rtl8139_dev.tx_buffers[3]);
    
    /* Enable TX and RX */
    outb(io_base + RTL8139_CR, RTL8139_CR_RE | RTL8139_CR_TE);
    
    /* Configure receive */
    uint32_t rcr = RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB |
                   RTL8139_RCR_WRAP | RTL8139_RCR_MXDMA_UNLIM |
                   RTL8139_RCR_RBLEN_32K | RTL8139_RCR_RXFTH_NONE;
    outl(io_base + RTL8139_RCR, rcr);
    
    /* Configure transmit */
    uint32_t tcr = RTL8139_TCR_MXDMA_2048 | RTL8139_TCR_IFG_NORMAL;
    outl(io_base + RTL8139_TCR, tcr);
    
    /* Accept all multicast */
    outl(io_base + RTL8139_MAR0, 0xFFFFFFFF);
    outl(io_base + RTL8139_MAR4, 0xFFFFFFFF);
    
    /* Enable interrupts */
    outw(io_base + RTL8139_IMR, RTL8139_INT_ROK | RTL8139_INT_TOK |
                                 RTL8139_INT_RER | RTL8139_INT_TER |
                                 RTL8139_INT_RXOVW | RTL8139_INT_PUN);
    
    /* Clear pending interrupts */
    outw(io_base + RTL8139_ISR, 0xFFFF);
    
    rtl8139_dev.rx_cur = 0;
    rtl8139_dev.tx_cur = 0;
    
    /* Check link status */
    rtl8139_update_link();
    
    rtl8139_dev.detected = 1;
    serial_printf("[RTL8139] Initialization complete\n");
    
    return 0;
}

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/**
 * Get MAC address
 */
void rtl8139_get_mac(uint8_t *mac) {
    if (mac) {
        for (int i = 0; i < 6; i++) {
            mac[i] = rtl8139_dev.mac_addr[i];
        }
    }
}

/**
 * Send packet
 */
int rtl8139_send(const void *data, uint16_t length) {
    if (!rtl8139_dev.detected || !data || length == 0) return -1;
    if (length > 1500) return -1;
    
    uint16_t io_base = rtl8139_dev.io_base;
    uint32_t desc = rtl8139_dev.tx_cur;
    
    /* Get TX status register for this descriptor */
    uint16_t tsd_reg = RTL8139_TSD0 + (desc * 4);
    
    /* Wait for previous transmission to complete */
    int timeout = 1000;
    uint32_t status;
    while (timeout-- > 0) {
        status = inl(io_base + tsd_reg);
        if (status & RTL8139_TSD_OWN) break;  /* DMA completed */
        for (volatile int i = 0; i < 100; i++);
    }
    
    /* Copy data to TX buffer */
    uint8_t *tx_buf = rtl8139_dev.tx_buffers[desc];
    const uint8_t *src = (const uint8_t *)data;
    for (uint16_t i = 0; i < length; i++) {
        tx_buf[i] = src[i];
    }
    
    /* Pad short packets */
    while (length < 60) {
        tx_buf[length++] = 0;
    }
    
    /* Start transmission:
     * Clear OWN bit and set length
     * The OWN bit is automatically set to 1 when transmission completes
     */
    outl(io_base + tsd_reg, (length & 0x1FFF));
    
    /* Move to next descriptor */
    rtl8139_dev.tx_cur = (desc + 1) % RTL8139_NUM_TX_DESC;
    
    rtl8139_dev.packets_sent++;
    rtl8139_dev.bytes_sent += length;
    
    return 0;
}

/**
 * Receive packet
 */
int rtl8139_receive(void *buffer, uint16_t max_length) {
    if (!rtl8139_dev.detected || !buffer) return -1;
    
    uint16_t io_base = rtl8139_dev.io_base;
    
    /* Check if buffer is empty */
    uint8_t cr = inb(io_base + RTL8139_CR);
    if (cr & RTL8139_CR_BUFE) {
        return 0;  /* No packet */
    }
    
    /* Get packet header */
    uint32_t cur = rtl8139_dev.rx_cur;
    uint16_t *header = (uint16_t *)(rtl8139_dev.rx_buffer + cur);
    
    uint16_t status = header[0];
    uint16_t length = header[1];
    
    /* Check for errors */
    if (!(status & RTL8139_ROK) || (status & (RTL8139_FAE | RTL8139_CRC | 
                                              RTL8139_LONG | RTL8139_RUNT))) {
        rtl8139_dev.errors++;
        /* Skip bad packet */
        cur = (cur + length + 4 + 3) & ~3;
        cur %= (32 * 1024);
        rtl8139_dev.rx_cur = cur;
        outw(io_base + RTL8139_CAPR, cur - 16);
        return -1;
    }
    
    /* Copy packet data (skip header) */
    uint16_t data_length = length - 4;  /* Exclude CRC */
    if (data_length > max_length) data_length = max_length;
    
    uint8_t *packet_data = rtl8139_dev.rx_buffer + cur + 4;
    uint8_t *dest = (uint8_t *)buffer;
    
    for (uint16_t i = 0; i < data_length; i++) {
        uint32_t offset = (cur + 4 + i) % RTL8139_RX_BUFFER_SIZE;
        dest[i] = rtl8139_dev.rx_buffer[offset];
    }
    
    /* Update current pointer (must be DWORD aligned) */
    cur = (cur + length + 4 + 3) & ~3;
    cur %= (32 * 1024);
    rtl8139_dev.rx_cur = cur;
    
    /* Update CAPR (Current Address of Packet Read) */
    outw(io_base + RTL8139_CAPR, cur - 16);
    
    rtl8139_dev.packets_received++;
    rtl8139_dev.bytes_received += data_length;
    
    return data_length;
}

/**
 * Handle interrupt
 */
void rtl8139_interrupt(void) {
    uint16_t io_base = rtl8139_dev.io_base;
    uint16_t status = inw(io_base + RTL8139_ISR);
    
    /* Clear interrupt flags */
    outw(io_base + RTL8139_ISR, status);
    
    if (status & RTL8139_INT_ROK) {
        /* Packet received - process in main loop */
    }
    
    if (status & RTL8139_INT_TOK) {
        /* TX complete */
    }
    
    if (status & RTL8139_INT_RER) {
        rtl8139_dev.errors++;
    }
    
    if (status & RTL8139_INT_TER) {
        rtl8139_dev.errors++;
    }
    
    if (status & RTL8139_INT_RXOVW) {
        serial_printf("[RTL8139] RX buffer overflow\n");
        rtl8139_dev.errors++;
        /* Reset RX */
        outb(io_base + RTL8139_CR, RTL8139_CR_TE);
        outb(io_base + RTL8139_CR, RTL8139_CR_RE | RTL8139_CR_TE);
    }
    
    if (status & RTL8139_INT_PUN) {
        rtl8139_update_link();
    }
}

/**
 * Check if RTL8139 is detected
 */
int rtl8139_is_detected(void) {
    return rtl8139_dev.detected;
}

/**
 * Get link status
 */
int rtl8139_link_up(void) {
    rtl8139_update_link();
    return rtl8139_dev.link_up;
}

/**
 * Get missed packet count
 */
uint32_t rtl8139_get_missed(void) {
    rtl8139_dev.missed = inl(rtl8139_dev.io_base + RTL8139_MPC);
    return rtl8139_dev.missed;
}

/**
 * Get statistics
 */
void rtl8139_get_stats(uint32_t *tx_packets, uint32_t *rx_packets,
                       uint32_t *tx_bytes, uint32_t *rx_bytes, uint32_t *errors) {
    if (tx_packets) *tx_packets = rtl8139_dev.packets_sent;
    if (rx_packets) *rx_packets = rtl8139_dev.packets_received;
    if (tx_bytes) *tx_bytes = rtl8139_dev.bytes_sent;
    if (rx_bytes) *rx_bytes = rtl8139_dev.bytes_received;
    if (errors) *errors = rtl8139_dev.errors;
}
