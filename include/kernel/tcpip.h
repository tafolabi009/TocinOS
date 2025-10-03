/**
 * TocinOS TCP/IP Network Stack
 * 
 * Provides TCP/IP protocol implementation for networking
 */

#ifndef TCPIP_H
#define TCPIP_H

#include "../stdint.h"

// Ethernet frame structure
typedef struct {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint8_t payload[1500];
} __attribute__((packed)) eth_frame_t;

// IPv4 header
typedef struct {
    uint8_t version_ihl;        // Version (4 bits) + IHL (4 bits)
    uint8_t dscp_ecn;           // DSCP (6 bits) + ECN (2 bits)
    uint16_t total_length;      // Total length
    uint16_t identification;    // Identification
    uint16_t flags_fragment;    // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t ttl;                // Time to live
    uint8_t protocol;           // Protocol
    uint16_t checksum;          // Header checksum
    uint32_t src_ip;            // Source IP
    uint32_t dest_ip;           // Destination IP
} __attribute__((packed)) ipv4_header_t;

// TCP header
typedef struct {
    uint16_t src_port;          // Source port
    uint16_t dest_port;         // Destination port
    uint32_t seq_num;           // Sequence number
    uint32_t ack_num;           // Acknowledgment number
    uint8_t data_offset;        // Data offset (4 bits) + Reserved (3 bits) + Flags (1 bit)
    uint8_t flags;              // Flags (8 bits)
    uint16_t window;            // Window size
    uint16_t checksum;          // Checksum
    uint16_t urgent_ptr;        // Urgent pointer
} __attribute__((packed)) tcp_header_t;

// UDP header
typedef struct {
    uint16_t src_port;          // Source port
    uint16_t dest_port;         // Destination port
    uint16_t length;            // Length
    uint16_t checksum;          // Checksum
} __attribute__((packed)) udp_header_t;

// ICMP header
typedef struct {
    uint8_t type;               // ICMP type
    uint8_t code;               // ICMP code
    uint16_t checksum;          // Checksum
    uint32_t rest;              // Rest of header (varies by type)
} __attribute__((packed)) icmp_header_t;

// ARP packet
typedef struct {
    uint16_t hw_type;           // Hardware type
    uint16_t proto_type;        // Protocol type
    uint8_t hw_addr_len;        // Hardware address length
    uint8_t proto_addr_len;     // Protocol address length
    uint16_t operation;         // Operation
    uint8_t sender_hw_addr[6];  // Sender hardware address
    uint32_t sender_proto_addr; // Sender protocol address
    uint8_t target_hw_addr[6];  // Target hardware address
    uint32_t target_proto_addr; // Target protocol address
} __attribute__((packed)) arp_packet_t;

// Protocol numbers
#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17

// Ethertype values
#define ETHERTYPE_IPV4      0x0800
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_IPV6      0x86DD

// TCP flags
#define TCP_FIN             0x01
#define TCP_SYN             0x02
#define TCP_RST             0x04
#define TCP_PSH             0x08
#define TCP_ACK             0x10
#define TCP_URG             0x20

// Socket structure
typedef struct {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t protocol;
    uint8_t state;
    uint32_t seq_num;
    uint32_t ack_num;
} tcp_socket_t;

// Network interface
typedef struct {
    uint8_t mac_addr[6];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint8_t up;
} net_interface_t;

// TCP/IP stack API
int tcpip_init(void);
int tcpip_set_interface(uint32_t ip, uint32_t netmask, uint32_t gateway);
int tcpip_get_interface(net_interface_t *iface);

// Packet processing
int tcpip_process_packet(const void *data, uint16_t length);
int tcpip_send_packet(const void *data, uint16_t length, uint32_t dest_ip);

// IP layer
uint16_t ip_checksum(const void *data, uint16_t length);
int ip_send(uint32_t dest_ip, uint8_t protocol, const void *data, uint16_t length);
int ip_receive(const void *packet, uint16_t length);

// TCP layer
int tcp_open(uint32_t dest_ip, uint16_t dest_port, uint16_t local_port);
int tcp_close(int sockfd);
int tcp_send(int sockfd, const void *data, uint16_t length);
int tcp_receive(int sockfd, void *buffer, uint16_t max_length);
int tcp_listen(uint16_t port);
int tcp_accept(int listen_sockfd);

// UDP layer
int udp_open(uint16_t local_port);
int udp_close(int sockfd);
int udp_send(int sockfd, uint32_t dest_ip, uint16_t dest_port, const void *data, uint16_t length);
int udp_receive(int sockfd, void *buffer, uint16_t max_length, uint32_t *src_ip, uint16_t *src_port);

// ICMP layer
int icmp_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq, const void *data, uint16_t length);
int icmp_echo_reply(uint32_t dest_ip, uint16_t id, uint16_t seq, const void *data, uint16_t length);

// ARP layer
int arp_resolve(uint32_t ip_addr, uint8_t *mac_addr);
int arp_send_request(uint32_t target_ip);
int arp_send_reply(uint32_t target_ip, const uint8_t *target_mac);

// Utility functions
uint32_t ip_str_to_addr(const char *str);
void ip_addr_to_str(uint32_t addr, char *str);
uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);

#endif // TCPIP_H
