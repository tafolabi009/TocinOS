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

// Socket table
#define MAX_SOCKETS 64
static tcp_socket_t socket_table[MAX_SOCKETS];
static int socket_bitmap[MAX_SOCKETS / 32];

/**
 * Initialize TCP/IP stack
 */
int tcpip_init(void) {
    if (tcpip_initialized) {
        return 0;
    }
    
    // Clear socket table
    for (int i = 0; i < MAX_SOCKETS; i++) {
        socket_table[i].local_ip = 0;
        socket_table[i].remote_ip = 0;
        socket_table[i].local_port = 0;
        socket_table[i].remote_port = 0;
        socket_table[i].protocol = 0;
        socket_table[i].state = 0;
    }
    
    for (int i = 0; i < MAX_SOCKETS / 32; i++) {
        socket_bitmap[i] = 0;
    }
    
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
    
    switch (header->protocol) {
        case IP_PROTO_ICMP:
            // TODO: Handle ICMP
            break;
            
        case IP_PROTO_TCP:
            // TODO: Handle TCP
            break;
            
        case IP_PROTO_UDP:
            // TODO: Handle UDP
            break;
            
        default:
            // Unknown protocol
            break;
    }
    
    return 0;
}

/**
 * Open TCP connection
 */
int tcp_open(uint32_t dest_ip, uint16_t dest_port, uint16_t local_port) {
    if (!tcpip_initialized) {
        return -1;
    }
    
    // Find free socket
    int sockfd = -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        int word = i / 32;
        int bit = i % 32;
        if (!(socket_bitmap[word] & (1 << bit))) {
            socket_bitmap[word] |= (1 << bit);
            sockfd = i;
            break;
        }
    }
    
    if (sockfd < 0) {
        return -1;
    }
    
    // Initialize socket
    socket_table[sockfd].local_ip = net_iface.ip_addr;
    socket_table[sockfd].remote_ip = dest_ip;
    socket_table[sockfd].local_port = local_port;
    socket_table[sockfd].remote_port = dest_port;
    socket_table[sockfd].protocol = IP_PROTO_TCP;
    socket_table[sockfd].state = 0; // TODO: TCP state machine
    socket_table[sockfd].seq_num = 0;
    socket_table[sockfd].ack_num = 0;
    
    // TODO: Send SYN packet
    
    return sockfd;
}

/**
 * Close TCP connection
 */
int tcp_close(int sockfd) {
    if (!tcpip_initialized || sockfd < 0 || sockfd >= MAX_SOCKETS) {
        return -1;
    }
    
    int word = sockfd / 32;
    int bit = sockfd % 32;
    if (!(socket_bitmap[word] & (1 << bit))) {
        return -1; // Socket not open
    }
    
    // TODO: Send FIN packet
    
    // Clear socket
    socket_table[sockfd].local_ip = 0;
    socket_table[sockfd].remote_ip = 0;
    socket_table[sockfd].local_port = 0;
    socket_table[sockfd].remote_port = 0;
    socket_table[sockfd].protocol = 0;
    socket_table[sockfd].state = 0;
    
    socket_bitmap[word] &= ~(1 << bit);
    return 0;
}

/**
 * Send data over TCP
 */
int tcp_send(int sockfd, const void *data, uint16_t length) {
    if (!tcpip_initialized || sockfd < 0 || sockfd >= MAX_SOCKETS || !data) {
        return -1;
    }
    
    // TODO: Build TCP packet and send
    
    return 0;
}

/**
 * Receive data from TCP
 */
int tcp_receive(int sockfd, void *buffer, uint16_t max_length) {
    if (!tcpip_initialized || sockfd < 0 || sockfd >= MAX_SOCKETS || !buffer) {
        return -1;
    }
    
    // TODO: Receive TCP data
    
    return 0;
}

/**
 * Send ICMP echo request (ping)
 */
int icmp_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq, const void *data, uint16_t length) {
    if (!tcpip_initialized) {
        return -1;
    }
    
    // TODO: Build and send ICMP echo request
    
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
