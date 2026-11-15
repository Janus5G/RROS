#ifndef EXPERIMENTALOS_KERNEL_NETWORK_H
#define EXPERIMENTALOS_KERNEL_NETWORK_H

#include "efi.h"

#define OS_NETWORK_FRAME_MAX 1514u
#define OS_NETWORK_UDP_PAYLOAD_MAX 512u
#define OS_NETWORK_DNS_NAME_MAX 96u

typedef int (*os_network_send_frame)(
    void *context,
    const u8 *frame,
    usize length
);
typedef int (*os_network_receive_frame)(
    void *context,
    u8 *frame,
    usize capacity,
    usize *length
);

typedef struct {
    void *context;
    os_network_send_frame send_frame;
    os_network_receive_frame receive_frame;
} os_network_device;

typedef struct {
    os_network_device device;
    u8 mac[6];
    u8 neighbor_mac[6];
    u32 ip;
    u32 netmask;
    u32 gateway;
    u32 dns_server;
    u32 neighbor_ip;
    u32 next_ip_id;
    u32 tx_frames;
    u32 rx_frames;
    u32 invalid_frames;
    u32 udp_received;
    u32 tcp_received;
    u32 icmp_received;
    u32 arp_received;
    u32 icmp_echo_replies;
    u32 dhcp_xid;
    u32 dhcp_offered_ip;
    u32 dhcp_server_id;
    u32 dhcp_state;
    u16 dns_id;
    u16 dns_state;
    char dns_name[OS_NETWORK_DNS_NAME_MAX];
    u32 dns_result;
    u16 tcp_local_port;
    u16 tcp_remote_port;
    u32 tcp_remote_ip;
    u32 tcp_sequence;
    u32 tcp_receive_sequence;
    u32 tcp_state;
    u16 last_udp_source_port;
    u16 last_udp_destination_port;
    u16 last_udp_length;
    u8 last_udp_payload[OS_NETWORK_UDP_PAYLOAD_MAX];
    u8 last_tcp_flags;
    u16 last_tcp_source_port;
    u16 last_tcp_destination_port;
    u32 last_tcp_sequence;
    u32 last_tcp_acknowledgment;
    u16 last_arp_operation;
    u32 last_icmp_type;
    u8 ready;
} os_network_stack;

int os_network_stack_init(
    os_network_stack *stack,
    const os_network_device *device,
    const u8 mac[6],
    u32 initial_ipv4_address
);
int os_network_stack_poll(os_network_stack *stack);
int os_network_stack_start_dhcp(
    os_network_stack *stack,
    u32 transaction_id
);
int os_network_self_test(void);
u32 os_network_self_test_error(void);

#endif
