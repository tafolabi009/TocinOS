/**
 * TocinOS TCP (Transmission Control Protocol) Implementation
 * 
 * Provides reliable, connection-oriented byte stream service.
 * Implements the TCP state machine per RFC 793.
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
extern uint32_t timer_get_ticks(void);

/* ================================================================
 * TCP CONSTANTS
 * ================================================================ */

#define TCP_HEADER_MIN_SIZE 20
#define TCP_MAX_SOCKETS     32
#define TCP_MAX_CONNECTIONS 64
#define TCP_RECV_BUFFER_SIZE 65536
#define TCP_SEND_BUFFER_SIZE 65536
#define TCP_MSS             1460  /* Maximum Segment Size (Ethernet) */
#define TCP_WINDOW_SIZE     65535

/* Timeouts (in milliseconds) */
#define TCP_SYN_TIMEOUT     3000
#define TCP_FIN_TIMEOUT     5000
#define TCP_TIME_WAIT       60000  /* 2 * MSL */
#define TCP_RETRANSMIT_BASE 1000
#define TCP_MAX_RETRIES     5

/* ================================================================
 * TCP FLAGS
 * ================================================================ */

#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10
#define TCP_FLAG_URG    0x20

/* ================================================================
 * TCP STATES (RFC 793)
 * ================================================================ */

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

static const char *tcp_state_names[] = {
    "CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED",
    "ESTABLISHED", "FIN_WAIT_1", "FIN_WAIT_2",
    "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT"
};

/* ================================================================
 * TCP CONNECTION BLOCK (TCB)
 * ================================================================ */

typedef struct {
    int in_use;
    tcp_state_t state;
    
    /* Local and remote endpoints */
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    
    /* Sequence numbers */
    uint32_t snd_una;     /* Send unacknowledged */
    uint32_t snd_nxt;     /* Send next */
    uint32_t snd_wnd;     /* Send window */
    uint32_t rcv_nxt;     /* Receive next */
    uint32_t rcv_wnd;     /* Receive window */
    uint32_t iss;         /* Initial send sequence */
    uint32_t irs;         /* Initial receive sequence */
    
    /* Receive buffer */
    uint8_t *recv_buffer;
    uint32_t recv_head;
    uint32_t recv_tail;
    uint32_t recv_count;
    
    /* Send buffer */
    uint8_t *send_buffer;
    uint32_t send_head;
    uint32_t send_tail;
    uint32_t send_count;
    
    /* Retransmission */
    uint32_t rto;          /* Retransmission timeout */
    uint32_t last_tx_time;
    int retries;
    
    /* Timers */
    uint32_t time_wait_start;
    
    /* Listening socket (for accepted connections) */
    int parent_socket;
    
    /* Backlog for listening sockets */
    int backlog;
    int pending_connections[16];
    int pending_count;
    
} tcp_cb_t;

static tcp_cb_t tcp_connections[TCP_MAX_CONNECTIONS];
static int tcp_initialized = 0;
static uint16_t next_ephemeral_port = 49152;

/* ================================================================
 * TCP STATISTICS
 * ================================================================ */

typedef struct {
    uint32_t segments_sent;
    uint32_t segments_received;
    uint32_t connections_opened;
    uint32_t connections_closed;
    uint32_t retransmissions;
    uint32_t errors;
} tcp_stats_t;

static tcp_stats_t tcp_stats = {0};

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void tcp_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void tcp_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

/* Generate initial sequence number (simplified) */
static uint32_t tcp_generate_iss(void) {
    static uint32_t iss = 0x12345678;
    iss += timer_get_ticks() + 64000;  /* RFC recommends incrementing */
    return iss;
}

/* Sequence number comparison (handles wrap-around) */
static int seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static int seq_le(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) <= 0;
}

static int seq_gt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

/* ================================================================
 * TCP CHECKSUM
 * ================================================================ */

/**
 * Calculate TCP checksum with pseudo-header
 */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip,
                              const void *tcp_packet, uint16_t tcp_length) {
    uint32_t sum = 0;
    const uint16_t *ptr;
    
    /* Pseudo header */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += htons(IP_PROTO_TCP);
    sum += htons(tcp_length);
    
    /* TCP header and data */
    ptr = (const uint16_t *)tcp_packet;
    uint16_t len = tcp_length;
    
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
    
    return ~sum;
}

