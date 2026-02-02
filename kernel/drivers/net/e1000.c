/**
 * TocinOS Intel e1000 Gigabit Ethernet Driver
 * 
 * Supports Intel 82540EM, 82545EM, 82546EB and compatible NICs.
 * Common in virtual machines (QEMU, VirtualBox, VMware).
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
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ================================================================
 * E1000 PCI IDENTIFIERS
 * ================================================================ */

#define E1000_VENDOR_ID         0x8086  /* Intel */

/* Device IDs */
#define E1000_DEV_82540EM       0x100E  /* QEMU default */
#define E1000_DEV_82545EM_A     0x100F
#define E1000_DEV_82545EM_C     0x1011
#define E1000_DEV_82546EB_C     0x1010
#define E1000_DEV_82543GC       0x1001
#define E1000_DEV_82544EI       0x1012
#define E1000_DEV_82541GI       0x1076

/* ================================================================
 * E1000 REGISTER DEFINITIONS
 * ================================================================ */

/* Device Control */
#define E1000_CTRL              0x0000
#define E1000_CTRL_FD           (1 << 0)   /* Full Duplex */
#define E1000_CTRL_LRST         (1 << 3)   /* Link Reset */
#define E1000_CTRL_ASDE         (1 << 5)   /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU          (1 << 6)   /* Set Link Up */
#define E1000_CTRL_ILOS         (1 << 7)   /* Invert Loss-of-Signal */
#define E1000_CTRL_RST          (1 << 26)  /* Device Reset */
#define E1000_CTRL_VME          (1 << 30)  /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST      (1 << 31)  /* PHY Reset */

/* Device Status */
#define E1000_STATUS            0x0008
#define E1000_STATUS_FD         (1 << 0)   /* Full Duplex */
#define E1000_STATUS_LU         (1 << 1)   /* Link Up */
#define E1000_STATUS_TXOFF      (1 << 4)   /* Transmission Paused */
#define E1000_STATUS_SPEED_MASK (3 << 6)
#define E1000_STATUS_SPEED_10   (0 << 6)
#define E1000_STATUS_SPEED_100  (1 << 6)
#define E1000_STATUS_SPEED_1000 (2 << 6)

/* EEPROM */
#define E1000_EECD              0x0010
#define E1000_EERD              0x0014
#define E1000_EERD_ADDR_SHIFT   8
#define E1000_EERD_START        (1 << 0)
#define E1000_EERD_DONE         (1 << 4)
#define E1000_EERD_DATA_SHIFT   16

/* Flow Control */
#define E1000_FCAL              0x0028
#define E1000_FCAH              0x002C
#define E1000_FCT               0x0030
#define E1000_FCTTV             0x0170

/* Interrupt */
#define E1000_ICR               0x00C0  /* Interrupt Cause Read */
#define E1000_ICS               0x00C8  /* Interrupt Cause Set */
#define E1000_IMS               0x00D0  /* Interrupt Mask Set */
#define E1000_IMC               0x00D8  /* Interrupt Mask Clear */

/* Interrupt bits */
#define E1000_INT_TXDW          (1 << 0)   /* TX Descriptor Written Back */
#define E1000_INT_TXQE          (1 << 1)   /* TX Queue Empty */
#define E1000_INT_LSC           (1 << 2)   /* Link Status Change */
#define E1000_INT_RXSEQ         (1 << 3)   /* RX Sequence Error */
#define E1000_INT_RXDMT0        (1 << 4)   /* RX Desc Min Threshold */
#define E1000_INT_RXO           (1 << 6)   /* RX Overrun */
#define E1000_INT_RXT0          (1 << 7)   /* RX Timer Interrupt */

/* Receive Control */
#define E1000_RCTL              0x0100
#define E1000_RCTL_EN           (1 << 1)   /* Receiver Enable */
#define E1000_RCTL_SBP          (1 << 2)   /* Store Bad Packets */
#define E1000_RCTL_UPE          (1 << 3)   /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE          (1 << 4)   /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE          (1 << 5)   /* Long Packet Reception Enable */
#define E1000_RCTL_LBM_NONE     (0 << 6)   /* No Loopback */
#define E1000_RCTL_RDMTS_HALF   (0 << 8)   /* RX Desc Min Threshold Size */
#define E1000_RCTL_MO_36        (0 << 12)  /* Multicast Offset 36 bits */
#define E1000_RCTL_BAM          (1 << 15)  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048   (0 << 16)  /* Buffer Size 2048 */
#define E1000_RCTL_BSIZE_1024   (1 << 16)
#define E1000_RCTL_BSIZE_512    (2 << 16)
#define E1000_RCTL_BSIZE_256    (3 << 16)
#define E1000_RCTL_SECRC        (1 << 26)  /* Strip Ethernet CRC */

