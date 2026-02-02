/**
 * TocinOS UHCI (USB 1.1) Host Controller Driver
 * 
 * Universal Host Controller Interface driver for USB 1.x devices.
 * Supports low-speed (1.5 Mbps) and full-speed (12 Mbps) devices.
 * 
 * @author TocinOS Team
 */

#include "../../../include/drivers/usb_core.h"
#include "../../../include/kernel/memory.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* External functions */
extern void serial_printf(const char *fmt, ...);
extern uint32_t timer_get_ticks(void);

/* Port I/O functions */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* ================================================================
 * UHCI REGISTERS (I/O space offsets)
 * ================================================================ */

#define UHCI_CMD            0x00    /* USB Command Register */
#define UHCI_STS            0x02    /* USB Status Register */
#define UHCI_INTR           0x04    /* USB Interrupt Enable */
#define UHCI_FRNUM          0x06    /* Frame Number */
#define UHCI_FRBASEADD      0x08    /* Frame List Base Address */
#define UHCI_SOFMOD         0x0C    /* Start of Frame Modify */
#define UHCI_PORTSC1        0x10    /* Port 1 Status/Control */
#define UHCI_PORTSC2        0x12    /* Port 2 Status/Control */

/* Command Register Bits */
#define UHCI_CMD_RS         0x0001  /* Run/Stop */
#define UHCI_CMD_HCRESET    0x0002  /* Host Controller Reset */
#define UHCI_CMD_GRESET     0x0004  /* Global Reset */
#define UHCI_CMD_EGSM       0x0008  /* Enter Global Suspend Mode */
#define UHCI_CMD_FGR        0x0010  /* Force Global Resume */
#define UHCI_CMD_SWDBG      0x0020  /* Software Debug */
#define UHCI_CMD_CF         0x0040  /* Configure Flag */
#define UHCI_CMD_MAXP       0x0080  /* Max Packet (1=64 bytes) */

/* Status Register Bits */
#define UHCI_STS_USBINT     0x0001  /* USB Interrupt */
#define UHCI_STS_ERROR      0x0002  /* USB Error Interrupt */
#define UHCI_STS_RD         0x0004  /* Resume Detect */
#define UHCI_STS_HSE        0x0008  /* Host System Error */
#define UHCI_STS_HCPE       0x0010  /* Host Controller Process Error */
#define UHCI_STS_HCH        0x0020  /* HC Halted */

/* Interrupt Enable Register Bits */
#define UHCI_INTR_TIMEOUT   0x0001  /* Timeout/CRC Interrupt Enable */
#define UHCI_INTR_RESUME    0x0002  /* Resume Interrupt Enable */
#define UHCI_INTR_IOC       0x0004  /* Interrupt On Complete Enable */
#define UHCI_INTR_SP        0x0008  /* Short Packet Interrupt Enable */

/* Port Status/Control Bits */
#define UHCI_PORTSC_CCS     0x0001  /* Current Connect Status */
#define UHCI_PORTSC_CSC     0x0002  /* Connect Status Change */
#define UHCI_PORTSC_PE      0x0004  /* Port Enable */
#define UHCI_PORTSC_PEC     0x0008  /* Port Enable Change */
#define UHCI_PORTSC_DPLUS   0x0010  /* D+ Line Status */
#define UHCI_PORTSC_DMINUS  0x0020  /* D- Line Status */
#define UHCI_PORTSC_RD      0x0040  /* Resume Detect */
#define UHCI_PORTSC_LSDA    0x0100  /* Low Speed Device Attached */
#define UHCI_PORTSC_PR      0x0200  /* Port Reset */
#define UHCI_PORTSC_SUSP    0x1000  /* Suspend */

/* ================================================================
 * UHCI DATA STRUCTURES (Must be aligned and in low memory)
 * ================================================================ */

/* Frame List Pointer */
#define UHCI_FLP_T          0x0001  /* Terminate */
#define UHCI_FLP_Q          0x0002  /* Queue Head select */
#define UHCI_FLP_DEPTH      0x0004  /* Depth/Breadth select */

/* Transfer Descriptor */
typedef struct uhci_td {
    uint32_t link;              /* Link to next TD/QH */
    uint32_t ctrl_status;       /* Control and Status */
    uint32_t token;             /* Token */
    uint32_t buffer;            /* Buffer Pointer */
    
    /* Software fields (not used by hardware) */
    uint32_t phys_addr;         /* Physical address of this TD */
    uint32_t reserved[3];       /* Pad to 32 bytes */
} __attribute__((packed, aligned(16))) uhci_td_t;