/* ================================================================
 * TCP SEGMENT TRANSMISSION
 * ================================================================ */

/**
 * Send TCP segment
 */
static int tcp_send_segment(tcp_cb_t *tcb, uint8_t flags,
                            const void *data, uint16_t length) {
    uint16_t header_size = TCP_HEADER_MIN_SIZE;
    uint16_t total_length = header_size + length;
    
    uint8_t *packet = kmalloc(total_length);
    if (!packet) return -1;
    
    /* Build TCP header */
    tcp_header_t *tcp = (tcp_header_t *)packet;
    tcp->src_port = htons(tcb->local_port);
    tcp->dest_port = htons(tcb->remote_port);
    tcp->seq_num = htonl(tcb->snd_nxt);
    tcp->ack_num = htonl(tcb->rcv_nxt);
    tcp->data_offset = (header_size / 4) << 4;  /* Header length in 32-bit words */
    tcp->flags = flags;
    tcp->window = htons(tcb->rcv_wnd);
    tcp->checksum = 0;
    tcp->urgent = 0;
    
    /* Copy data if present */
    if (data && length > 0) {
        tcp_memcpy(packet + header_size, data, length);
    }
    
    /* Calculate checksum */
    tcp->checksum = tcp_checksum(tcb->local_ip, tcb->remote_ip, packet, total_length);
    
    /* Send via IP layer */
    int ret = ip_send_packet(tcb->remote_ip, IP_PROTO_TCP, packet, total_length);
    
    kfree(packet);
    
    if (ret == 0) {
        tcb->last_tx_time = timer_get_ticks();
        tcp_stats.segments_sent++;
        
        /* Update sequence number for data segments */
        if (length > 0) {
            tcb->snd_nxt += length;
        }
        
        /* SYN and FIN consume sequence numbers */
        if (flags & TCP_FLAG_SYN) tcb->snd_nxt++;
        if (flags & TCP_FLAG_FIN) tcb->snd_nxt++;
    }
    
    return ret;
}

/**
 * Send RST segment
 */
static int tcp_send_rst(uint32_t src_ip, uint16_t src_port,
                        uint32_t dest_ip, uint16_t dest_port,
                        uint32_t seq, uint32_t ack) {
    uint8_t packet[TCP_HEADER_MIN_SIZE];
    
    tcp_header_t *tcp = (tcp_header_t *)packet;
    tcp->src_port = htons(src_port);
    tcp->dest_port = htons(dest_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = (TCP_HEADER_MIN_SIZE / 4) << 4;
    tcp->flags = TCP_FLAG_RST | TCP_FLAG_ACK;
    tcp->window = 0;
    tcp->checksum = 0;
    tcp->urgent = 0;
    
    tcp->checksum = tcp_checksum(src_ip, dest_ip, packet, TCP_HEADER_MIN_SIZE);
    
    return ip_send_packet(dest_ip, IP_PROTO_TCP, packet, TCP_HEADER_MIN_SIZE);
}

/* ================================================================
 * TCP INITIALIZATION
 * ================================================================ */

/**
 * Initialize TCP subsystem
 */
int tcp_init(void) {
    if (tcp_initialized) return 0;
    
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_memset(&tcp_connections[i], 0, sizeof(tcp_cb_t));
        tcp_connections[i].state = TCP_STATE_CLOSED;
    }
    
    tcp_initialized = 1;
    serial_printf("[TCP] TCP subsystem initialized\n");
    
    return 0;
}

/* ================================================================
 * CONNECTION MANAGEMENT
 * ================================================================ */

/**
 * Allocate ephemeral port
 */
static uint16_t tcp_alloc_port(void) {
    uint16_t port = next_ephemeral_port++;
    
    if (next_ephemeral_port == 0) {
        next_ephemeral_port = 49152;
    }
    
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (tcp_connections[i].in_use && tcp_connections[i].local_port == port) {
            return tcp_alloc_port();
        }
    }
    
    return port;
}

/**
 * Find connection by endpoints
 */
