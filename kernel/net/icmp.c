/**
 * TocinOS ICMP (Internet Control Message Protocol) Implementation
 * 
 * Handles ICMP messages including ping (echo request/reply).
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

/* ================================================================
 * ICMP TYPES AND CODES
 * ================================================================ */

#define ICMP_TYPE_ECHO_REPLY        0
#define ICMP_TYPE_DEST_UNREACHABLE  3
#define ICMP_TYPE_SOURCE_QUENCH     4
#define ICMP_TYPE_REDIRECT          5
#define ICMP_TYPE_ECHO_REQUEST      8
#define ICMP_TYPE_TIME_EXCEEDED     11
#define ICMP_TYPE_PARAM_PROBLEM     12
#define ICMP_TYPE_TIMESTAMP         13
#define ICMP_TYPE_TIMESTAMP_REPLY   14

/* Destination Unreachable Codes */
#define ICMP_CODE_NET_UNREACHABLE   0
#define ICMP_CODE_HOST_UNREACHABLE  1
#define ICMP_CODE_PROTO_UNREACHABLE 2
#define ICMP_CODE_PORT_UNREACHABLE  3
#define ICMP_CODE_FRAG_NEEDED       4
#define ICMP_CODE_SOURCE_ROUTE_FAIL 5

/* ================================================================
 * ICMP STATISTICS
 * ================================================================ */

typedef struct {
    uint32_t msgs_received;
    uint32_t msgs_sent;
    uint32_t errors;
    uint32_t echo_requests;
    uint32_t echo_replies;
} icmp_stats_t;

static icmp_stats_t icmp_stats = {0};

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void icmp_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/**
 * Calculate ICMP checksum
 */
