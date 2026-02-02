/**
 * TocinOS UDP (User Datagram Protocol) Implementation
 * 
 * Provides connectionless, unreliable datagram service.
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/tcpip.h"
#include "../../include/kernel/memory.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* External functions */
extern void serial_printf(const char *fmt, ...);
extern int ip_send_packet(uint32_t dest_ip, uint8_t protocol, const void *data, uint16_t length);
extern uint32_t tcpip_get_local_ip(void);

/* ================================================================
 * UDP CONSTANTS
 * ================================================================ */

#define UDP_HEADER_SIZE     8
#define UDP_MAX_SOCKETS     64
#define UDP_RECV_QUEUE_SIZE 16
#define UDP_MAX_PAYLOAD     65507  /* 65535 - 20 (IP) - 8 (UDP) */

/* ================================================================
 * UDP SOCKET STRUCTURES
 * ================================================================ */

typedef struct udp_packet {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t length;
    uint8_t *data;
    struct udp_packet *next;
} udp_packet_t;

typedef struct {
    int in_use;
    uint16_t local_port;
    uint32_t local_ip;
    int bound;
    
    /* Connected mode (optional) */
    uint32_t remote_ip;
    uint16_t remote_port;
    int connected;
    
    /* Receive queue */
    udp_packet_t *recv_queue_head;
    udp_packet_t *recv_queue_tail;
    int recv_queue_count;
    
    /* Statistics */
    uint32_t packets_sent;
    uint32_t packets_received;
} udp_socket_t;

static udp_socket_t udp_sockets[UDP_MAX_SOCKETS];
static int udp_initialized = 0;
static uint16_t next_ephemeral_port = 49152;

/* ================================================================
 * UDP STATISTICS
 * ================================================================ */

typedef struct {
    uint32_t datagrams_received;
    uint32_t datagrams_sent;
    uint32_t receive_errors;
    uint32_t no_port_errors;
    uint32_t checksum_errors;
} udp_stats_t;

static udp_stats_t udp_stats = {0};

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void udp_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void udp_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

/* ================================================================
 * UDP CHECKSUM
 * ================================================================ */

/**
 * Calculate UDP checksum with pseudo-header
 */
static uint16_t udp_checksum(uint32_t src_ip, uint32_t dest_ip,
                              const void *udp_packet, uint16_t udp_length) {
    uint32_t sum = 0;
    const uint16_t *ptr;
    
    /* Pseudo header */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += htons(IP_PROTO_UDP);
    sum += htons(udp_length);
    
    /* UDP header and data */
    ptr = (const uint16_t *)udp_packet;
    uint16_t len = udp_length;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len > 0) {
        sum += *(const uint8_t *)ptr;
    }
    
    /* Fold 32-bit sum to 16-bit */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    uint16_t result = ~sum;
    /* UDP allows 0 checksum to mean "no checksum" */
    return result == 0 ? 0xFFFF : result;
}

/* ================================================================
 * UDP INITIALIZATION
 * ================================================================ */

/**
 * Initialize UDP subsystem
 */
int udp_init(void) {
    if (udp_initialized) return 0;
    
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        udp_memset(&udp_sockets[i], 0, sizeof(udp_socket_t));
    }
    
    udp_initialized = 1;
    serial_printf("[UDP] UDP subsystem initialized\n");
    
    return 0;
}

/* ================================================================
 * PORT MANAGEMENT
 * ================================================================ */

/**
 * Allocate ephemeral port
 */
static uint16_t udp_alloc_port(void) {
    uint16_t port = next_ephemeral_port++;
    
    /* Wrap around */
    if (next_ephemeral_port == 0) {
        next_ephemeral_port = 49152;
    }
    
    /* Check if port is already in use */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].in_use && udp_sockets[i].local_port == port) {
            return udp_alloc_port();  /* Try next port */
        }
    }
    
    return port;
}

/**
 * Find socket by local port
 */
static udp_socket_t *udp_find_socket(uint16_t local_port) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].in_use && 
            udp_sockets[i].bound &&
            udp_sockets[i].local_port == local_port) {
            return &udp_sockets[i];
        }
    }
    return NULL;
}

/* ================================================================
 * SOCKET OPERATIONS
 * ================================================================ */

/**
 * Create UDP socket
 * Returns socket descriptor (index) or -1 on error
 */
int udp_socket(void) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].in_use) {
            udp_memset(&udp_sockets[i], 0, sizeof(udp_socket_t));
            udp_sockets[i].in_use = 1;
            serial_printf("[UDP] Socket %d created\n", i);
            return i;
        }
    }
    return -1;  /* No free sockets */
}