static tcp_cb_t *tcp_find_connection(uint32_t local_ip, uint16_t local_port,
                                      uint32_t remote_ip, uint16_t remote_port) {
    /* First try exact match */
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_cb_t *tcb = &tcp_connections[i];
        if (tcb->in_use &&
            tcb->local_port == local_port &&
            tcb->remote_port == remote_port &&
            (tcb->local_ip == local_ip || tcb->local_ip == 0) &&
            tcb->remote_ip == remote_ip) {
            return tcb;
        }
    }
    
    /* Then try listening socket match */
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_cb_t *tcb = &tcp_connections[i];
        if (tcb->in_use &&
            tcb->state == TCP_STATE_LISTEN &&
            tcb->local_port == local_port &&
            (tcb->local_ip == local_ip || tcb->local_ip == 0)) {
            return tcb;
        }
    }
    
    return NULL;
}

/**
 * Allocate new TCB
 */
static tcp_cb_t *tcp_alloc_tcb(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!tcp_connections[i].in_use) {
            tcp_cb_t *tcb = &tcp_connections[i];
            tcp_memset(tcb, 0, sizeof(tcp_cb_t));
            tcb->in_use = 1;
            tcb->state = TCP_STATE_CLOSED;
            tcb->rcv_wnd = TCP_WINDOW_SIZE;
            tcb->rto = TCP_RETRANSMIT_BASE;
            tcb->parent_socket = -1;
            return tcb;
        }
    }
    return NULL;
}

/**
 * Get socket index for TCB
 */
static int tcp_tcb_to_socket(tcp_cb_t *tcb) {
    if (!tcb) return -1;
    return (int)(tcb - tcp_connections);
}

/* ================================================================
 * SOCKET API
 * ================================================================ */

/**
 * Create TCP socket
 */
int tcp_socket(void) {
    tcp_cb_t *tcb = tcp_alloc_tcb();
    if (!tcb) return -1;
    
    /* Allocate buffers */
    tcb->recv_buffer = kmalloc(TCP_RECV_BUFFER_SIZE);
    tcb->send_buffer = kmalloc(TCP_SEND_BUFFER_SIZE);
    
    if (!tcb->recv_buffer || !tcb->send_buffer) {
        if (tcb->recv_buffer) kfree(tcb->recv_buffer);
        if (tcb->send_buffer) kfree(tcb->send_buffer);
        tcb->in_use = 0;
        return -1;
    }
    
    int sockfd = tcp_tcb_to_socket(tcb);
    serial_printf("[TCP] Socket %d created\n", sockfd);
    return sockfd;
}

/**
 * Bind socket to local address
 */
int tcp_bind(int sockfd, uint16_t port) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use || tcb->state != TCP_STATE_CLOSED) return -1;
    
    /* Check if port is already in use */
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (i != sockfd && tcp_connections[i].in_use &&
            tcp_connections[i].local_port == port) {
            return -1;
        }
    }
    
    tcb->local_ip = tcpip_get_local_ip();
    tcb->local_port = (port != 0) ? port : tcp_alloc_port();
    
    serial_printf("[TCP] Socket %d bound to port %d\n", sockfd, tcb->local_port);
    return 0;
}

/**
 * Listen for connections
 */
int tcp_listen(int sockfd, int backlog) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use || tcb->state != TCP_STATE_CLOSED) return -1;
    if (tcb->local_port == 0) return -1;  /* Must be bound first */
    
    tcb->backlog = (backlog > 16) ? 16 : backlog;
    tcb->state = TCP_STATE_LISTEN;
    
    serial_printf("[TCP] Socket %d listening on port %d\n", sockfd, tcb->local_port);
    return 0;
}

/**
 * Accept incoming connection
 */
int tcp_accept(int sockfd) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use || tcb->state != TCP_STATE_LISTEN) return -1;
    
    /* Check for pending connections */
    if (tcb->pending_count == 0) {
        return -1;  /* No pending connections */
    }
    
    /* Get first pending connection */
    int new_sockfd = tcb->pending_connections[0];
    
    /* Shift remaining pending connections */
    for (int i = 0; i < tcb->pending_count - 1; i++) {
        tcb->pending_connections[i] = tcb->pending_connections[i + 1];
    }
    tcb->pending_count--;
    
    tcp_cb_t *new_tcb = &tcp_connections[new_sockfd];
    serial_printf("[TCP] Accepted connection %d from %d.%d.%d.%d:%d\n",
                  new_sockfd,
                  (new_tcb->remote_ip >> 24) & 0xFF,
                  (new_tcb->remote_ip >> 16) & 0xFF,
                  (new_tcb->remote_ip >> 8) & 0xFF,
                  new_tcb->remote_ip & 0xFF,
                  new_tcb->remote_port);
    
    return new_sockfd;
}