/* TD Link Pointer Bits */
#define UHCI_TD_T           0x0001  /* Terminate */
#define UHCI_TD_Q           0x0002  /* Queue Head */
#define UHCI_TD_VF          0x0004  /* Depth First */

/* TD Control/Status Bits */
#define UHCI_TD_ACTLEN_MASK     0x7FF       /* Actual Length */
#define UHCI_TD_STATUS_MASK     0xFF0000    /* Status */
#define UHCI_TD_ACTIVE          (1 << 23)   /* Active */
#define UHCI_TD_STALLED         (1 << 22)   /* Stalled */
#define UHCI_TD_DATABUFFER      (1 << 21)   /* Data Buffer Error */
#define UHCI_TD_BABBLE          (1 << 20)   /* Babble Detected */
#define UHCI_TD_NAK             (1 << 19)   /* NAK Received */
#define UHCI_TD_CRC             (1 << 18)   /* CRC/Timeout Error */
#define UHCI_TD_BITSTUFF        (1 << 17)   /* Bitstuff Error */
#define UHCI_TD_IOC             (1 << 24)   /* Interrupt On Complete */
#define UHCI_TD_IOS             (1 << 25)   /* Isochronous Select */
#define UHCI_TD_LS              (1 << 26)   /* Low Speed Device */
#define UHCI_TD_CERR_MASK       (3 << 27)   /* Error Counter */
#define UHCI_TD_SPD             (1 << 29)   /* Short Packet Detect */

/* TD Token Bits */
#define UHCI_TD_PID_SETUP       0x2D
#define UHCI_TD_PID_IN          0x69
#define UHCI_TD_PID_OUT         0xE1

#define UHCI_TD_TOKEN(maxlen, toggle, endpt, addr, pid) \
    (((maxlen - 1) << 21) | (toggle << 19) | (endpt << 15) | (addr << 8) | pid)

/* Queue Head */
typedef struct uhci_qh {
    uint32_t head;              /* Queue Head Link Pointer */
    uint32_t element;           /* Queue Element Link Pointer */
    
    /* Software fields */
    uint32_t phys_addr;         /* Physical address of this QH */
    uint32_t reserved[5];       /* Pad to 32 bytes */
} __attribute__((packed, aligned(16))) uhci_qh_t;

/* QH Link Pointer Bits */
#define UHCI_QH_T           0x0001  /* Terminate */
#define UHCI_QH_Q           0x0002  /* Queue Head */

/* ================================================================
 * UHCI DRIVER DATA
 * ================================================================ */

#define UHCI_MAX_TDS        32
#define UHCI_FRAME_COUNT    1024

typedef struct uhci_data {
    uint16_t io_base;           /* I/O base address */
    
    uint32_t *frame_list;       /* Frame list (1024 entries) */
    uint32_t frame_list_phys;   /* Physical address of frame list */
    
    uhci_qh_t *async_qh;        /* Async queue head */
    uhci_td_t *td_pool;         /* TD pool */
    uint32_t td_pool_phys;
    int td_alloc_idx;           /* Next TD to allocate */
    
    uint8_t port_count;         /* Number of ports */
} uhci_data_t;

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void uhci_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

static void uhci_delay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++);
}

/* ================================================================
 * TD/QH ALLOCATION
 * ================================================================ */

static uhci_td_t *uhci_alloc_td(uhci_data_t *uhci) {
    if (uhci->td_alloc_idx >= UHCI_MAX_TDS) {
        uhci->td_alloc_idx = 0;
    }
    
    uhci_td_t *td = &uhci->td_pool[uhci->td_alloc_idx++];
    uhci_memset(td, 0, sizeof(uhci_td_t));
    td->phys_addr = uhci->td_pool_phys + ((uint32_t)td - (uint32_t)uhci->td_pool);
    
    return td;
}

/* ================================================================
 * REGISTER ACCESS
 * ================================================================ */

static inline uint16_t uhci_read16(uhci_data_t *uhci, uint16_t reg) {
    return inw(uhci->io_base + reg);
}

static inline void uhci_write16(uhci_data_t *uhci, uint16_t reg, uint16_t val) {
    outw(uhci->io_base + reg, val);
}

static inline uint32_t uhci_read32(uhci_data_t *uhci, uint16_t reg) {
    return inl(uhci->io_base + reg);
}