/**
 * Bind socket to local port
 */
int udp_bind(int sockfd, uint16_t port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    
    udp_socket_t *sock = &udp_sockets[sockfd];
    if (!sock->in_use) return -1;
    
    /* Check if port is already in use */
    if (port != 0) {
        for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
            if (i != sockfd && 
                udp_sockets[i].in_use && 
                udp_sockets[i].bound &&
                udp_sockets[i].local_port == port) {
                return -1;  /* Port already in use */
            }
        }
        sock->local_port = port;
    } else {
        sock->local_port = udp_alloc_port();
    }
    
    sock->local_ip = tcpip_get_local_ip();
    sock->bound = 1;
    
    serial_printf("[UDP] Socket %d bound to port %d\n", sockfd, sock->local_port);
    return 0;
}

/**
 * Connect socket to remote address (optional for UDP)
 */
int udp_connect(int sockfd, uint32_t remote_ip, uint16_t remote_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    
    udp_socket_t *sock = &udp_sockets[sockfd];
    if (!sock->in_use) return -1;
    
    /* Auto-bind if not bound */
    if (!sock->bound) {
        if (udp_bind(sockfd, 0) != 0) return -1;
    }
    
    sock->remote_ip = remote_ip;
    sock->remote_port = remote_port;
    sock->connected = 1;
    
    serial_printf("[UDP] Socket %d connected to %d.%d.%d.%d:%d\n", 
                  sockfd,
                  (remote_ip >> 24) & 0xFF, (remote_ip >> 16) & 0xFF,
                  (remote_ip >> 8) & 0xFF, remote_ip & 0xFF,
                  remote_port);
    
    return 0;
}

/**
 * Send datagram to connected peer
 */
int udp_send(int sockfd, const void *data, uint16_t length) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    
    udp_socket_t *sock = &udp_sockets[sockfd];
    if (!sock->in_use || !sock->connected) return -1;
    
    return udp_sendto(sockfd, data, length, sock->remote_ip, sock->remote_port);
}

/**
 * Send datagram to specified address
 */