/**
 * Connect to remote host (active open)
 */
int tcp_connect(int sockfd, uint32_t remote_ip, uint16_t remote_port) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use || tcb->state != TCP_STATE_CLOSED) return -1;
    
    /* Auto-bind if needed */
    if (tcb->local_port == 0) {
        tcb->local_ip = tcpip_get_local_ip();
        tcb->local_port = tcp_alloc_port();
    }
    
    tcb->remote_ip = remote_ip;
    tcb->remote_port = remote_port;
    
    /* Initialize sequence numbers */
    tcb->iss = tcp_generate_iss();
    tcb->snd_una = tcb->iss;
    tcb->snd_nxt = tcb->iss;
    
    /* Send SYN */
    tcb->state = TCP_STATE_SYN_SENT;
    tcp_send_segment(tcb, TCP_FLAG_SYN, NULL, 0);
    
    serial_printf("[TCP] Socket %d connecting to %d.%d.%d.%d:%d (state: %s)\n",
                  sockfd,
                  (remote_ip >> 24) & 0xFF, (remote_ip >> 16) & 0xFF,
                  (remote_ip >> 8) & 0xFF, remote_ip & 0xFF,
                  remote_port, tcp_state_names[tcb->state]);
    
    tcp_stats.connections_opened++;
    
    /* Wait for connection to establish (simplified - synchronous) */
    uint32_t start = timer_get_ticks();
    while (tcb->state == TCP_STATE_SYN_SENT) {
        if (timer_get_ticks() - start > TCP_SYN_TIMEOUT) {
            tcb->state = TCP_STATE_CLOSED;
            return -1;  /* Timeout */
        }
        /* Would normally yield here */
        for (volatile int i = 0; i < 10000; i++);
    }
    
    return (tcb->state == TCP_STATE_ESTABLISHED) ? 0 : -1;
}

/**
 * Send data on connection
 */
int tcp_write(int sockfd, const void *data, uint16_t length) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    if (!data || length == 0) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use || tcb->state != TCP_STATE_ESTABLISHED) return -1;
    
    /* Add data to send buffer */
    uint32_t space = TCP_SEND_BUFFER_SIZE - tcb->send_count;
    uint16_t to_send = (length < space) ? length : space;
    
    for (uint16_t i = 0; i < to_send; i++) {
        tcb->send_buffer[tcb->send_tail] = ((const uint8_t *)data)[i];
        tcb->send_tail = (tcb->send_tail + 1) % TCP_SEND_BUFFER_SIZE;
    }
    tcb->send_count += to_send;
    
    /* Send segments */
    while (tcb->send_count > 0) {
        uint16_t seg_len = (tcb->send_count > TCP_MSS) ? TCP_MSS : tcb->send_count;
        uint8_t seg_data[TCP_MSS];
        
        for (uint16_t i = 0; i < seg_len; i++) {
            seg_data[i] = tcb->send_buffer[tcb->send_head];
            tcb->send_head = (tcb->send_head + 1) % TCP_SEND_BUFFER_SIZE;
        }
        tcb->send_count -= seg_len;
        
        tcp_send_segment(tcb, TCP_FLAG_ACK | TCP_FLAG_PSH, seg_data, seg_len);
    }
    
    serial_printf("[TCP] Socket %d sent %d bytes\n", sockfd, to_send);
    return to_send;
}

/**
 * Receive data from connection
 */
int tcp_read(int sockfd, void *buffer, uint16_t max_length) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    if (!buffer || max_length == 0) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use) return -1;
    
    /* Check connection state */
    if (tcb->state != TCP_STATE_ESTABLISHED &&
        tcb->state != TCP_STATE_FIN_WAIT_1 &&
        tcb->state != TCP_STATE_FIN_WAIT_2 &&
        tcb->state != TCP_STATE_CLOSE_WAIT) {
        if (tcb->recv_count == 0) return -1;
    }
    
    /* Copy data from receive buffer */
    uint16_t to_read = (tcb->recv_count < max_length) ? tcb->recv_count : max_length;
    
    for (uint16_t i = 0; i < to_read; i++) {
        ((uint8_t *)buffer)[i] = tcb->recv_buffer[tcb->recv_head];
        tcb->recv_head = (tcb->recv_head + 1) % TCP_RECV_BUFFER_SIZE;
    }
    tcb->recv_count -= to_read;
    
    /* Update receive window */
    tcb->rcv_wnd = TCP_RECV_BUFFER_SIZE - tcb->recv_count;
    
    return to_read;
}