/* Transmit Control */
#define E1000_TCTL              0x0400
#define E1000_TCTL_EN           (1 << 1)   /* Transmit Enable */
#define E1000_TCTL_PSP          (1 << 3)   /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT     4          /* Collision Threshold */
#define E1000_TCTL_COLD_SHIFT   12         /* Collision Distance */
#define E1000_TCTL_SWXOFF       (1 << 22)  /* Software XOFF Transmission */
#define E1000_TCTL_RTLC         (1 << 24)  /* Re-transmit on Late Collision */

/* TX Inter-Packet Gap */
#define E1000_TIPG              0x0410
#define E1000_TIPG_IPGT_SHIFT   0
#define E1000_TIPG_IPGR1_SHIFT  10
#define E1000_TIPG_IPGR2_SHIFT  20

/* RX Descriptor Base Address */
#define E1000_RDBAL             0x2800
#define E1000_RDBAH             0x2804
#define E1000_RDLEN             0x2808
#define E1000_RDH               0x2810  /* RX Descriptor Head */
#define E1000_RDT               0x2818  /* RX Descriptor Tail */

/* TX Descriptor Base Address */
#define E1000_TDBAL             0x3800
#define E1000_TDBAH             0x3804
#define E1000_TDLEN             0x3808
#define E1000_TDH               0x3810  /* TX Descriptor Head */
#define E1000_TDT               0x3818  /* TX Descriptor Tail */

/* Receive Address */
#define E1000_RAL0              0x5400  /* Receive Address Low */
#define E1000_RAH0              0x5404  /* Receive Address High */
#define E1000_RAH_AV            (1 << 31)  /* Address Valid */

/* Multicast Table Array */
#define E1000_MTA               0x5200

/* ================================================================
 * DESCRIPTOR STRUCTURES
 * ================================================================ */

/* Receive Descriptor */
typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

/* RX Status bits */
#define E1000_RXD_STAT_DD       (1 << 0)  /* Descriptor Done */
#define E1000_RXD_STAT_EOP      (1 << 1)  /* End of Packet */

/* Transmit Descriptor (Legacy) */
typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;        /* Checksum Offset */
    uint8_t cmd;        /* Command */
    uint8_t status;
    uint8_t css;        /* Checksum Start */
    uint16_t special;
} e1000_tx_desc_t;

/* TX Command bits */
#define E1000_TXD_CMD_EOP       (1 << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS      (1 << 1)  /* Insert FCS */
#define E1000_TXD_CMD_RS        (1 << 3)  /* Report Status */

/* TX Status bits */
#define E1000_TXD_STAT_DD       (1 << 0)  /* Descriptor Done */

/* ================================================================
 * DRIVER STRUCTURES
 * ================================================================ */

#define E1000_NUM_RX_DESC       32
#define E1000_NUM_TX_DESC       32
#define E1000_RX_BUFFER_SIZE    2048

typedef struct {
    /* Device identification */
    int detected;
    uint32_t io_base;
    uint32_t mmio_base;
    uint8_t irq;
    uint16_t device_id;
    
    /* MAC address */
    uint8_t mac_addr[6];
    
    /* Receive ring */
    e1000_rx_desc_t *rx_descs;
    uint8_t *rx_buffers[E1000_NUM_RX_DESC];
    uint32_t rx_cur;
    
    /* Transmit ring */
    e1000_tx_desc_t *tx_descs;
    uint8_t *tx_buffers[E1000_NUM_TX_DESC];
    uint32_t tx_cur;
    
    /* Statistics */
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t errors;
    
    /* Link status */
    int link_up;
    int speed;  /* 10, 100, or 1000 */
    int full_duplex;
} e1000_device_t;

static e1000_device_t e1000_dev;

/* ================================================================
 * MMIO ACCESS
 * ================================================================ */

static void e1000_write(uint32_t reg, uint32_t value) {
    if (e1000_dev.mmio_base) {
        *((volatile uint32_t *)(e1000_dev.mmio_base + reg)) = value;
    } else {
        outl(e1000_dev.io_base, reg);
        outl(e1000_dev.io_base + 4, value);
    }
}

static uint32_t e1000_read(uint32_t reg) {
    if (e1000_dev.mmio_base) {
        return *((volatile uint32_t *)(e1000_dev.mmio_base + reg));
    } else {
        outl(e1000_dev.io_base, reg);
        return inl(e1000_dev.io_base + 4);
    }
}