static inline void uhci_write32(uhci_data_t *uhci, uint16_t reg, uint32_t val) {
    outl(uhci->io_base + reg, val);
}

/* ================================================================
 * HOST CONTROLLER OPERATIONS
 * ================================================================ */

static int uhci_start(usb_controller_t *hc) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    serial_printf("[UHCI] Starting controller at I/O %04X\n", uhci->io_base);
    
    /* Enable interrupts */
    uhci_write16(uhci, UHCI_INTR, UHCI_INTR_IOC | UHCI_INTR_SP);
    
    /* Set frame list base */
    uhci_write32(uhci, UHCI_FRBASEADD, uhci->frame_list_phys);
    
    /* Start at frame 0 */
    uhci_write16(uhci, UHCI_FRNUM, 0);
    
    /* Start the controller */
    uhci_write16(uhci, UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_MAXP);
    
    uhci_delay(10);
    
    uint16_t status = uhci_read16(uhci, UHCI_STS);
    serial_printf("[UHCI] Status after start: %04X\n", status);
    
    if (status & UHCI_STS_HCH) {
        serial_printf("[UHCI] Controller still halted!\n");
        return -1;
    }
    
    /* Enable ports */
    for (int i = 0; i < uhci->port_count; i++) {
        uint16_t port_reg = UHCI_PORTSC1 + (i * 2);
        uint16_t portsc = uhci_read16(uhci, port_reg);
        
        /* Clear change bits and enable port */
        uhci_write16(uhci, port_reg, portsc | UHCI_PORTSC_CSC | UHCI_PORTSC_PEC);
    }
    
    serial_printf("[UHCI] Controller started\n");
    return 0;
}

static int uhci_stop(usb_controller_t *hc) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    /* Stop the controller */
    uhci_write16(uhci, UHCI_CMD, 0);
    
    /* Wait for halt */
    for (int i = 0; i < 100; i++) {
        if (uhci_read16(uhci, UHCI_STS) & UHCI_STS_HCH) {
            break;
        }
        uhci_delay(1);
    }
    
    serial_printf("[UHCI] Controller stopped\n");
    return 0;
}

static int uhci_reset(usb_controller_t *hc) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    serial_printf("[UHCI] Resetting controller\n");
    
    /* Global reset */
    uhci_write16(uhci, UHCI_CMD, UHCI_CMD_GRESET);
    uhci_delay(50);
    uhci_write16(uhci, UHCI_CMD, 0);
    uhci_delay(10);
    
    /* Host controller reset */
    uhci_write16(uhci, UHCI_CMD, UHCI_CMD_HCRESET);
    
    /* Wait for reset to complete */
    for (int i = 0; i < 100; i++) {
        if (!(uhci_read16(uhci, UHCI_CMD) & UHCI_CMD_HCRESET)) {
            break;
        }
        uhci_delay(1);
    }
    
    if (uhci_read16(uhci, UHCI_CMD) & UHCI_CMD_HCRESET) {
        serial_printf("[UHCI] Reset timeout\n");
        return -1;
    }
    
    serial_printf("[UHCI] Reset complete\n");
    return 0;
}

static int uhci_get_port_status(usb_controller_t *hc, int port, uint16_t *status) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    if (port >= uhci->port_count) {
        return -1;
    }
    
    uint16_t port_reg = UHCI_PORTSC1 + (port * 2);
    uint16_t portsc = uhci_read16(uhci, port_reg);
    
    *status = 0;
    
    if (portsc & UHCI_PORTSC_CCS) *status |= USB_PORT_CONNECTED;
    if (portsc & UHCI_PORTSC_PE) *status |= USB_PORT_ENABLED;
    if (portsc & UHCI_PORTSC_SUSP) *status |= USB_PORT_SUSPENDED;
    if (portsc & UHCI_PORTSC_PR) *status |= USB_PORT_RESET;
    if (portsc & UHCI_PORTSC_LSDA) *status |= USB_PORT_LOW_SPEED;
    
    return 0;
}

