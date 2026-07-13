/**
 * TocinOS TCP/IP Network Stack Implementation
 * 
 * Basic TCP/IP protocol implementation
 */

#include "../include/kernel/tcpip.h"
#include "../include/drivers/net.h"

// Network interface
static net_interface_t net_iface = {0};
static int tcpip_initialized = 0;

/**
 * Initialize TCP/IP stack
 */
int tcpip_init(void) {
    if (tcpip_initialized) {
        return 0;
    }

    // Initialize protocol layers (kernel/net)
    arp_init();
    tcp_init();
    udp_init();

    // Initialize interface
    net_iface.ip_addr = 0;
    net_iface.netmask = 0;
    net_iface.gateway = 0;
    net_iface.up = 0;
    
    tcpip_initialized = 1;
    return 0;
}

/**
 * Set network interface configuration
 */
int tcpip_set_interface(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if (!tcpip_initialized) {
        return -1;
    }
    
    net_iface.ip_addr = ip;
    net_iface.netmask = netmask;
    net_iface.gateway = gateway;
    net_iface.up = 1;
    
    // Get MAC address from network driver
    net_get_mac_address(net_iface.mac_addr);
    
    return 0;
}

/**
 * Get network interface configuration
 */
int tcpip_get_interface(net_interface_t *iface) {
    if (!tcpip_initialized || !iface) {
        return -1;
    }
    
    *iface = net_iface;
    return 0;
}

/**
 * Calculate IP checksum
 */
uint16_t ip_checksum(const void *data, uint16_t length) {
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

/**
 * Process incoming packet
 */
int tcpip_process_packet(const void *data, uint16_t length) {
    if (!tcpip_initialized || !data || length < sizeof(eth_frame_t)) {
        return -1;
    }
    
    const eth_frame_t *frame = (const eth_frame_t *)data;
    uint16_t ethertype = ntohs(frame->ethertype);
    
    switch (ethertype) {
        case ETHERTYPE_IPV4:
            return ip_receive(frame->payload, length - 14);
            
        case ETHERTYPE_ARP:
            // TODO: Handle ARP
            break;
            
        case ETHERTYPE_IPV6:
            // TODO: IPv6 support
            break;
            
        default:
            // Unknown protocol
            break;
    }
    
    return 0;
}

/**
 * Send IP packet
 */
int ip_send(uint32_t dest_ip, uint8_t protocol, const void *data, uint16_t length) {
    if (!tcpip_initialized || !data) {
        return -1;
    }
    
    // Build IP header
    ipv4_header_t header;
    header.version_ihl = 0x45;  // IPv4, 20 byte header
    header.dscp_ecn = 0;
    header.total_length = htons(sizeof(ipv4_header_t) + length);
    header.identification = 0;
    header.flags_fragment = 0;
    header.ttl = 64;
    header.protocol = protocol;
    header.src_ip = htonl(net_iface.ip_addr);
    header.dest_ip = htonl(dest_ip);
    header.checksum = 0;
    header.checksum = ip_checksum(&header, sizeof(header));
    
    // TODO: Send via network driver
    
    return 0;
}

/**
 * Send IP packet (kernel/net stack entry point)
 */
int ip_send_packet(uint32_t dest_ip, uint8_t protocol, const void *data, uint16_t length) {
    return ip_send(dest_ip, protocol, data, length);
}

/**
 * Get local interface IP address
 */
uint32_t tcpip_get_local_ip(void) {
    return net_iface.ip_addr;
}

/**
 * Receive IP packet
 */
int ip_receive(const void *packet, uint16_t length) {
    if (!packet || length < sizeof(ipv4_header_t)) {
        return -1;
    }
    
    const ipv4_header_t *header = (const ipv4_header_t *)packet;
    
    // Verify checksum
    uint16_t checksum = ip_checksum(header, sizeof(ipv4_header_t));
    if (checksum != 0) {
        return -1; // Bad checksum
    }
    
    // Check destination IP
    if (ntohl(header->dest_ip) != net_iface.ip_addr) {
        return -1; // Not for us
    }
    
    // Process based on protocol
    const void *payload = (const uint8_t *)packet + sizeof(ipv4_header_t);
    uint16_t payload_length = ntohs(header->total_length) - sizeof(ipv4_header_t);
    
    uint32_t src_ip = ntohl(header->src_ip);
    uint32_t dst_ip = ntohl(header->dest_ip);

    switch (header->protocol) {
        case IP_PROTO_ICMP:
            return icmp_receive(src_ip, payload, payload_length);

        case IP_PROTO_TCP:
            return tcp_receive(src_ip, dst_ip, payload, payload_length);

        case IP_PROTO_UDP:
            return udp_receive(src_ip, dst_ip, payload, payload_length);

        default:
            // Unknown protocol
            break;
    }

    return 0;
}

/**
 * Byte order conversion functions
 */
uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong & 0xFF0000) >> 8) |
           ((hostlong >> 24) & 0xFF);
}

uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

/**
 * Convert IP string to address
 */
uint32_t ip_str_to_addr(const char *str) {
    if (!str) {
        return 0;
    }
    
    uint32_t addr = 0;
    uint8_t octets[4] = {0};
    int octet_idx = 0;
    int num = 0;
    
    while (*str && octet_idx < 4) {
        if (*str >= '0' && *str <= '9') {
            num = num * 10 + (*str - '0');
        } else if (*str == '.') {
            octets[octet_idx++] = num;
            num = 0;
        }
        str++;
    }
    octets[octet_idx] = num;
    
    addr = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return addr;
}

/**
 * Convert IP address to string
 */
void ip_addr_to_str(uint32_t addr, char *str) {
    if (!str) {
        return;
    }
    
    uint8_t octets[4];
    octets[0] = (addr >> 24) & 0xFF;
    octets[1] = (addr >> 16) & 0xFF;
    octets[2] = (addr >> 8) & 0xFF;
    octets[3] = addr & 0xFF;
    
    // TODO: Convert to string
}