/* ================================================================
 * EEPROM ACCESS
 * ================================================================ */

/**
 * Read word from EEPROM
 */
static uint16_t e1000_eeprom_read(uint8_t addr) {
    uint32_t val;
    
    /* Start read */
    e1000_write(E1000_EERD, (addr << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);
    
    /* Wait for completion */
    int timeout = 1000;
    while (timeout-- > 0) {
        val = e1000_read(E1000_EERD);
        if (val & E1000_EERD_DONE) {
            break;
        }
        /* Small delay */
        for (volatile int i = 0; i < 100; i++);
    }
    
    return (uint16_t)(val >> E1000_EERD_DATA_SHIFT);
}

/**
 * Read MAC address from EEPROM
 */
static void e1000_read_mac(void) {
    uint16_t word;
    
    /* Try reading from EEPROM */
    word = e1000_eeprom_read(0);
    e1000_dev.mac_addr[0] = word & 0xFF;
    e1000_dev.mac_addr[1] = (word >> 8) & 0xFF;
    
    word = e1000_eeprom_read(1);
    e1000_dev.mac_addr[2] = word & 0xFF;
    e1000_dev.mac_addr[3] = (word >> 8) & 0xFF;
    
    word = e1000_eeprom_read(2);
    e1000_dev.mac_addr[4] = word & 0xFF;
    e1000_dev.mac_addr[5] = (word >> 8) & 0xFF;
    
    /* Validate MAC (shouldn't be all 0xFF or all 0x00) */
    int all_ff = 1, all_00 = 1;
    for (int i = 0; i < 6; i++) {
        if (e1000_dev.mac_addr[i] != 0xFF) all_ff = 0;
        if (e1000_dev.mac_addr[i] != 0x00) all_00 = 0;
    }
    
    if (all_ff || all_00) {
        /* Try reading from RAL/RAH registers */
        uint32_t ral = e1000_read(E1000_RAL0);
        uint32_t rah = e1000_read(E1000_RAH0);
        
        e1000_dev.mac_addr[0] = ral & 0xFF;
        e1000_dev.mac_addr[1] = (ral >> 8) & 0xFF;
        e1000_dev.mac_addr[2] = (ral >> 16) & 0xFF;
        e1000_dev.mac_addr[3] = (ral >> 24) & 0xFF;
        e1000_dev.mac_addr[4] = rah & 0xFF;
        e1000_dev.mac_addr[5] = (rah >> 8) & 0xFF;
    }
    
    serial_printf("[E1000] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  e1000_dev.mac_addr[0], e1000_dev.mac_addr[1],
                  e1000_dev.mac_addr[2], e1000_dev.mac_addr[3],
                  e1000_dev.mac_addr[4], e1000_dev.mac_addr[5]);
}

/* ================================================================
 * DESCRIPTOR RING INITIALIZATION
 * ================================================================ */

/**
 * Initialize receive descriptors
 */
static int e1000_init_rx(void) {
    /* Allocate descriptor ring (16-byte aligned) */
    e1000_dev.rx_descs = kmalloc(sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC + 16);
    if (!e1000_dev.rx_descs) return -1;
    
    /* Align to 16 bytes */
    e1000_dev.rx_descs = (e1000_rx_desc_t *)(((uint32_t)e1000_dev.rx_descs + 15) & ~15);
    
    /* Allocate receive buffers */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        e1000_dev.rx_buffers[i] = kmalloc(E1000_RX_BUFFER_SIZE + 16);
        if (!e1000_dev.rx_buffers[i]) return -1;
        
        /* Align buffer */
        e1000_dev.rx_buffers[i] = (uint8_t *)(((uint32_t)e1000_dev.rx_buffers[i] + 15) & ~15);
        
        e1000_dev.rx_descs[i].buffer_addr = (uint32_t)e1000_dev.rx_buffers[i];
        e1000_dev.rx_descs[i].status = 0;
    }
    
    /* Set base address */
    e1000_write(E1000_RDBAL, (uint32_t)e1000_dev.rx_descs);
    e1000_write(E1000_RDBAH, 0);
    
    /* Set ring length */
    e1000_write(E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    
    /* Set head and tail */
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_NUM_RX_DESC - 1);
    
    e1000_dev.rx_cur = 0;
    
    /* Enable receiver */
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
                    E1000_RCTL_MPE | E1000_RCTL_LBM_NONE |
                    E1000_RCTL_RDMTS_HALF | E1000_RCTL_BAM |
                    E1000_RCTL_SECRC | E1000_RCTL_BSIZE_2048;
    e1000_write(E1000_RCTL, rctl);
    
    return 0;
}