static int uhci_set_port_feature(usb_controller_t *hc, int port, int feature) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    if (port >= uhci->port_count) {
        return -1;
    }
    
    uint16_t port_reg = UHCI_PORTSC1 + (port * 2);
    uint16_t portsc = uhci_read16(uhci, port_reg);
    
    switch (feature) {
        case USB_HUB_FEAT_PORT_RESET:
            /* Reset port */
            uhci_write16(uhci, port_reg, portsc | UHCI_PORTSC_PR);
            uhci_delay(50);  /* USB spec requires 10ms minimum */
            uhci_write16(uhci, port_reg, portsc & ~UHCI_PORTSC_PR);
            uhci_delay(10);
            
            /* Enable port */
            portsc = uhci_read16(uhci, port_reg);
            uhci_write16(uhci, port_reg, portsc | UHCI_PORTSC_PE);
            break;
            
        case USB_HUB_FEAT_PORT_POWER:
            /* UHCI doesn't have per-port power control */
            break;
    }
    
    return 0;
}

static int uhci_clear_port_feature(usb_controller_t *hc, int port, int feature) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    if (port >= uhci->port_count) {
        return -1;
    }
    
    uint16_t port_reg = UHCI_PORTSC1 + (port * 2);
    uint16_t portsc = uhci_read16(uhci, port_reg);
    
    switch (feature) {
        case USB_HUB_FEAT_C_PORT_CONNECT:
            uhci_write16(uhci, port_reg, portsc | UHCI_PORTSC_CSC);
            break;
            
        case USB_HUB_FEAT_C_PORT_RESET:
            /* Clear by writing 1 to change bits */
            break;
    }
    
    return 0;
}

/* ================================================================
 * TRANSFER EXECUTION
 * ================================================================ */

static int uhci_wait_td(uhci_td_t *td, int timeout_ms) {
    uint32_t start = timer_get_ticks();
    uint32_t timeout = timeout_ms * 100 / 1000;  /* Convert to ticks */
    
    while (td->ctrl_status & UHCI_TD_ACTIVE) {
        if (timer_get_ticks() - start > timeout) {
            return USB_STATUS_TIMEOUT;
        }
        
        /* Small delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    /* Check for errors */
    uint32_t status = td->ctrl_status;
    
    if (status & UHCI_TD_STALLED) return USB_STATUS_STALL;
    if (status & UHCI_TD_BABBLE) return USB_STATUS_OVERFLOW;
    if (status & UHCI_TD_CRC) return USB_STATUS_CRC_ERROR;
    if (status & UHCI_TD_BITSTUFF) return USB_STATUS_BIT_STUFF;
    if (status & UHCI_TD_DATABUFFER) return USB_STATUS_OVERFLOW;
    
    return USB_STATUS_SUCCESS;
}

static int uhci_control_transfer(usb_controller_t *hc, usb_transfer_t *transfer) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    usb_device_t *device = transfer->device;
    usb_device_request_t *setup = transfer->setup;
    
    uint8_t addr = device->address;
    uint8_t endp = transfer->endpoint->address;
    int low_speed = (device->speed == USB_SPEED_LOW) ? UHCI_TD_LS : 0;
    
    /* Allocate TDs */
    uhci_td_t *setup_td = uhci_alloc_td(uhci);
    uhci_td_t *data_td = NULL;
    uhci_td_t *status_td = uhci_alloc_td(uhci);
    
    if (!setup_td || !status_td) {
        return USB_STATUS_CANCELLED;
    }
    
    /* Setup TD */
    setup_td->ctrl_status = UHCI_TD_ACTIVE | low_speed | (3 << 27);
    setup_td->token = UHCI_TD_TOKEN(8, 0, endp, addr, UHCI_TD_PID_SETUP);
    setup_td->buffer = usb_virt_to_phys(setup);
    
    uint8_t toggle = 1;
    uhci_td_t *prev = setup_td;
    
    /* Data TD(s) if needed */
    if (transfer->length > 0 && transfer->buffer) {
        data_td = uhci_alloc_td(uhci);
        if (!data_td) {
            return USB_STATUS_CANCELLED;
        }
        
        uint8_t pid = (transfer->direction & USB_DIR_IN) ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT;
        uint32_t max_packet = transfer->endpoint->max_packet_size;
        if (max_packet == 0) max_packet = 8;
        
        uint32_t len = transfer->length;
        if (len > max_packet) len = max_packet;
        
        data_td->ctrl_status = UHCI_TD_ACTIVE | low_speed | (3 << 27);
        data_td->token = UHCI_TD_TOKEN(len, toggle, endp, addr, pid);
        data_td->buffer = usb_virt_to_phys(transfer->buffer);
        
        prev->link = data_td->phys_addr | UHCI_TD_VF;
        prev = data_td;
        toggle ^= 1;
    }
    
    /* Status TD */
    uint8_t status_pid = (transfer->direction & USB_DIR_IN) ? UHCI_TD_PID_OUT : UHCI_TD_PID_IN;
    status_td->ctrl_status = UHCI_TD_ACTIVE | UHCI_TD_IOC | low_speed | (3 << 27);
    status_td->token = UHCI_TD_TOKEN(0, 1, endp, addr, status_pid);
    status_td->buffer = 0;
    
    prev->link = status_td->phys_addr | UHCI_TD_VF;
    status_td->link = UHCI_TD_T;
    
    /* Add to schedule */
    uhci->async_qh->element = setup_td->phys_addr;
    
    /* Wait for completion */
    int ret = uhci_wait_td(status_td, 1000);
    
    /* Remove from schedule */
    uhci->async_qh->element = UHCI_TD_T;
    
    if (ret == USB_STATUS_SUCCESS) {
        if (data_td) {
            transfer->actual_length = (data_td->ctrl_status & UHCI_TD_ACTLEN_MASK) + 1;
            if (transfer->actual_length > transfer->length) {
                transfer->actual_length = transfer->length;
            }
        } else {
            transfer->actual_length = 0;
        }
    }
    
    return ret;
}

