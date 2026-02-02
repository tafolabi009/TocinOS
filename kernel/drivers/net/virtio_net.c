/**
 * TocinOS VirtIO Network Driver
 * 
 * VirtIO paravirtualized network device driver for virtual machines.
 * Provides high-performance networking in QEMU, KVM, and other hypervisors.
 * 
 * @author TocinOS Team
 */

#include "../../../include/kernel/memory.h"
#include "../../../include/stdint.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* External functions */
extern void serial_printf(const char *fmt, ...);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

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
 * VirtIO PCI Constants
 * ================================================================ */

/* PCI Vendor/Device IDs */
#define VIRTIO_VENDOR_ID        0x1AF4
#define VIRTIO_NET_DEVICE_ID    0x1000  /* Network device (legacy) */
#define VIRTIO_NET_DEVICE_ID_M  0x1041  /* Network device (modern) */

/* VirtIO PCI Configuration Registers (legacy) */
#define VIRTIO_PCI_HOST_FEATURES    0x00    /* Host features (32-bit) */
#define VIRTIO_PCI_GUEST_FEATURES   0x04    /* Guest features (32-bit) */
#define VIRTIO_PCI_QUEUE_ADDR       0x08    /* Queue address (32-bit) */
#define VIRTIO_PCI_QUEUE_SIZE       0x0C    /* Queue size (16-bit) */
#define VIRTIO_PCI_QUEUE_SELECT     0x0E    /* Queue select (16-bit) */
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10    /* Queue notify (16-bit) */
#define VIRTIO_PCI_STATUS           0x12    /* Device status (8-bit) */
#define VIRTIO_PCI_ISR              0x13    /* ISR status (8-bit) */
#define VIRTIO_PCI_CONFIG           0x14    /* Device config (offset) */

/* Device Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_FAILED        0x80

/* VirtIO Net Feature Bits */
#define VIRTIO_NET_F_CSUM           (1 << 0)    /* Checksum offload */
#define VIRTIO_NET_F_GUEST_CSUM     (1 << 1)    /* Guest handles csum */
#define VIRTIO_NET_F_MAC            (1 << 5)    /* MAC address in config */
#define VIRTIO_NET_F_GSO            (1 << 6)    /* GSO supported */
#define VIRTIO_NET_F_GUEST_TSO4     (1 << 7)    /* Guest handles TSO4 */
#define VIRTIO_NET_F_GUEST_TSO6     (1 << 8)    /* Guest handles TSO6 */
#define VIRTIO_NET_F_GUEST_ECN      (1 << 9)    /* Guest handles ECN */
#define VIRTIO_NET_F_GUEST_UFO      (1 << 10)   /* Guest handles UFO */
#define VIRTIO_NET_F_HOST_TSO4      (1 << 11)   /* Host handles TSO4 */
#define VIRTIO_NET_F_HOST_TSO6      (1 << 12)   /* Host handles TSO6 */
#define VIRTIO_NET_F_HOST_ECN       (1 << 13)   /* Host handles ECN */
#define VIRTIO_NET_F_HOST_UFO       (1 << 14)   /* Host handles UFO */
#define VIRTIO_NET_F_MRG_RXBUF      (1 << 15)   /* Merge RX buffers */
#define VIRTIO_NET_F_STATUS         (1 << 16)   /* Status field in config */
#define VIRTIO_NET_F_CTRL_VQ        (1 << 17)   /* Control virtqueue */
#define VIRTIO_NET_F_CTRL_RX        (1 << 18)   /* Control RX mode */
#define VIRTIO_NET_F_CTRL_VLAN      (1 << 19)   /* Control VLAN filtering */
#define VIRTIO_NET_F_GUEST_ANNOUNCE (1 << 21)   /* Guest announce */

/* VirtQueue indices */
#define VIRTIO_NET_RX_QUEUE     0
#define VIRTIO_NET_TX_QUEUE     1
#define VIRTIO_NET_CTRL_QUEUE   2

/* ================================================================
 * VirtIO Structures
 * ================================================================ */

/**
 * VirtQueue Descriptor
 */
typedef struct __attribute__((packed)) {
    uint64_t addr;          /* Physical address of buffer */
    uint32_t len;           /* Buffer length */
    uint16_t flags;         /* Descriptor flags */
    uint16_t next;          /* Next descriptor index */
} virtq_desc_t;