/**
 * Initialize transmit descriptors
 */
static int e1000_init_tx(void) {
    /* Allocate descriptor ring (16-byte aligned) */
    e1000_dev.tx_descs = kmalloc(sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC + 16);
    if (!e1000_dev.tx_descs) return -1;
    
    /* Align to 16 bytes */
    e1000_dev.tx_descs = (e1000_tx_desc_t *)(((uint32_t)e1000_dev.tx_descs + 15) & ~15);
    
    /* Allocate transmit buffers */
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        e1000_dev.tx_buffers[i] = kmalloc(E1000_RX_BUFFER_SIZE + 16);
        if (!e1000_dev.tx_buffers[i]) return -1;
        
        e1000_dev.tx_buffers[i] = (uint8_t *)(((uint32_t)e1000_dev.tx_buffers[i] + 15) & ~15);
        
        e1000_dev.tx_descs[i].buffer_addr = (uint32_t)e1000_dev.tx_buffers[i];
        e1000_dev.tx_descs[i].cmd = 0;
        e1000_dev.tx_descs[i].status = E1000_TXD_STAT_DD;  /* Mark as available */
    }
    
    /* Set base address */
    e1000_write(E1000_TDBAL, (uint32_t)e1000_dev.tx_descs);
    e1000_write(E1000_TDBAH, 0);
    
    /* Set ring length */
    e1000_write(E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    
    /* Set head and tail */
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    
    e1000_dev.tx_cur = 0;
    
    /* Set Inter-Packet Gap */
    e1000_write(E1000_TIPG, (10 << E1000_TIPG_IPGT_SHIFT) |
                            (10 << E1000_TIPG_IPGR1_SHIFT) |
                            (10 << E1000_TIPG_IPGR2_SHIFT));
    
    /* Enable transmitter */
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
                    (15 << E1000_TCTL_CT_SHIFT) |
                    (64 << E1000_TCTL_COLD_SHIFT) |
                    E1000_TCTL_RTLC;
    e1000_write(E1000_TCTL, tctl);
    
    return 0;
}

/* ================================================================
 * LINK STATUS
 * ================================================================ */

/**
 * Update link status
 */
static void e1000_update_link(void) {
    uint32_t status = e1000_read(E1000_STATUS);
    
    e1000_dev.link_up = (status & E1000_STATUS_LU) ? 1 : 0;
    e1000_dev.full_duplex = (status & E1000_STATUS_FD) ? 1 : 0;
    
    uint32_t speed = status & E1000_STATUS_SPEED_MASK;
    if (speed == E1000_STATUS_SPEED_1000) {
        e1000_dev.speed = 1000;
    } else if (speed == E1000_STATUS_SPEED_100) {
        e1000_dev.speed = 100;
    } else {
        e1000_dev.speed = 10;
    }
    
    if (e1000_dev.link_up) {
        serial_printf("[E1000] Link up: %d Mbps %s\n",
                      e1000_dev.speed,
                      e1000_dev.full_duplex ? "Full Duplex" : "Half Duplex");
    } else {
        serial_printf("[E1000] Link down\n");
    }
}

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/**
 * Detect and initialize e1000 NIC
 */
int e1000_init(uint32_t mmio_base, uint8_t irq) {
    serial_printf("[E1000] Initializing Intel Gigabit Ethernet...\n");
    
    e1000_dev.mmio_base = mmio_base;
    e1000_dev.irq = irq;
    
    /* Reset device */
    uint32_t ctrl = e1000_read(E1000_CTRL);
    e1000_write(E1000_CTRL, ctrl | E1000_CTRL_RST);
    
    /* Wait for reset to complete */
    for (volatile int i = 0; i < 100000; i++);
    
    /* Disable interrupts */
    e1000_write(E1000_IMC, 0xFFFFFFFF);
    
    /* Read MAC address */
    e1000_read_mac();
    
    /* Set receive address */
    uint32_t ral = e1000_dev.mac_addr[0] | (e1000_dev.mac_addr[1] << 8) |
                   (e1000_dev.mac_addr[2] << 16) | (e1000_dev.mac_addr[3] << 24);
    uint32_t rah = e1000_dev.mac_addr[4] | (e1000_dev.mac_addr[5] << 8) | E1000_RAH_AV;
    e1000_write(E1000_RAL0, ral);
    e1000_write(E1000_RAH0, rah);
    
    /* Clear multicast table */
    for (int i = 0; i < 128; i++) {
        e1000_write(E1000_MTA + i * 4, 0);
    }
    
    /* Initialize RX */
    if (e1000_init_rx() != 0) {
        serial_printf("[E1000] Failed to initialize RX\n");
        return -1;
    }
    
    /* Initialize TX */
    if (e1000_init_tx() != 0) {
        serial_printf("[E1000] Failed to initialize TX\n");
        return -1;
    }
    
    /* Enable interrupts */
    e1000_write(E1000_IMS, E1000_INT_RXT0 | E1000_INT_TXDW | E1000_INT_LSC);
    
    /* Set link up */
    ctrl = e1000_read(E1000_CTRL);
    e1000_write(E1000_CTRL, ctrl | E1000_CTRL_SLU | E1000_CTRL_ASDE);
    
    /* Check link status */
    e1000_update_link();
    
    e1000_dev.detected = 1;
    serial_printf("[E1000] Initialization complete\n");
    
    return 0;
}