static int uhci_bulk_transfer(usb_controller_t *hc, usb_transfer_t *transfer) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    usb_device_t *device = transfer->device;
    usb_endpoint_t *ep = transfer->endpoint;
    
    uint8_t addr = device->address;
    uint8_t endp = ep->address;
    int low_speed = (device->speed == USB_SPEED_LOW) ? UHCI_TD_LS : 0;
    uint8_t pid = (ep->direction & USB_DIR_IN) ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT;
    
    uint32_t max_packet = ep->max_packet_size;
    if (max_packet == 0) max_packet = 64;
    
    uint32_t remaining = transfer->length;
    uint8_t *buf = (uint8_t *)transfer->buffer;
    uint32_t total = 0;
    uint8_t toggle = ep->toggle;
    
    while (remaining > 0) {
        uhci_td_t *td = uhci_alloc_td(uhci);
        if (!td) {
            return USB_STATUS_CANCELLED;
        }
        
        uint32_t len = remaining;
        if (len > max_packet) len = max_packet;
        
        td->ctrl_status = UHCI_TD_ACTIVE | UHCI_TD_IOC | low_speed | (3 << 27);
        td->token = UHCI_TD_TOKEN(len, toggle, endp, addr, pid);
        td->buffer = usb_virt_to_phys(buf);
        td->link = UHCI_TD_T;
        
        /* Add to schedule */
        uhci->async_qh->element = td->phys_addr;
        
        /* Wait for completion */
        int ret = uhci_wait_td(td, 1000);
        
        /* Remove from schedule */
        uhci->async_qh->element = UHCI_TD_T;
        
        if (ret != USB_STATUS_SUCCESS) {
            ep->toggle = toggle;
            transfer->actual_length = total;
            return ret;
        }
        
        uint32_t actual = (td->ctrl_status & UHCI_TD_ACTLEN_MASK) + 1;
        if (actual > len) actual = len;
        
        total += actual;
        buf += len;
        remaining -= len;
        toggle ^= 1;
        
        /* Short packet means end of transfer */
        if (actual < len) {
            break;
        }
    }
    
    ep->toggle = toggle;
    transfer->actual_length = total;
    
    return USB_STATUS_SUCCESS;
}

static int uhci_interrupt_transfer(usb_controller_t *hc, usb_transfer_t *transfer) {
    /* For simplicity, handle interrupt transfers like bulk */
    return uhci_bulk_transfer(hc, transfer);
}

static void uhci_irq_handler(usb_controller_t *hc) {
    uhci_data_t *uhci = (uhci_data_t *)hc->hc_data;
    
    uint16_t status = uhci_read16(uhci, UHCI_STS);
    
    /* Clear interrupt bits */
    uhci_write16(uhci, UHCI_STS, status);
    
    if (status & UHCI_STS_USBINT) {
        /* Transfer complete interrupt */
    }
    
    if (status & UHCI_STS_ERROR) {
        serial_printf("[UHCI] USB Error interrupt\n");
    }
    
    if (status & UHCI_STS_HSE) {
        serial_printf("[UHCI] Host System Error!\n");
    }
}

/* ================================================================
 * DRIVER OPERATIONS
 * ================================================================ */