/* Descriptor flags */
#define VIRTQ_DESC_F_NEXT       1   /* Has next descriptor */
#define VIRTQ_DESC_F_WRITE      2   /* Device writes (vs reads) */
#define VIRTQ_DESC_F_INDIRECT   4   /* Buffer is indirect */

/**
 * VirtQueue Available Ring
 */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];     /* Descriptor indices */
    uint16_t used_event;    /* Only if VIRTIO_F_EVENT_IDX */
} virtq_avail_t;

/**
 * VirtQueue Used Element
 */
typedef struct __attribute__((packed)) {
    uint32_t id;            /* Descriptor index */
    uint32_t len;           /* Length written to descriptor */
} virtq_used_elem_t;

/**
 * VirtQueue Used Ring
 */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[256];
    uint16_t avail_event;   /* Only if VIRTIO_F_EVENT_IDX */
} virtq_used_t;

/**
 * VirtQueue structure
 */
typedef struct {
    uint16_t size;              /* Number of descriptors */
    uint16_t last_used_idx;     /* Last used index processed */
    
    virtq_desc_t *desc;         /* Descriptor table */
    virtq_avail_t *avail;       /* Available ring */
    virtq_used_t *used;         /* Used ring */
    
    void **buffers;             /* Buffer pointers for each descriptor */
} virtqueue_t;

/**
 * VirtIO Net Header
 */
typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;   /* Only if VIRTIO_NET_F_MRG_RXBUF */
} virtio_net_hdr_t;

/* GSO types */
#define VIRTIO_NET_HDR_GSO_NONE     0
#define VIRTIO_NET_HDR_GSO_TCPV4    1
#define VIRTIO_NET_HDR_GSO_UDP      3
#define VIRTIO_NET_HDR_GSO_TCPV6    4
#define VIRTIO_NET_HDR_GSO_ECN      0x80

/**
 * VirtIO Net Device
 */
typedef struct {
    uint16_t io_base;           /* I/O port base */
    uint8_t irq;                /* IRQ number */
    
    uint8_t mac[6];             /* MAC address */
    uint32_t features;          /* Negotiated features */
    
    virtqueue_t rx_vq;          /* Receive virtqueue */
    virtqueue_t tx_vq;          /* Transmit virtqueue */
    virtqueue_t ctrl_vq;        /* Control virtqueue (optional) */
    
    /* Statistics */
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_errors;
    uint32_t tx_errors;
    
    int initialized;
} virtio_net_dev_t;

static virtio_net_dev_t virtio_net;

/* RX buffer pool */
#define VIRTIO_NET_RX_BUF_SIZE  2048
#define VIRTIO_NET_RX_BUFFERS   64
static uint8_t *rx_buffers[VIRTIO_NET_RX_BUFFERS];

/* TX buffer pool */
#define VIRTIO_NET_TX_BUF_SIZE  2048
#define VIRTIO_NET_TX_BUFFERS   64
static uint8_t *tx_buffers[VIRTIO_NET_TX_BUFFERS];
static int tx_buffer_used[VIRTIO_NET_TX_BUFFERS];

/* ================================================================
 * VirtQueue Management
 * ================================================================ */

/**
 * Calculate virtqueue memory layout
 */
static uint32_t virtq_size(uint16_t qsz) {
    /* Align descriptor table */
    uint32_t desc_size = sizeof(virtq_desc_t) * qsz;
    /* Align available ring (to 2-byte boundary) */
    uint32_t avail_size = sizeof(uint16_t) * (3 + qsz);
    /* Align used ring (to 4096-byte boundary) */
    uint32_t used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * qsz;
    
    uint32_t first_part = ((desc_size + avail_size) + 4095) & ~4095;
    return first_part + used_size;
}

/**
 * Initialize a virtqueue
 */