/**
 * Close connection
 */
int tcp_close_socket(int sockfd) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use) return -1;
    
    serial_printf("[TCP] Closing socket %d (state: %s)\n", 
                  sockfd, tcp_state_names[tcb->state]);
    
    switch (tcb->state) {
        case TCP_STATE_CLOSED:
        case TCP_STATE_LISTEN:
            /* Just cleanup */
            break;
            
        case TCP_STATE_SYN_SENT:
            /* Cancel connection attempt */
            tcb->state = TCP_STATE_CLOSED;
            break;
            
        case TCP_STATE_SYN_RECEIVED:
        case TCP_STATE_ESTABLISHED:
            /* Send FIN */
            tcp_send_segment(tcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            tcb->state = TCP_STATE_FIN_WAIT_1;
            break;
            
        case TCP_STATE_CLOSE_WAIT:
            /* Send FIN */
            tcp_send_segment(tcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            tcb->state = TCP_STATE_LAST_ACK;
            break;
            
        case TCP_STATE_FIN_WAIT_1:
        case TCP_STATE_FIN_WAIT_2:
        case TCP_STATE_CLOSING:
        case TCP_STATE_LAST_ACK:
        case TCP_STATE_TIME_WAIT:
            /* Already closing */
            break;
    }
    
    /* For simplified implementation, cleanup immediately for closed states */
    if (tcb->state == TCP_STATE_CLOSED) {
        if (tcb->recv_buffer) kfree(tcb->recv_buffer);
        if (tcb->send_buffer) kfree(tcb->send_buffer);
        tcp_memset(tcb, 0, sizeof(tcp_cb_t));
        tcp_stats.connections_closed++;
    }
    
    return 0;
}

/* ================================================================
 * TCP INPUT PROCESSING
 * ================================================================ */

/**
 * Process incoming TCP segment
 */
int tcp_receive(uint32_t src_ip, uint32_t dest_ip,
                const void *packet, uint16_t length) {
    if (!packet || length < TCP_HEADER_MIN_SIZE) {
        tcp_stats.errors++;
        return -1;
    }
    
    const tcp_header_t *tcp = (const tcp_header_t *)packet;
    
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dest_port = ntohs(tcp->dest_port);
    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t flags = tcp->flags;
    uint16_t window = ntohs(tcp->window);
    uint8_t header_len = (tcp->data_offset >> 4) * 4;
    
    /* Verify checksum */
    if (tcp_checksum(src_ip, dest_ip, packet, length) != 0) {
        serial_printf("[TCP] Bad checksum\n");
        tcp_stats.errors++;
        return -1;
    }
    
    tcp_stats.segments_received++;
    
    /* Find connection */
    tcp_cb_t *tcb = tcp_find_connection(dest_ip, dest_port, src_ip, src_port);
    
    if (!tcb) {
        /* No connection found - send RST if not RST */
        if (!(flags & TCP_FLAG_RST)) {
            tcp_send_rst(dest_ip, dest_port, src_ip, src_port, ack, seq + 1);
        }
        return -1;
    }
    
    /* Data portion */
    const uint8_t *data = (const uint8_t *)packet + header_len;
    uint16_t data_len = length - header_len;
    
    serial_printf("[TCP] Received: port=%d seq=%u ack=%u flags=%02X len=%d state=%s\n",
                  dest_port, seq, ack, flags, data_len, tcp_state_names[tcb->state]);
    
    /* State machine */
    switch (tcb->state) {
        case TCP_STATE_CLOSED:
            if (!(flags & TCP_FLAG_RST)) {
                tcp_send_rst(dest_ip, dest_port, src_ip, src_port, ack, seq + 1);
            }
            break;
            
        case TCP_STATE_LISTEN:
            if (flags & TCP_FLAG_SYN) {
                /* Create new connection for incoming SYN */
                tcp_cb_t *new_tcb = tcp_alloc_tcb();
                if (!new_tcb) break;
                
                /* Allocate buffers */
                new_tcb->recv_buffer = kmalloc(TCP_RECV_BUFFER_SIZE);
                new_tcb->send_buffer = kmalloc(TCP_SEND_BUFFER_SIZE);
                if (!new_tcb->recv_buffer || !new_tcb->send_buffer) {
                    if (new_tcb->recv_buffer) kfree(new_tcb->recv_buffer);
                    if (new_tcb->send_buffer) kfree(new_tcb->send_buffer);
                    new_tcb->in_use = 0;
                    break;
                }
                
                new_tcb->local_ip = dest_ip;
                new_tcb->local_port = dest_port;
                new_tcb->remote_ip = src_ip;
                new_tcb->remote_port = src_port;
                new_tcb->parent_socket = tcp_tcb_to_socket(tcb);
                
                /* Initialize sequence numbers */
                new_tcb->irs = seq;
                new_tcb->rcv_nxt = seq + 1;
                new_tcb->iss = tcp_generate_iss();
                new_tcb->snd_una = new_tcb->iss;
                new_tcb->snd_nxt = new_tcb->iss;
                new_tcb->snd_wnd = window;
                
                /* Send SYN-ACK */
                new_tcb->state = TCP_STATE_SYN_RECEIVED;
                tcp_send_segment(new_tcb, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                
                /* Add to parent's pending list */
                if (tcb->pending_count < tcb->backlog) {
                    tcb->pending_connections[tcb->pending_count++] = 
                        tcp_tcb_to_socket(new_tcb);
                }
                
                tcp_stats.connections_opened++;
            }
            break;
            
        case TCP_STATE_SYN_SENT:
            if (flags & TCP_FLAG_ACK) {
                if (seq_le(ack, tcb->iss) || seq_gt(ack, tcb->snd_nxt)) {
                    if (!(flags & TCP_FLAG_RST)) {
                        tcp_send_rst(dest_ip, dest_port, src_ip, src_port, ack, 0);
                    }
                    break;
                }
            }
            
            if (flags & TCP_FLAG_RST) {
                tcb->state = TCP_STATE_CLOSED;
                break;
            }
            
            if (flags & TCP_FLAG_SYN) {
                tcb->irs = seq;
                tcb->rcv_nxt = seq + 1;
                tcb->snd_wnd = window;
                
                if (flags & TCP_FLAG_ACK) {
                    tcb->snd_una = ack;
                }
                
                if (seq_gt(tcb->snd_una, tcb->iss)) {
                    /* Our SYN was ACKed */
                    tcb->state = TCP_STATE_ESTABLISHED;
                    tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0);
                    serial_printf("[TCP] Connection established\n");
                } else {
                    /* Simultaneous open */
                    tcb->state = TCP_STATE_SYN_RECEIVED;
                    tcp_send_segment(tcb, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                }
            }
            break;
            
        case TCP_STATE_SYN_RECEIVED:
            if (flags & TCP_FLAG_RST) {
                tcb->state = TCP_STATE_CLOSED;
                break;
            }
            
            if (flags & TCP_FLAG_ACK) {
                if (seq_le(tcb->snd_una, ack) && seq_le(ack, tcb->snd_nxt)) {
                    tcb->state = TCP_STATE_ESTABLISHED;
                    tcb->snd_una = ack;
                    serial_printf("[TCP] Connection established (from SYN_RECEIVED)\n");
                }
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            if (flags & TCP_FLAG_RST) {
                tcb->state = TCP_STATE_CLOSED;
                break;
            }
            
            /* Process ACK */
            if (flags & TCP_FLAG_ACK) {
                if (seq_le(tcb->snd_una, ack) && seq_le(ack, tcb->snd_nxt)) {
                    tcb->snd_una = ack;
                }
            }
            
            /* Process data */
            if (data_len > 0 && seq == tcb->rcv_nxt) {
                /* In-order data - add to receive buffer */
                uint32_t space = TCP_RECV_BUFFER_SIZE - tcb->recv_count;
                uint16_t to_copy = (data_len < space) ? data_len : space;
                
                for (uint16_t i = 0; i < to_copy; i++) {
                    tcb->recv_buffer[tcb->recv_tail] = data[i];
                    tcb->recv_tail = (tcb->recv_tail + 1) % TCP_RECV_BUFFER_SIZE;
                }
                tcb->recv_count += to_copy;
                tcb->rcv_nxt += to_copy;
                tcb->rcv_wnd = TCP_RECV_BUFFER_SIZE - tcb->recv_count;
                
                /* Send ACK */
                tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0);
            }
            
            /* Process FIN */
            if (flags & TCP_FLAG_FIN) {
                tcb->rcv_nxt = seq + 1;
                tcb->state = TCP_STATE_CLOSE_WAIT;
                tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0);
                serial_printf("[TCP] FIN received, moving to CLOSE_WAIT\n");
            }
            break;
            
        case TCP_STATE_FIN_WAIT_1:
            if (flags & TCP_FLAG_ACK) {
                if (ack == tcb->snd_nxt) {
                    tcb->state = TCP_STATE_FIN_WAIT_2;
                }
            }
            if (flags & TCP_FLAG_FIN) {
                tcb->rcv_nxt = seq + 1;
                tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0);
                if (tcb->state == TCP_STATE_FIN_WAIT_2) {
                    tcb->state = TCP_STATE_TIME_WAIT;
                    tcb->time_wait_start = timer_get_ticks();
                } else {
                    tcb->state = TCP_STATE_CLOSING;
                }
            }
            break;
            
        case TCP_STATE_FIN_WAIT_2:
            if (flags & TCP_FLAG_FIN) {
                tcb->rcv_nxt = seq + 1;
                tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0);
                tcb->state = TCP_STATE_TIME_WAIT;
                tcb->time_wait_start = timer_get_ticks();
            }
            break;
            
        case TCP_STATE_CLOSE_WAIT:
            /* Application should call close */
            break;
            
        case TCP_STATE_CLOSING:
            if (flags & TCP_FLAG_ACK) {
                tcb->state = TCP_STATE_TIME_WAIT;
                tcb->time_wait_start = timer_get_ticks();
            }
            break;
            
        case TCP_STATE_LAST_ACK:
            if (flags & TCP_FLAG_ACK) {
                tcb->state = TCP_STATE_CLOSED;
                /* Cleanup */
                if (tcb->recv_buffer) kfree(tcb->recv_buffer);
                if (tcb->send_buffer) kfree(tcb->send_buffer);
                tcp_memset(tcb, 0, sizeof(tcp_cb_t));
                tcp_stats.connections_closed++;
            }
            break;
            
        case TCP_STATE_TIME_WAIT:
            /* Wait for 2MSL timeout */
            if (timer_get_ticks() - tcb->time_wait_start > TCP_TIME_WAIT) {
                tcb->state = TCP_STATE_CLOSED;
                if (tcb->recv_buffer) kfree(tcb->recv_buffer);
                if (tcb->send_buffer) kfree(tcb->send_buffer);
                tcp_memset(tcb, 0, sizeof(tcp_cb_t));
                tcp_stats.connections_closed++;
            }
            break;
    }
    
    return 0;
}

