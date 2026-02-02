/**
 * TocinOS ARP (Address Resolution Protocol) Implementation
 * 
 * Resolves IPv4 addresses to MAC (Ethernet) addresses.
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
extern uint32_t timer_get_ticks(void);
extern int net_transmit(const void *data, uint16_t length);
extern void net_get_mac_address(uint8_t *mac);

/* ================================================================
 * ARP CONSTANTS
 * ================================================================ */

#define ARP_HW_ETHERNET     0x0001
#define ARP_PROTO_IPV4      0x0800
#define ARP_OP_REQUEST      0x0001
#define ARP_OP_REPLY        0x0002

#define ARP_CACHE_SIZE      64
#define ARP_TIMEOUT_MS      (20 * 60 * 1000)  /* 20 minutes */
#define ARP_PENDING_TIMEOUT 3000              /* 3 seconds for pending entries */
#define ARP_RETRIES         3

/* ================================================================
 * ARP CACHE
 * ================================================================ */

typedef enum {
    ARP_STATE_FREE = 0,
    ARP_STATE_PENDING,
    ARP_STATE_RESOLVED
} arp_state_t;

typedef struct {
    uint32_t ip_addr;
    uint8_t mac_addr[6];
    arp_state_t state;
    uint32_t timestamp;
    int retries;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static int arp_initialized = 0;

/* Local network interface info */
static uint8_t local_mac[6];
static uint32_t local_ip = 0;

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void arp_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void arp_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

static int arp_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

/* ================================================================
 * ARP INITIALIZATION
 * ================================================================ */

/**
 * Initialize ARP subsystem
 */
int arp_init(void) {
    if (arp_initialized) return 0;
    
    /* Clear cache */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].state = ARP_STATE_FREE;
        arp_cache[i].ip_addr = 0;
        arp_cache[i].timestamp = 0;
    }
    
    /* Get local MAC */
    net_get_mac_address(local_mac);
    
    arp_initialized = 1;
    serial_printf("[ARP] ARP subsystem initialized\n");
    
    return 0;
}

/**
 * Set local IP address for ARP
 */
void arp_set_ip(uint32_t ip) {
    local_ip = ip;
}

/* ================================================================
 * ARP CACHE OPERATIONS
 * ================================================================ */

/**
 * Find entry in ARP cache
 */
static arp_entry_t *arp_cache_find(uint32_t ip_addr) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE && 
            arp_cache[i].ip_addr == ip_addr) {
            return &arp_cache[i];
        }
    }
    return NULL;
}

/**
 * Allocate new cache entry
 */
static arp_entry_t *arp_cache_alloc(void) {
    uint32_t now = timer_get_ticks();
    
    /* Find free entry */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_FREE) {
            return &arp_cache[i];
        }
    }
    
    /* Find oldest entry */
    arp_entry_t *oldest = &arp_cache[0];
    for (int i = 1; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].timestamp < oldest->timestamp) {
            oldest = &arp_cache[i];
        }
    }
    
    /* Evict oldest entry */
    oldest->state = ARP_STATE_FREE;
    return oldest;
}

/**
 * Add or update cache entry
 */
static void arp_cache_add(uint32_t ip_addr, const uint8_t *mac_addr) {
    arp_entry_t *entry = arp_cache_find(ip_addr);
    
    if (!entry) {
        entry = arp_cache_alloc();
    }
    
    entry->ip_addr = ip_addr;
    arp_memcpy(entry->mac_addr, mac_addr, 6);
    entry->state = ARP_STATE_RESOLVED;
    entry->timestamp = timer_get_ticks();
    
    serial_printf("[ARP] Cache: %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                  (ip_addr >> 24) & 0xFF, (ip_addr >> 16) & 0xFF,
                  (ip_addr >> 8) & 0xFF, ip_addr & 0xFF,
                  mac_addr[0], mac_addr[1], mac_addr[2],
                  mac_addr[3], mac_addr[4], mac_addr[5]);
}

/* ================================================================
 * ARP PACKET HANDLING
 * ================================================================ */

/**
 * Build and send ARP request
 */