static uint16_t icmp_checksum(const void *data, uint16_t length) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    
    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    
    if (length > 0) {
        sum += *(const uint8_t *)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/* ================================================================
 * ICMP ECHO (PING)
 * ================================================================ */

/**
 * Send ICMP echo request (ping)
 */
int icmp_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq, 
                      const void *data, uint16_t data_len) {
    uint16_t total_len = sizeof(icmp_header_t) + data_len;
    uint8_t *packet = kmalloc(total_len);
    
    if (!packet) return -1;
    
    /* Build ICMP header */
    icmp_header_t *icmp = (icmp_header_t *)packet;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    
    /* Echo-specific fields in 'rest' */
    icmp->rest = htonl((id << 16) | seq);
    
    /* Copy data */
    if (data && data_len > 0) {
        icmp_memcpy(packet + sizeof(icmp_header_t), data, data_len);
    }
    
    /* Calculate checksum */
    icmp->checksum = icmp_checksum(packet, total_len);
    
    /* Send via IP layer */
    int ret = ip_send_packet(dest_ip, IP_PROTO_ICMP, packet, total_len);
    
    kfree(packet);
    
    if (ret == 0) {
        icmp_stats.msgs_sent++;
        icmp_stats.echo_requests++;
        serial_printf("[ICMP] Echo request sent to %d.%d.%d.%d\n",
                      (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
                      (dest_ip >> 8) & 0xFF, dest_ip & 0xFF);
    }
    
    return ret;
}

/**
 * Send ICMP echo reply
 */
int icmp_echo_reply(uint32_t dest_ip, uint16_t id, uint16_t seq,
                    const void *data, uint16_t data_len) {
    uint16_t total_len = sizeof(icmp_header_t) + data_len;
    uint8_t *packet = kmalloc(total_len);
    
    if (!packet) return -1;
    
    /* Build ICMP header */
    icmp_header_t *icmp = (icmp_header_t *)packet;
    icmp->type = ICMP_TYPE_ECHO_REPLY;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->rest = htonl((id << 16) | seq);
    
    /* Copy data */
    if (data && data_len > 0) {
        icmp_memcpy(packet + sizeof(icmp_header_t), data, data_len);
    }
    
    /* Calculate checksum */
    icmp->checksum = icmp_checksum(packet, total_len);
    
    /* Send via IP layer */
    int ret = ip_send_packet(dest_ip, IP_PROTO_ICMP, packet, total_len);
    
    kfree(packet);
    
    if (ret == 0) {
        icmp_stats.msgs_sent++;
        icmp_stats.echo_replies++;
    }
    
    return ret;
}

/* ================================================================
 * ICMP ERROR MESSAGES
 * ================================================================ */

/**
 * Send ICMP destination unreachable
 */
int icmp_dest_unreachable(uint32_t dest_ip, uint8_t code,
                          const void *orig_packet, uint16_t orig_len) {
    /* Include IP header + first 8 bytes of original datagram */
    uint16_t copy_len = (orig_len > 28) ? 28 : orig_len;
    uint16_t total_len = sizeof(icmp_header_t) + copy_len;
    
    uint8_t *packet = kmalloc(total_len);
    if (!packet) return -1;
    
    icmp_header_t *icmp = (icmp_header_t *)packet;
    icmp->type = ICMP_TYPE_DEST_UNREACHABLE;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->rest = 0;  /* Unused for most codes */
    
    icmp_memcpy(packet + sizeof(icmp_header_t), orig_packet, copy_len);
    icmp->checksum = icmp_checksum(packet, total_len);
    
    int ret = ip_send_packet(dest_ip, IP_PROTO_ICMP, packet, total_len);
    kfree(packet);
    
    if (ret == 0) {
        icmp_stats.msgs_sent++;
    }
    
    return ret;
}

/**
 * Send ICMP time exceeded
 */
int icmp_time_exceeded(uint32_t dest_ip, uint8_t code,
                       const void *orig_packet, uint16_t orig_len) {
    uint16_t copy_len = (orig_len > 28) ? 28 : orig_len;
    uint16_t total_len = sizeof(icmp_header_t) + copy_len;
    
    uint8_t *packet = kmalloc(total_len);
    if (!packet) return -1;
    
    icmp_header_t *icmp = (icmp_header_t *)packet;
    icmp->type = ICMP_TYPE_TIME_EXCEEDED;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->rest = 0;
    
    icmp_memcpy(packet + sizeof(icmp_header_t), orig_packet, copy_len);
    icmp->checksum = icmp_checksum(packet, total_len);
    
    int ret = ip_send_packet(dest_ip, IP_PROTO_ICMP, packet, total_len);
    kfree(packet);
    
    return ret;
}

/* ================================================================
 * ICMP RECEIVE HANDLING
 * ================================================================ */

/**
 * Process received ICMP packet
 */
int icmp_receive(uint32_t src_ip, const void *packet, uint16_t length) {
    if (!packet || length < sizeof(icmp_header_t)) {
        icmp_stats.errors++;
        return -1;
    }
    
    const icmp_header_t *icmp = (const icmp_header_t *)packet;
    
    /* Verify checksum */
    if (icmp_checksum(packet, length) != 0) {
        serial_printf("[ICMP] Bad checksum\n");
        icmp_stats.errors++;
        return -1;
    }
    
    icmp_stats.msgs_received++;
    
    uint16_t id = (ntohl(icmp->rest) >> 16) & 0xFFFF;
    uint16_t seq = ntohl(icmp->rest) & 0xFFFF;
    const void *data = (const uint8_t *)packet + sizeof(icmp_header_t);
    uint16_t data_len = length - sizeof(icmp_header_t);
    
    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            serial_printf("[ICMP] Echo request from %d.%d.%d.%d id=%d seq=%d\n",
                          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                          (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                          id, seq);
            /* Send echo reply */
            return icmp_echo_reply(src_ip, id, seq, data, data_len);
            
        case ICMP_TYPE_ECHO_REPLY:
            serial_printf("[ICMP] Echo reply from %d.%d.%d.%d id=%d seq=%d\n",
                          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                          (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                          id, seq);
            icmp_stats.echo_replies++;
            /* TODO: Notify waiting ping process */
            return 0;
            
        case ICMP_TYPE_DEST_UNREACHABLE:
            serial_printf("[ICMP] Destination unreachable from %d.%d.%d.%d code=%d\n",
                          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                          (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                          icmp->code);
            /* TODO: Notify upper layer */
            return 0;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            serial_printf("[ICMP] Time exceeded from %d.%d.%d.%d\n",
                          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                          (src_ip >> 8) & 0xFF, src_ip & 0xFF);
            return 0;
            
        default:
            serial_printf("[ICMP] Unknown type %d from %d.%d.%d.%d\n",
                          icmp->type,
                          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                          (src_ip >> 8) & 0xFF, src_ip & 0xFF);
            return 0;
    }
}

/* ================================================================
 * PING UTILITY
 * ================================================================ */

typedef struct {
    uint32_t dest_ip;
    uint16_t id;
    uint16_t seq;
    uint32_t send_time;
    int received;
    uint32_t rtt_ms;
} ping_state_t;

static ping_state_t ping_state = {0};

/**
 * Send ping and wait for reply
 * Returns RTT in milliseconds, or -1 on timeout
 */
int ping(uint32_t dest_ip, uint16_t timeout_ms) {
    static uint16_t ping_id = 1;
    static uint16_t ping_seq = 0;
    
    ping_state.dest_ip = dest_ip;
    ping_state.id = ping_id;
    ping_state.seq = ++ping_seq;
    ping_state.send_time = timer_get_ticks();
    ping_state.received = 0;
    
    /* Ping data */
    uint8_t data[32];
    for (int i = 0; i < 32; i++) {
        data[i] = 'a' + (i % 26);
    }
    
    /* Send echo request */
    if (icmp_echo_request(dest_ip, ping_id, ping_seq, data, 32) != 0) {
        return -1;
    }
    
    /* Wait for reply (simplified - should use proper waiting mechanism) */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < timeout_ms) {
        if (ping_state.received) {
            return ping_state.rtt_ms;
        }
        /* Small delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    serial_printf("[PING] Request timeout for %d.%d.%d.%d\n",
                  (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
                  (dest_ip >> 8) & 0xFF, dest_ip & 0xFF);
    
    return -1;  /* Timeout */
}

/**
 * Get ICMP statistics
 */
void icmp_get_stats(uint32_t *sent, uint32_t *received, uint32_t *errors) {
    if (sent) *sent = icmp_stats.msgs_sent;
    if (received) *received = icmp_stats.msgs_received;
    if (errors) *errors = icmp_stats.errors;
}

/* External timer function */
extern uint32_t timer_get_ticks(void);
