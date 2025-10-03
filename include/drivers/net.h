/**
 * TocinOS Network Driver (NE2000 Compatible)
 * 
 * Provides basic network card support for NE2000 compatible cards
 */

#ifndef NET_H
#define NET_H

#include "../stdint.h"

// NE2000 I/O ports (typical ISA base addresses)
#define NE2000_BASE         0x300
#define NE2000_DATA         0x10
#define NE2000_RESET        0x1F

// NE2000 register pages
#define NE2000_COMMAND      0x00
#define NE2000_PAGESTART    0x01
#define NE2000_PAGESTOP     0x02
#define NE2000_BOUNDARY     0x03
#define NE2000_TSR          0x04
#define NE2000_TPSR         0x04
#define NE2000_NCR          0x05
#define NE2000_TBCR0        0x05
#define NE2000_FIFO         0x06
#define NE2000_TBCR1        0x06
#define NE2000_ISR          0x07
#define NE2000_RSAR0        0x08
#define NE2000_CRDA0        0x08
#define NE2000_RSAR1        0x09
#define NE2000_CRDA1        0x09
#define NE2000_RBCR0        0x0A
#define NE2000_RBCR1        0x0B
#define NE2000_RSR          0x0C
#define NE2000_RCR          0x0C
#define NE2000_CNTR0        0x0D
#define NE2000_TCR          0x0D
#define NE2000_CNTR1        0x0E
#define NE2000_DCR          0x0E
#define NE2000_CNTR2        0x0F
#define NE2000_IMR          0x0F

// Commands
#define NE2000_CMD_STOP     0x01
#define NE2000_CMD_START    0x02
#define NE2000_CMD_TRANSMIT 0x04
#define NE2000_CMD_RREAD    0x08
#define NE2000_CMD_RWRITE   0x10
#define NE2000_CMD_NODMA    0x20
#define NE2000_CMD_PAGE0    0x00
#define NE2000_CMD_PAGE1    0x40
#define NE2000_CMD_PAGE2    0x80

// Interrupt status
#define NE2000_ISR_PRX      0x01  // Packet received
#define NE2000_ISR_PTX      0x02  // Packet transmitted
#define NE2000_ISR_RXE      0x04  // Receive error
#define NE2000_ISR_TXE      0x08  // Transmit error
#define NE2000_ISR_OVW      0x10  // Overwrite warning
#define NE2000_ISR_CNT      0x20  // Counter overflow
#define NE2000_ISR_RDC      0x40  // Remote DMA complete
#define NE2000_ISR_RST      0x80  // Reset status

// Network packet structure
typedef struct {
    uint8_t dest_mac[6];        // Destination MAC address
    uint8_t src_mac[6];         // Source MAC address
    uint16_t ethertype;         // Ethernet type
    uint8_t data[1500];         // Packet data
} __attribute__((packed)) net_packet_t;

// Network device info
typedef struct {
    uint16_t base_addr;         // Base I/O address
    uint8_t irq;                // IRQ number
    uint8_t mac_addr[6];        // MAC address
    uint8_t present;            // Is device present?
    uint32_t rx_packets;        // Received packets count
    uint32_t tx_packets;        // Transmitted packets count
    uint32_t rx_errors;         // Receive errors
    uint32_t tx_errors;         // Transmit errors
} net_device_t;

// Network driver API
int net_init(void);
int net_detect(void);
int net_send_packet(const void *data, uint16_t length);
int net_receive_packet(void *buffer, uint16_t max_length);
int net_get_mac_address(uint8_t *mac);
net_device_t *net_get_device(void);

// Internal functions
void net_reset(void);
void net_start(void);
void net_stop(void);
void net_irq_handler(void);

#endif // NET_H