/**
 * Get MAC address
 */
void e1000_get_mac(uint8_t *mac) {
    if (mac) {
        for (int i = 0; i < 6; i++) {
            mac[i] = e1000_dev.mac_addr[i];
        }
    }
}

/**
 * Send packet
 */
int e1000_send(const void *data, uint16_t length) {
    if (!e1000_dev.detected || !data || length == 0) return -1;
    if (length > 1522) return -1;  /* Max Ethernet frame + VLAN */
    
    uint32_t cur = e1000_dev.tx_cur;
    e1000_tx_desc_t *desc = &e1000_dev.tx_descs[cur];
    
    /* Wait for descriptor to be available */
    int timeout = 1000;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout <= 0) {
        e1000_dev.errors++;
        return -1;
    }
    
    /* Copy data to buffer */
    uint8_t *d = e1000_dev.tx_buffers[cur];
    const uint8_t *s = (const uint8_t *)data;
    for (uint16_t i = 0; i < length; i++) {
        d[i] = s[i];
    }
    
    /* Setup descriptor */
    desc->length = length;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    /* Update tail */
    e1000_dev.tx_cur = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(E1000_TDT, e1000_dev.tx_cur);
    
    e1000_dev.packets_sent++;
    e1000_dev.bytes_sent += length;
    
    return 0;
}

/**
 * Receive packet
 */
int e1000_receive(void *buffer, uint16_t max_length) {
    if (!e1000_dev.detected || !buffer) return -1;
    
    uint32_t cur = e1000_dev.rx_cur;
    e1000_rx_desc_t *desc = &e1000_dev.rx_descs[cur];
    
    /* Check if packet available */
    if (!(desc->status & E1000_RXD_STAT_DD)) {
        return 0;  /* No packet */
    }
    
    /* Check for errors */
    if (desc->errors) {
        e1000_dev.errors++;
        desc->status = 0;
        e1000_write(E1000_RDT, cur);
        e1000_dev.rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
        return -1;
    }
    
    /* Copy data to buffer */
    uint16_t length = desc->length;
    if (length > max_length) length = max_length;
    
    uint8_t *d = (uint8_t *)buffer;
    uint8_t *s = e1000_dev.rx_buffers[cur];
    for (uint16_t i = 0; i < length; i++) {
        d[i] = s[i];
    }
    
    /* Reset descriptor */
    desc->status = 0;
    
    /* Update tail */
    uint32_t old_cur = cur;
    e1000_dev.rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
    e1000_write(E1000_RDT, old_cur);
    
    e1000_dev.packets_received++;
    e1000_dev.bytes_received += length;
    
    return length;
}

/**
 * Handle interrupt
 */
void e1000_interrupt(void) {
    uint32_t icr = e1000_read(E1000_ICR);
    
    if (icr & E1000_INT_LSC) {
        e1000_update_link();
    }
    
    if (icr & E1000_INT_RXT0) {
        /* Packet received - process in main loop */
    }
    
    if (icr & E1000_INT_TXDW) {
        /* TX complete */
    }
}

/**
 * Check if e1000 is detected
 */
int e1000_is_detected(void) {
    return e1000_dev.detected;
}

/**
 * Get link status
 */
int e1000_link_up(void) {
    e1000_update_link();
    return e1000_dev.link_up;
}

/**
 * Get statistics
 */
void e1000_get_stats(uint32_t *tx_packets, uint32_t *rx_packets,
                     uint32_t *tx_bytes, uint32_t *rx_bytes, uint32_t *errors) {
    if (tx_packets) *tx_packets = e1000_dev.packets_sent;
    if (rx_packets) *rx_packets = e1000_dev.packets_received;
    if (tx_bytes) *tx_bytes = e1000_dev.bytes_sent;
    if (rx_bytes) *rx_bytes = e1000_dev.bytes_received;
    if (errors) *errors = e1000_dev.errors;
}