static usb_hc_ops_t uhci_ops = {
    .start = uhci_start,
    .stop = uhci_stop,
    .reset = uhci_reset,
    .get_port_status = uhci_get_port_status,
    .set_port_feature = uhci_set_port_feature,
    .clear_port_feature = uhci_clear_port_feature,
    .control_transfer = uhci_control_transfer,
    .bulk_transfer = uhci_bulk_transfer,
    .interrupt_transfer = uhci_interrupt_transfer,
    .isochronous_transfer = NULL,
    .irq_handler = uhci_irq_handler
};

/* ================================================================
 * INITIALIZATION
 * ================================================================ */

/**
 * Initialize UHCI controller from PCI device
 */
int uhci_init(uint32_t io_base, uint8_t irq) {
    serial_printf("[UHCI] Initializing UHCI controller at I/O %04X, IRQ %d\n", 
                  io_base, irq);
    
    /* Allocate controller structure */
    usb_controller_t *hc = kmalloc(sizeof(usb_controller_t));
    if (!hc) {
        serial_printf("[UHCI] Failed to allocate controller\n");
        return -1;
    }
    uhci_memset(hc, 0, sizeof(usb_controller_t));
    
    /* Allocate UHCI-specific data */
    uhci_data_t *uhci = kmalloc(sizeof(uhci_data_t));
    if (!uhci) {
        kfree(hc);
        return -1;
    }
    uhci_memset(uhci, 0, sizeof(uhci_data_t));
    
    uhci->io_base = io_base;
    uhci->port_count = 2;  /* Most UHCI controllers have 2 ports */
    
    /* Allocate frame list (4KB aligned) */
    uhci->frame_list = usb_alloc_buffer(UHCI_FRAME_COUNT * sizeof(uint32_t));
    if (!uhci->frame_list) {
        kfree(uhci);
        kfree(hc);
        return -1;
    }
    uhci->frame_list_phys = usb_virt_to_phys(uhci->frame_list);
    
    /* Allocate async QH */
    uhci->async_qh = usb_alloc_buffer(sizeof(uhci_qh_t));
    if (!uhci->async_qh) {
        usb_free_buffer(uhci->frame_list);
        kfree(uhci);
        kfree(hc);
        return -1;
    }
    uhci->async_qh->phys_addr = usb_virt_to_phys(uhci->async_qh);
    uhci->async_qh->head = UHCI_QH_T;
    uhci->async_qh->element = UHCI_TD_T;
    
    /* Allocate TD pool */
    uhci->td_pool = usb_alloc_buffer(UHCI_MAX_TDS * sizeof(uhci_td_t));
    if (!uhci->td_pool) {
        usb_free_buffer(uhci->async_qh);
        usb_free_buffer(uhci->frame_list);
        kfree(uhci);
        kfree(hc);
        return -1;
    }
    uhci->td_pool_phys = usb_virt_to_phys(uhci->td_pool);
    uhci->td_alloc_idx = 0;
    
    /* Initialize frame list to point to async QH */
    uint32_t qh_ptr = uhci->async_qh->phys_addr | UHCI_FLP_Q;
    for (int i = 0; i < UHCI_FRAME_COUNT; i++) {
        uhci->frame_list[i] = qh_ptr;
    }
    
    /* Set up controller */
    hc->hc_data = uhci;
    hc->type = USB_HC_UHCI;
    hc->io_base = io_base;
    hc->irq = irq;
    hc->num_ports = uhci->port_count;
    hc->ops = &uhci_ops;
    
    char *name = "UHCI USB 1.1";
    for (int i = 0; name[i] && i < 31; i++) {
        hc->name[i] = name[i];
        hc->name[i+1] = '\0';
    }
    
    /* Reset the controller */
    if (uhci_reset(hc) < 0) {
        usb_free_buffer(uhci->td_pool);
        usb_free_buffer(uhci->async_qh);
        usb_free_buffer(uhci->frame_list);
        kfree(uhci);
        kfree(hc);
        return -1;
    }
    
    /* Register with USB core */
    if (usb_register_controller(hc) < 0) {
        usb_free_buffer(uhci->td_pool);
        usb_free_buffer(uhci->async_qh);
        usb_free_buffer(uhci->frame_list);
        kfree(uhci);
        kfree(hc);
        return -1;
    }
    
    serial_printf("[UHCI] Controller initialized with %d ports\n", uhci->port_count);
    return 0;
}