/* ================================================================
 * STATISTICS AND DEBUGGING
 * ================================================================ */

/**
 * Get TCP statistics
 */
void tcp_get_stats(uint32_t *sent, uint32_t *received, 
                   uint32_t *opened, uint32_t *closed) {
    if (sent) *sent = tcp_stats.segments_sent;
    if (received) *received = tcp_stats.segments_received;
    if (opened) *opened = tcp_stats.connections_opened;
    if (closed) *closed = tcp_stats.connections_closed;
}

/**
 * Get socket state
 */
int tcp_get_state(int sockfd) {
    if (sockfd < 0 || sockfd >= TCP_MAX_CONNECTIONS) return -1;
    
    tcp_cb_t *tcb = &tcp_connections[sockfd];
    if (!tcb->in_use) return -1;
    
    return tcb->state;
}

/**
 * Print connection table (debug)
 */
void tcp_print_connections(void) {
    serial_printf("[TCP] Connection table:\n");
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_cb_t *tcb = &tcp_connections[i];
        if (tcb->in_use) {
            serial_printf("  [%d] %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d (%s)\n",
                          i,
                          (tcb->local_ip >> 24) & 0xFF,
                          (tcb->local_ip >> 16) & 0xFF,
                          (tcb->local_ip >> 8) & 0xFF,
                          tcb->local_ip & 0xFF,
                          tcb->local_port,
                          (tcb->remote_ip >> 24) & 0xFF,
                          (tcb->remote_ip >> 16) & 0xFF,
                          (tcb->remote_ip >> 8) & 0xFF,
                          tcb->remote_ip & 0xFF,
                          tcb->remote_port,
                          tcp_state_names[tcb->state]);
        }
    }
}