int udp_sendto(int sockfd, const void *data, uint16_t length,
               uint32_t dest_ip, uint16_t dest_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    if (!data || length == 0 || length > UDP_MAX_PAYLOAD) return -1;
    
    udp_socket_t *sock = &udp_sockets[sockfd];
    if (!sock->in_use) return -1;
    
    /* Auto-bind if not bound */
    if (!sock->bound) {
        if (udp_bind(sockfd, 0) != 0) return -1;
    }
    
    /* Build UDP packet */
    uint16_t udp_length = UDP_HEADER_SIZE + length;
    uint8_t *packet = kmalloc(udp_length);
    if (!packet) return -1;
    
    udp_header_t *udp = (udp_header_t *)packet;
    udp->src_port = htons(sock->local_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(udp_length);
    udp->checksum = 0;
    
    /* Copy data */
    udp_memcpy(packet + UDP_HEADER_SIZE, data, length);
    
    /* Calculate checksum */
    uint32_t src_ip = sock->local_ip;
    udp->checksum = udp_checksum(src_ip, dest_ip, packet, udp_length);
    
    /* Send via IP layer */
    int ret = ip_send_packet(dest_ip, IP_PROTO_UDP, packet, udp_length);
    
    kfree(packet);
    
    if (ret == 0) {
        sock->packets_sent++;
        udp_stats.datagrams_sent++;
        serial_printf("[UDP] Sent %d bytes to %d.%d.%d.%d:%d\n",
                      length,
                      (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
                      (dest_ip >> 8) & 0xFF, dest_ip & 0xFF,
                      dest_port);
    }
    
    return ret == 0 ? length : -1;
}

/**
 * Receive datagram (blocking or non-blocking based on implementation)
 */
int udp_recv(int sockfd, void *buffer, uint16_t max_length) {
    uint32_t src_ip;
    uint16_t src_port;
    return udp_recvfrom(sockfd, buffer, max_length, &src_ip, &src_port);
}

/**
 * Receive datagram with source address
 */
int udp_recvfrom(int sockfd, void *buffer, uint16_t max_length,
                 uint32_t *src_ip, uint16_t *src_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    if (!buffer || max_length == 0) return -1;
    
    udp_socket_t *sock = &udp_sockets[sockfd];
    if (!sock->in_use || !sock->bound) return -1;
    
    /* Check receive queue */
    if (sock->recv_queue_head == NULL) {
        return 0;  /* No data available (non-blocking) */
    }
    
    /* Dequeue packet */
    udp_packet_t *pkt = sock->recv_queue_head;
    sock->recv_queue_head = pkt->next;
    if (sock->recv_queue_head == NULL) {
        sock->recv_queue_tail = NULL;
    }
    sock->recv_queue_count--;
    
    /* Copy data to buffer */
    uint16_t copy_len = (pkt->length < max_length) ? pkt->length : max_length;
    udp_memcpy(buffer, pkt->data, copy_len);
    
    /* Return source address if requested */
    if (src_ip) *src_ip = pkt->src_ip;
    if (src_port) *src_port = pkt->src_port;
    
    /* Free packet */
    kfree(pkt->data);
    kfree(pkt);
    
    return copy_len;
}

/**
 * Close UDP socket
 */
int udp_close(int sockfd) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    
    udp_socket_t *sock = &udp_sockets[sockfd];
    if (!sock->in_use) return -1;
    
    /* Free receive queue */
    while (sock->recv_queue_head != NULL) {
        udp_packet_t *pkt = sock->recv_queue_head;
        sock->recv_queue_head = pkt->next;
        kfree(pkt->data);
        kfree(pkt);
    }
    
    udp_memset(sock, 0, sizeof(udp_socket_t));
    serial_printf("[UDP] Socket %d closed\n", sockfd);
    
    return 0;
}

/* ================================================================
 * UDP PACKET RECEPTION
 * ================================================================ */

/**
 * Process received UDP packet (called by IP layer)
 */
int udp_receive(uint32_t src_ip, uint32_t dest_ip,
                const void *packet, uint16_t length) {
    if (!packet || length < UDP_HEADER_SIZE) {
        udp_stats.receive_errors++;
        return -1;
    }
    
    const udp_header_t *udp = (const udp_header_t *)packet;
    
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t udp_length = ntohs(udp->length);
    
    /* Validate length */
    if (udp_length < UDP_HEADER_SIZE || udp_length > length) {
        udp_stats.receive_errors++;
        return -1;
    }
    
    /* Verify checksum if present */
    if (udp->checksum != 0) {
        uint16_t calc_checksum = udp_checksum(src_ip, dest_ip, packet, udp_length);
        if (calc_checksum != 0) {
            serial_printf("[UDP] Bad checksum\n");
            udp_stats.checksum_errors++;
            return -1;
        }
    }
    
    udp_stats.datagrams_received++;
    
    /* Find socket for this port */
    udp_socket_t *sock = udp_find_socket(dest_port);
    if (sock == NULL) {
        serial_printf("[UDP] No socket for port %d\n", dest_port);
        udp_stats.no_port_errors++;
        /* TODO: Send ICMP port unreachable */
        return -1;
    }
    
    /* If connected, check source */
    if (sock->connected) {
        if (sock->remote_ip != src_ip || sock->remote_port != src_port) {
            return 0;  /* Silently drop */
        }
    }
    
    /* Check queue limit */
    if (sock->recv_queue_count >= UDP_RECV_QUEUE_SIZE) {
        serial_printf("[UDP] Receive queue full\n");
        return -1;
    }
    
    /* Allocate packet structure */
    uint16_t data_len = udp_length - UDP_HEADER_SIZE;
    udp_packet_t *pkt = kmalloc(sizeof(udp_packet_t));
    if (!pkt) return -1;
    
    pkt->data = kmalloc(data_len);
    if (!pkt->data) {
        kfree(pkt);
        return -1;
    }
    
    pkt->src_ip = src_ip;
    pkt->src_port = src_port;
    pkt->length = data_len;
    pkt->next = NULL;
    
    udp_memcpy(pkt->data, (const uint8_t *)packet + UDP_HEADER_SIZE, data_len);
    
    /* Enqueue packet */
    if (sock->recv_queue_tail == NULL) {
        sock->recv_queue_head = pkt;
    } else {
        sock->recv_queue_tail->next = pkt;
    }
    sock->recv_queue_tail = pkt;
    sock->recv_queue_count++;
    sock->packets_received++;
    
    serial_printf("[UDP] Received %d bytes from %d.%d.%d.%d:%d on port %d\n",
                  data_len,
                  (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                  (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                  src_port, dest_port);
    
    return 0;
}

/* ================================================================
 * STATISTICS
 * ================================================================ */

/**
 * Get UDP statistics
 */
void udp_get_stats(uint32_t *sent, uint32_t *received, uint32_t *errors) {
    if (sent) *sent = udp_stats.datagrams_sent;
    if (received) *received = udp_stats.datagrams_received;
    if (errors) *errors = udp_stats.receive_errors + udp_stats.checksum_errors;
}