static int virtq_init(virtqueue_t *vq, uint16_t io_base, int queue_idx) {
    /* Select queue */
    outw(io_base + VIRTIO_PCI_QUEUE_SELECT, queue_idx);
    
    /* Get queue size */
    uint16_t qsz = inw(io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (qsz == 0) {
        serial_printf("[VIRTIO-NET] Queue %d not available\n", queue_idx);
        return -1;
    }
    
    if (qsz > 256) qsz = 256;  /* Limit to our structure sizes */
    
    vq->size = qsz;
    vq->last_used_idx = 0;
    
    /* Allocate virtqueue memory (page-aligned) */
    uint32_t vq_size = virtq_size(qsz);
    void *vq_mem = kmalloc(vq_size + 4096);
    if (!vq_mem) {
        serial_printf("[VIRTIO-NET] Failed to allocate queue %d\n", queue_idx);
        return -1;
    }
    
    /* Align to page boundary */
    vq_mem = (void *)(((uint32_t)vq_mem + 4095) & ~4095);
    
    /* Clear memory */
    for (uint32_t i = 0; i < vq_size; i++) {
        ((uint8_t *)vq_mem)[i] = 0;
    }
    
    /* Set up pointers */
    vq->desc = (virtq_desc_t *)vq_mem;
    vq->avail = (virtq_avail_t *)((uint8_t *)vq_mem + sizeof(virtq_desc_t) * qsz);
    
    uint32_t used_offset = ((sizeof(virtq_desc_t) * qsz + sizeof(uint16_t) * (3 + qsz)) + 4095) & ~4095;
    vq->used = (virtq_used_t *)((uint8_t *)vq_mem + used_offset);
    
    /* Allocate buffer pointer array */
    vq->buffers = kmalloc(sizeof(void *) * qsz);
    if (!vq->buffers) {
        return -1;
    }
    for (int i = 0; i < qsz; i++) {
        vq->buffers[i] = NULL;
    }
    
    /* Tell device the queue address (page-aligned physical address) */
    uint32_t pfn = (uint32_t)vq_mem >> 12;  /* Page frame number */
    outl(io_base + VIRTIO_PCI_QUEUE_ADDR, pfn);
    
    serial_printf("[VIRTIO-NET] Queue %d initialized: size=%d\n", queue_idx, qsz);
    
    return 0;
}

/**
 * Add buffer to virtqueue (for RX)
 */
static int virtq_add_buf(virtqueue_t *vq, void *buf, uint32_t len, int writable) {
    uint16_t idx = vq->avail->idx % vq->size;
    uint16_t desc_idx = idx;
    
    /* Set up descriptor */
    vq->desc[desc_idx].addr = (uint64_t)(uint32_t)buf;
    vq->desc[desc_idx].len = len;
    vq->desc[desc_idx].flags = writable ? VIRTQ_DESC_F_WRITE : 0;
    vq->desc[desc_idx].next = 0;
    
    vq->buffers[desc_idx] = buf;
    
    /* Add to available ring */
    vq->avail->ring[idx] = desc_idx;
    
    /* Memory barrier */
    __asm__ volatile ("mfence" ::: "memory");
    
    /* Update available index */
    vq->avail->idx++;
    
    return 0;
}

/**
 * Notify device of available buffers
 */
static void virtq_kick(uint16_t io_base, int queue_idx) {
    outw(io_base + VIRTIO_PCI_QUEUE_NOTIFY, queue_idx);
}

/**
 * Get used buffer from virtqueue
 */
static void *virtq_get_buf(virtqueue_t *vq, uint32_t *len) {
    if (vq->last_used_idx == vq->used->idx) {
        return NULL;  /* No new buffers */
    }
    
    /* Memory barrier */
    __asm__ volatile ("mfence" ::: "memory");
    
    uint16_t idx = vq->last_used_idx % vq->size;
    uint32_t desc_idx = vq->used->ring[idx].id;
    
    if (len) {
        *len = vq->used->ring[idx].len;
    }
    
    void *buf = vq->buffers[desc_idx];
    vq->buffers[desc_idx] = NULL;
    
    vq->last_used_idx++;
    
    return buf;
}

/* ================================================================
 * Driver Functions
 * ================================================================ */

/**
 * Initialize VirtIO-Net device
 */
int virtio_net_init(uint16_t io_base, uint8_t irq) {
    serial_printf("[VIRTIO-NET] Initializing device at I/O 0x%04X, IRQ %d\n",
                  io_base, irq);
    
    virtio_net.io_base = io_base;
    virtio_net.irq = irq;
    virtio_net.initialized = 0;
    
    /* Reset device */
    outb(io_base + VIRTIO_PCI_STATUS, 0);
    
    /* Acknowledge device */
    outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    
    /* We're a driver */
    outb(io_base + VIRTIO_PCI_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    
    /* Read host features */
    uint32_t host_features = inl(io_base + VIRTIO_PCI_HOST_FEATURES);
    serial_printf("[VIRTIO-NET] Host features: 0x%08X\n", host_features);
    
    /* Select features we want */
    uint32_t guest_features = 0;
    
    if (host_features & VIRTIO_NET_F_MAC) {
        guest_features |= VIRTIO_NET_F_MAC;
    }
    if (host_features & VIRTIO_NET_F_STATUS) {
        guest_features |= VIRTIO_NET_F_STATUS;
    }
    if (host_features & VIRTIO_NET_F_CSUM) {
        guest_features |= VIRTIO_NET_F_CSUM;
    }
    
    /* Write guest features */
    outl(io_base + VIRTIO_PCI_GUEST_FEATURES, guest_features);
    virtio_net.features = guest_features;
    
    serial_printf("[VIRTIO-NET] Negotiated features: 0x%08X\n", guest_features);
    
    /* Read MAC address from config space */
    if (guest_features & VIRTIO_NET_F_MAC) {
        for (int i = 0; i < 6; i++) {
            virtio_net.mac[i] = inb(io_base + VIRTIO_PCI_CONFIG + i);
        }
    } else {
        /* Generate random MAC */
        virtio_net.mac[0] = 0x52;
        virtio_net.mac[1] = 0x54;
        virtio_net.mac[2] = 0x00;
        virtio_net.mac[3] = 0x12;
        virtio_net.mac[4] = 0x34;
        virtio_net.mac[5] = 0x56;
    }
    
    serial_printf("[VIRTIO-NET] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  virtio_net.mac[0], virtio_net.mac[1], virtio_net.mac[2],
                  virtio_net.mac[3], virtio_net.mac[4], virtio_net.mac[5]);
    
    /* Initialize virtqueues */
    if (virtq_init(&virtio_net.rx_vq, io_base, VIRTIO_NET_RX_QUEUE) != 0) {
        serial_printf("[VIRTIO-NET] Failed to init RX queue\n");
        outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }
    
    if (virtq_init(&virtio_net.tx_vq, io_base, VIRTIO_NET_TX_QUEUE) != 0) {
        serial_printf("[VIRTIO-NET] Failed to init TX queue\n");
        outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }
    
    /* Allocate RX buffers and add to queue */
    for (int i = 0; i < VIRTIO_NET_RX_BUFFERS && i < virtio_net.rx_vq.size; i++) {
        rx_buffers[i] = kmalloc(VIRTIO_NET_RX_BUF_SIZE);
        if (!rx_buffers[i]) break;
        
        virtq_add_buf(&virtio_net.rx_vq, rx_buffers[i], 
                      VIRTIO_NET_RX_BUF_SIZE, 1);
    }
    
    /* Allocate TX buffers */
    for (int i = 0; i < VIRTIO_NET_TX_BUFFERS; i++) {
        tx_buffers[i] = kmalloc(VIRTIO_NET_TX_BUF_SIZE);
        tx_buffer_used[i] = 0;
    }
    
    /* Notify device of RX buffers */
    virtq_kick(io_base, VIRTIO_NET_RX_QUEUE);
    
    /* Device is ready */
    outb(io_base + VIRTIO_PCI_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    
    virtio_net.initialized = 1;
    
    serial_printf("[VIRTIO-NET] Device initialized successfully\n");
    
    return 0;
}

/**
 * Send a packet
 */
int virtio_net_send(const void *data, uint32_t len) {
    if (!virtio_net.initialized) return -1;
    if (len > VIRTIO_NET_TX_BUF_SIZE - sizeof(virtio_net_hdr_t)) return -1;
    
    /* Find free TX buffer */
    int buf_idx = -1;
    for (int i = 0; i < VIRTIO_NET_TX_BUFFERS; i++) {
        if (!tx_buffer_used[i]) {
            buf_idx = i;
            break;
        }
    }
    
    if (buf_idx < 0) {
        virtio_net.tx_errors++;
        return -1;  /* No free buffers */
    }
    
    tx_buffer_used[buf_idx] = 1;
    
    uint8_t *buf = tx_buffers[buf_idx];
    
    /* Prepare VirtIO net header */
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)buf;
    hdr->flags = 0;
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;
    
    /* Copy packet data after header */
    const uint8_t *src = (const uint8_t *)data;
    uint8_t *dest = buf + sizeof(virtio_net_hdr_t);
    for (uint32_t i = 0; i < len; i++) {
        dest[i] = src[i];
    }
    
    /* Add to TX queue */
    uint16_t idx = virtio_net.tx_vq.avail->idx % virtio_net.tx_vq.size;
    
    virtio_net.tx_vq.desc[idx].addr = (uint64_t)(uint32_t)buf;
    virtio_net.tx_vq.desc[idx].len = sizeof(virtio_net_hdr_t) + len;
    virtio_net.tx_vq.desc[idx].flags = 0;  /* Read by device */
    virtio_net.tx_vq.desc[idx].next = 0;
    
    virtio_net.tx_vq.buffers[idx] = buf;
    virtio_net.tx_vq.avail->ring[idx] = idx;
    
    __asm__ volatile ("mfence" ::: "memory");
    
    virtio_net.tx_vq.avail->idx++;
    
    /* Notify device */
    virtq_kick(virtio_net.io_base, VIRTIO_NET_TX_QUEUE);
    
    virtio_net.tx_packets++;
    virtio_net.tx_bytes += len;
    
    return 0;
}

/**
 * Receive a packet
 */
int virtio_net_recv(void *buffer, uint32_t max_len) {
    if (!virtio_net.initialized) return -1;
    
    uint32_t len;
    void *buf = virtq_get_buf(&virtio_net.rx_vq, &len);
    
    if (!buf) {
        return 0;  /* No packet available */
    }
    
    /* Skip VirtIO header */
    uint8_t *pkt = (uint8_t *)buf + sizeof(virtio_net_hdr_t);
    uint32_t pkt_len = len - sizeof(virtio_net_hdr_t);
    
    if (pkt_len > max_len) {
        pkt_len = max_len;
    }
    
    /* Copy to caller's buffer */
    uint8_t *dest = (uint8_t *)buffer;
    for (uint32_t i = 0; i < pkt_len; i++) {
        dest[i] = pkt[i];
    }
    
    /* Return buffer to RX queue */
    virtq_add_buf(&virtio_net.rx_vq, buf, VIRTIO_NET_RX_BUF_SIZE, 1);
    virtq_kick(virtio_net.io_base, VIRTIO_NET_RX_QUEUE);
    
    virtio_net.rx_packets++;
    virtio_net.rx_bytes += pkt_len;
    
    return pkt_len;
}

/**
 * Handle interrupt
 */
void virtio_net_irq_handler(void) {
    if (!virtio_net.initialized) return;
    
    /* Read ISR status (also acknowledges interrupt) */
    uint8_t isr = inb(virtio_net.io_base + VIRTIO_PCI_ISR);
    
    if (isr & 0x01) {
        /* Used buffer notification - process completed TX */
        uint32_t len;
        void *buf;
        
        while ((buf = virtq_get_buf(&virtio_net.tx_vq, &len)) != NULL) {
            /* Find and free the buffer */
            for (int i = 0; i < VIRTIO_NET_TX_BUFFERS; i++) {
                if (tx_buffers[i] == buf) {
                    tx_buffer_used[i] = 0;
                    break;
                }
            }
        }
    }
    
    if (isr & 0x02) {
        /* Configuration change */
        serial_printf("[VIRTIO-NET] Configuration changed\n");
    }
}

/**
 * Get MAC address
 */
void virtio_net_get_mac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = virtio_net.mac[i];
    }
}

/**
 * Get link status
 */
int virtio_net_link_up(void) {
    if (!virtio_net.initialized) return 0;
    
    if (virtio_net.features & VIRTIO_NET_F_STATUS) {
        uint16_t status = inw(virtio_net.io_base + VIRTIO_PCI_CONFIG + 6);
        return (status & 1) != 0;
    }
    
    return 1;  /* Assume up if no status feature */
}

/**
 * Get statistics
 */
void virtio_net_get_stats(uint32_t *rx_pkts, uint32_t *tx_pkts,
                          uint32_t *rx_bytes_out, uint32_t *tx_bytes_out) {
    if (rx_pkts) *rx_pkts = virtio_net.rx_packets;
    if (tx_pkts) *tx_pkts = virtio_net.tx_packets;
    if (rx_bytes_out) *rx_bytes_out = virtio_net.rx_bytes;
    if (tx_bytes_out) *tx_bytes_out = virtio_net.tx_bytes;
}