int arp_send_request(uint32_t target_ip) {
    uint8_t packet[42];  /* Ethernet header (14) + ARP (28) */
    
    /* Ethernet header */
    arp_memset(packet, 0xFF, 6);  /* Broadcast destination */
    arp_memcpy(packet + 6, local_mac, 6);  /* Source MAC */
    packet[12] = (ETHERTYPE_ARP >> 8) & 0xFF;
    packet[13] = ETHERTYPE_ARP & 0xFF;
    
    /* ARP header */
    arp_packet_t *arp = (arp_packet_t *)(packet + 14);
    arp->hw_type = htons(ARP_HW_ETHERNET);
    arp->proto_type = htons(ARP_PROTO_IPV4);
    arp->hw_addr_len = 6;
    arp->proto_addr_len = 4;
    arp->operation = htons(ARP_OP_REQUEST);
    arp_memcpy(arp->sender_hw_addr, local_mac, 6);
    arp->sender_proto_addr = htonl(local_ip);
    arp_memset(arp->target_hw_addr, 0, 6);
    arp->target_proto_addr = htonl(target_ip);
    
    serial_printf("[ARP] Sending request for %d.%d.%d.%d\n",
                  (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
                  (target_ip >> 8) & 0xFF, target_ip & 0xFF);
    
    return net_transmit(packet, sizeof(packet));
}

/**
 * Build and send ARP reply
 */
int arp_send_reply(uint32_t target_ip, const uint8_t *target_mac) {
    uint8_t packet[42];
    
    /* Ethernet header */
    arp_memcpy(packet, target_mac, 6);  /* Destination */
    arp_memcpy(packet + 6, local_mac, 6);  /* Source */
    packet[12] = (ETHERTYPE_ARP >> 8) & 0xFF;
    packet[13] = ETHERTYPE_ARP & 0xFF;
    
    /* ARP header */
    arp_packet_t *arp = (arp_packet_t *)(packet + 14);
    arp->hw_type = htons(ARP_HW_ETHERNET);
    arp->proto_type = htons(ARP_PROTO_IPV4);
    arp->hw_addr_len = 6;
    arp->proto_addr_len = 4;
    arp->operation = htons(ARP_OP_REPLY);
    arp_memcpy(arp->sender_hw_addr, local_mac, 6);
    arp->sender_proto_addr = htonl(local_ip);
    arp_memcpy(arp->target_hw_addr, target_mac, 6);
    arp->target_proto_addr = htonl(target_ip);
    
    return net_transmit(packet, sizeof(packet));
}

/**
 * Process incoming ARP packet
 */
int arp_receive(const arp_packet_t *arp, uint16_t length) {
    if (!arp || length < sizeof(arp_packet_t)) {
        return -1;
    }
    
    /* Validate packet */
    if (ntohs(arp->hw_type) != ARP_HW_ETHERNET ||
        ntohs(arp->proto_type) != ARP_PROTO_IPV4 ||
        arp->hw_addr_len != 6 ||
        arp->proto_addr_len != 4) {
        return -1;
    }
    
    uint32_t sender_ip = ntohl(arp->sender_proto_addr);
    uint32_t target_ip = ntohl(arp->target_proto_addr);
    uint16_t operation = ntohs(arp->operation);
    
    /* Update cache with sender info */
    arp_cache_add(sender_ip, arp->sender_hw_addr);
    
    /* Handle request for our IP */
    if (operation == ARP_OP_REQUEST && target_ip == local_ip) {
        serial_printf("[ARP] Request for our IP, sending reply\n");
        arp_send_reply(sender_ip, arp->sender_hw_addr);
    }
    
    /* Handle reply - cache is already updated above */
    if (operation == ARP_OP_REPLY) {
        serial_printf("[ARP] Reply received from %d.%d.%d.%d\n",
                      (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                      (sender_ip >> 8) & 0xFF, sender_ip & 0xFF);
    }
    
    return 0;
}

/**
 * Resolve IP to MAC address (may block)
 */
int arp_resolve(uint32_t ip_addr, uint8_t *mac_addr) {
    if (!mac_addr) return -1;
    
    /* Check if it's broadcast */
    if (ip_addr == 0xFFFFFFFF) {
        arp_memset(mac_addr, 0xFF, 6);
        return 0;
    }
    
    /* Look in cache first */
    arp_entry_t *entry = arp_cache_find(ip_addr);
    if (entry && entry->state == ARP_STATE_RESOLVED) {
        arp_memcpy(mac_addr, entry->mac_addr, 6);
        return 0;
    }
    
    /* Need to send ARP request */
    if (!entry) {
        entry = arp_cache_alloc();
        entry->ip_addr = ip_addr;
        entry->state = ARP_STATE_PENDING;
        entry->retries = 0;
        entry->timestamp = timer_get_ticks();
    }
    
    /* Send request */
    arp_send_request(ip_addr);
    entry->retries++;
    
    /* Wait for reply (with timeout) */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ARP_PENDING_TIMEOUT) {
        entry = arp_cache_find(ip_addr);
        if (entry && entry->state == ARP_STATE_RESOLVED) {
            arp_memcpy(mac_addr, entry->mac_addr, 6);
            return 0;
        }
        
        /* Small delay */
        for (volatile int i = 0; i < 10000; i++);
    }
    
    /* Retry if we haven't exceeded limit */
    if (entry && entry->retries < ARP_RETRIES) {
        return arp_resolve(ip_addr, mac_addr);  /* Recursive retry */
    }
    
    serial_printf("[ARP] Resolution failed for %d.%d.%d.%d\n",
                  (ip_addr >> 24) & 0xFF, (ip_addr >> 16) & 0xFF,
                  (ip_addr >> 8) & 0xFF, ip_addr & 0xFF);
    
    return -1;  /* Failed to resolve */
}

/**
 * Gratuitous ARP announcement
 */
int arp_announce(void) {
    return arp_send_request(local_ip);
}

/**
 * Clear ARP cache
 */
void arp_cache_clear(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].state = ARP_STATE_FREE;
    }
}

/**
 * Print ARP cache (debug)
 */
void arp_cache_print(void) {
    serial_printf("[ARP] Cache contents:\n");
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_RESOLVED) {
            uint32_t ip = arp_cache[i].ip_addr;
            uint8_t *mac = arp_cache[i].mac_addr;
            serial_printf("  %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                          (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                          (ip >> 8) & 0xFF, ip & 0xFF,
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
}
