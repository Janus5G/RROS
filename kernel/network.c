#include "network.h"
#include "efi.h"

#define NET_ETHERNET_HEADER 14u
#define NET_IPV4_HEADER 20u
#define NET_UDP_HEADER 8u
#define NET_TCP_HEADER 20u
#define NET_FRAME_MAX OS_NETWORK_FRAME_MAX
#define NET_QUEUE_DEPTH 4u
#define NET_UDP_PAYLOAD_MAX OS_NETWORK_UDP_PAYLOAD_MAX
#define NET_DNS_NAME_MAX OS_NETWORK_DNS_NAME_MAX
#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_ETHERTYPE_ARP 0x0806u
#define NET_PROTOCOL_ICMP 1u
#define NET_PROTOCOL_TCP 6u
#define NET_PROTOCOL_UDP 17u
#define NET_IP_BROADCAST 0xffffffffu
#define NET_DHCP_DISCOVER 1u
#define NET_DHCP_OFFER 2u
#define NET_DHCP_REQUEST 3u
#define NET_DHCP_ACK 5u
#define NET_DHCP_CLIENT_PORT 68u
#define NET_DHCP_SERVER_PORT 67u
#define NET_DNS_PORT 53u
#define NET_DNS_LOCAL_PORT 53000u
#define NET_TCP_CLOSED 0u
#define NET_TCP_SYN_SENT 1u
#define NET_TCP_ESTABLISHED 2u

typedef struct {
    u8 frames[NET_QUEUE_DEPTH][NET_FRAME_MAX];
    u16 lengths[NET_QUEUE_DEPTH];
    u32 head;
    u32 count;
} net_frame_queue;

typedef struct {
    net_frame_queue receive[2];
} net_vm_link;

typedef struct {
    net_vm_link *link;
    u32 endpoint;
} net_vm_endpoint;

typedef os_network_device net_device;
typedef os_network_stack net_stack;

static const u8 g_broadcast_mac[6] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static net_vm_link g_vm_link;
static net_vm_endpoint g_endpoints[2];
static net_stack g_client;
static net_stack g_peer;
static u32 g_network_test_failure;

#define NETWORK_TEST_REQUIRE(condition, stage) \
    do { \
        if (condition) { \
            g_network_test_failure = stage; \
            return 0; \
        } \
    } while (0)

static void zero_bytes(void *memory, usize size)
{
    u8 *bytes = (u8 *)memory;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, usize size)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    for (usize i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

static int bytes_equal(const u8 *left, const u8 *right, usize size)
{
    for (usize i = 0; i < size; ++i) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static u16 read_be16(const u8 *bytes)
{
    return (u16)(((u16)bytes[0] << 8) | bytes[1]);
}

static u32 read_be32(const u8 *bytes)
{
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16)
        | ((u32)bytes[2] << 8) | bytes[3];
}

static void write_be16(u8 *bytes, u16 value)
{
    bytes[0] = (u8)(value >> 8);
    bytes[1] = (u8)value;
}

static void write_be32(u8 *bytes, u32 value)
{
    bytes[0] = (u8)(value >> 24);
    bytes[1] = (u8)(value >> 16);
    bytes[2] = (u8)(value >> 8);
    bytes[3] = (u8)value;
}

static u32 checksum_sum(u32 sum, const u8 *bytes, usize length)
{
    usize i = 0;
    for (; i + 1u < length; i += 2u) {
        sum += read_be16(bytes + i);
    }
    if (i < length) {
        sum += (u32)bytes[i] << 8;
    }
    return sum;
}

static u16 checksum_finish(u32 sum)
{
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (u16)~sum;
}

static u16 internet_checksum(const u8 *bytes, usize length)
{
    return checksum_finish(checksum_sum(0, bytes, length));
}

static u32 transport_sum(u32 source, u32 destination, u8 protocol, u16 length)
{
    u32 sum = (source >> 16) + (source & 0xffffu)
        + (destination >> 16) + (destination & 0xffffu)
        + protocol + length;
    return sum;
}

static u16 transport_checksum(
    u32 source,
    u32 destination,
    u8 protocol,
    const u8 *packet,
    u16 length
)
{
    return checksum_finish(checksum_sum(
        transport_sum(source, destination, protocol, length),
        packet, length));
}

static int vm_send_frame(void *context, const u8 *frame, usize length)
{
    net_vm_endpoint *endpoint = (net_vm_endpoint *)context;
    if (endpoint == 0 || endpoint->link == 0 || frame == 0
        || length < NET_ETHERNET_HEADER || length > NET_FRAME_MAX
        || endpoint->endpoint > 1u) {
        return 0;
    }
    net_frame_queue *queue = &endpoint->link->receive[endpoint->endpoint ^ 1u];
    if (queue->count == NET_QUEUE_DEPTH) {
        return 0;
    }
    u32 slot = (queue->head + queue->count) % NET_QUEUE_DEPTH;
    copy_bytes(queue->frames[slot], frame, length);
    queue->lengths[slot] = (u16)length;
    ++queue->count;
    return 1;
}

static int vm_receive_frame(
    void *context,
    u8 *frame,
    usize capacity,
    usize *length
)
{
    net_vm_endpoint *endpoint = (net_vm_endpoint *)context;
    if (endpoint == 0 || endpoint->link == 0 || frame == 0
        || length == 0 || endpoint->endpoint > 1u) {
        return -1;
    }
    net_frame_queue *queue = &endpoint->link->receive[endpoint->endpoint];
    if (queue->count == 0) {
        return 0;
    }
    usize frame_length = queue->lengths[queue->head];
    if (frame_length > capacity) {
        return -1;
    }
    copy_bytes(frame, queue->frames[queue->head], frame_length);
    *length = frame_length;
    queue->head = (queue->head + 1u) % NET_QUEUE_DEPTH;
    --queue->count;
    return 1;
}

static int send_ipv4(
    net_stack *stack,
    const u8 destination_mac[6],
    u32 source_ip,
    u32 destination_ip,
    u8 protocol,
    const u8 *payload,
    usize payload_length
)
{
    if (stack == 0 || destination_mac == 0
        || payload_length > NET_FRAME_MAX - NET_ETHERNET_HEADER - NET_IPV4_HEADER
        || (payload_length != 0 && payload == 0)) {
        return 0;
    }
    u8 frame[NET_FRAME_MAX];
    usize frame_length = NET_ETHERNET_HEADER + NET_IPV4_HEADER + payload_length;
    zero_bytes(frame, NET_ETHERNET_HEADER + NET_IPV4_HEADER);
    copy_bytes(frame, destination_mac, 6);
    copy_bytes(frame + 6, stack->mac, 6);
    write_be16(frame + 12, NET_ETHERTYPE_IPV4);
    u8 *ip = frame + NET_ETHERNET_HEADER;
    ip[0] = 0x45;
    write_be16(ip + 2, (u16)(NET_IPV4_HEADER + payload_length));
    write_be16(ip + 4, (u16)stack->next_ip_id++);
    write_be16(ip + 6, 0x4000u);
    ip[8] = 64;
    ip[9] = protocol;
    write_be32(ip + 12, source_ip);
    write_be32(ip + 16, destination_ip);
    write_be16(ip + 10, internet_checksum(ip, NET_IPV4_HEADER));
    if (payload_length != 0) {
        copy_bytes(ip + NET_IPV4_HEADER, payload, payload_length);
    }
    if (!stack->device.send_frame(
            stack->device.context, frame, frame_length)) {
        return 0;
    }
    ++stack->tx_frames;
    return 1;
}

static int find_neighbor(const net_stack *stack, u32 address, u8 mac[6])
{
    if (address == stack->neighbor_ip && address != 0) {
        copy_bytes(mac, stack->neighbor_mac, 6);
        return 1;
    }
    return 0;
}

static int send_udp(
    net_stack *stack,
    u32 source_ip,
    u32 destination_ip,
    u16 source_port,
    u16 destination_port,
    const u8 *payload,
    usize payload_length
)
{
    if (payload_length > NET_UDP_PAYLOAD_MAX
        || (payload_length != 0 && payload == 0)) {
        return 0;
    }
    u8 packet[NET_UDP_HEADER + NET_UDP_PAYLOAD_MAX];
    usize length = NET_UDP_HEADER + payload_length;
    zero_bytes(packet, length);
    write_be16(packet, source_port);
    write_be16(packet + 2, destination_port);
    write_be16(packet + 4, (u16)length);
    if (payload_length != 0) {
        copy_bytes(packet + NET_UDP_HEADER, payload, payload_length);
    }
    u16 checksum = transport_checksum(
        source_ip, destination_ip, NET_PROTOCOL_UDP, packet, (u16)length);
    write_be16(packet + 6, checksum == 0 ? 0xffffu : checksum);
    u8 destination_mac[6];
    if (destination_ip == NET_IP_BROADCAST) {
        copy_bytes(destination_mac, g_broadcast_mac, 6);
    } else if (!find_neighbor(stack, destination_ip, destination_mac)) {
        return 0;
    }
    return send_ipv4(
        stack, destination_mac, source_ip, destination_ip,
        NET_PROTOCOL_UDP, packet, length);
}

static void learn_neighbor(net_stack *stack, u32 address, const u8 mac[6])
{
    if (address != 0 && address != NET_IP_BROADCAST
        && (mac[0] & 1u) == 0
        && (stack->neighbor_ip == 0 || stack->neighbor_ip == address)) {
        stack->neighbor_ip = address;
        copy_bytes(stack->neighbor_mac, mac, 6);
    }
}

static int send_arp(
    net_stack *stack,
    u16 operation,
    const u8 destination_mac[6],
    u32 target_ip,
    const u8 target_mac[6]
)
{
    u8 frame[NET_ETHERNET_HEADER + 28u];
    zero_bytes(frame, sizeof(frame));
    copy_bytes(frame, destination_mac, 6);
    copy_bytes(frame + 6, stack->mac, 6);
    write_be16(frame + 12, NET_ETHERTYPE_ARP);
    u8 *arp = frame + NET_ETHERNET_HEADER;
    write_be16(arp, 1);
    write_be16(arp + 2, NET_ETHERTYPE_IPV4);
    arp[4] = 6;
    arp[5] = 4;
    write_be16(arp + 6, operation);
    copy_bytes(arp + 8, stack->mac, 6);
    write_be32(arp + 14, stack->ip);
    if (target_mac != 0) {
        copy_bytes(arp + 18, target_mac, 6);
    }
    write_be32(arp + 24, target_ip);
    if (!stack->device.send_frame(
            stack->device.context, frame, sizeof(frame))) {
        return 0;
    }
    ++stack->tx_frames;
    return 1;
}

static int send_tcp(
    net_stack *stack,
    u32 source_ip,
    u32 destination_ip,
    u16 source_port,
    u16 destination_port,
    u32 sequence,
    u32 acknowledgment,
    u8 flags
)
{
    u8 packet[NET_TCP_HEADER];
    zero_bytes(packet, sizeof(packet));
    write_be16(packet, source_port);
    write_be16(packet + 2, destination_port);
    write_be32(packet + 4, sequence);
    write_be32(packet + 8, acknowledgment);
    packet[12] = 0x50;
    packet[13] = flags;
    write_be16(packet + 14, 4096);
    write_be16(packet + 16, transport_checksum(
        source_ip, destination_ip, NET_PROTOCOL_TCP,
        packet, sizeof(packet)));
    u8 destination_mac[6];
    if (!find_neighbor(stack, destination_ip, destination_mac)) {
        return 0;
    }
    return send_ipv4(
        stack, destination_mac, source_ip, destination_ip,
        NET_PROTOCOL_TCP, packet, sizeof(packet));
}

static int dhcp_append_option(
    u8 *packet,
    usize capacity,
    usize *length,
    u8 code,
    const u8 *value,
    u8 value_length
)
{
    if (*length + 2u + value_length + 1u > capacity) {
        return 0;
    }
    packet[(*length)++] = code;
    packet[(*length)++] = value_length;
    copy_bytes(packet + *length, value, value_length);
    *length += value_length;
    return 1;
}

static int send_dhcp_message(net_stack *stack, u8 message_type)
{
    u8 packet[300];
    zero_bytes(packet, sizeof(packet));
    packet[0] = 1;
    packet[1] = 1;
    packet[2] = 6;
    write_be32(packet + 4, stack->dhcp_xid);
    write_be16(packet + 10, 0x8000u);
    copy_bytes(packet + 28, stack->mac, 6);
    packet[236] = 99;
    packet[237] = 130;
    packet[238] = 83;
    packet[239] = 99;
    usize length = 240;
    if (!dhcp_append_option(
            packet, sizeof(packet), &length, 53, &message_type, 1)) {
        return 0;
    }
    if (message_type == NET_DHCP_REQUEST) {
        u8 value[4];
        write_be32(value, stack->dhcp_offered_ip);
        if (!dhcp_append_option(
                packet, sizeof(packet), &length, 50, value, 4)) {
            return 0;
        }
        write_be32(value, stack->dhcp_server_id);
        if (!dhcp_append_option(
                packet, sizeof(packet), &length, 54, value, 4)) {
            return 0;
        }
    }
    if (length + 1u > sizeof(packet)) {
        return 0;
    }
    packet[length++] = 255;
    return send_udp(
        stack, 0, NET_IP_BROADCAST, NET_DHCP_CLIENT_PORT,
        NET_DHCP_SERVER_PORT, packet, length);
}

typedef struct {
    u8 type;
    u32 server;
    u32 mask;
    u32 gateway;
    u32 dns;
} dhcp_options;

static int parse_dhcp_options(
    const u8 *packet,
    usize length,
    dhcp_options *options
)
{
    if (length < 240 || packet[236] != 99 || packet[237] != 130
        || packet[238] != 83 || packet[239] != 99) {
        return 0;
    }
    zero_bytes(options, sizeof(*options));
    usize offset = 240;
    while (offset < length) {
        u8 code = packet[offset++];
        if (code == 255) {
            return options->type != 0;
        }
        if (code == 0) {
            continue;
        }
        if (offset >= length) {
            return 0;
        }
        u8 option_length = packet[offset++];
        if (option_length > length - offset) {
            return 0;
        }
        if (code == 53 && option_length == 1) {
            options->type = packet[offset];
        } else if (code == 54 && option_length == 4) {
            options->server = read_be32(packet + offset);
        } else if (code == 1 && option_length == 4) {
            options->mask = read_be32(packet + offset);
        } else if (code == 3 && option_length >= 4) {
            options->gateway = read_be32(packet + offset);
        } else if (code == 6 && option_length >= 4) {
            options->dns = read_be32(packet + offset);
        }
        offset += option_length;
    }
    return 0;
}

static void process_dhcp(
    net_stack *stack,
    u32 source_ip,
    const u8 *packet,
    usize length
)
{
    dhcp_options options;
    if (length < 240 || packet[0] != 2 || packet[1] != 1
        || packet[2] != 6 || read_be32(packet + 4) != stack->dhcp_xid
        || !bytes_equal(packet + 28, stack->mac, 6)
        || !parse_dhcp_options(packet, length, &options)) {
        ++stack->invalid_frames;
        return;
    }
    if (options.type == NET_DHCP_OFFER
        && stack->dhcp_state == 1u && read_be32(packet + 16) != 0
        && options.server != 0 && options.server == source_ip) {
        stack->dhcp_offered_ip = read_be32(packet + 16);
        stack->dhcp_server_id = options.server;
        stack->dhcp_state = 2u;
        if (!send_dhcp_message(stack, NET_DHCP_REQUEST)) {
            ++stack->invalid_frames;
        }
        return;
    }
    if (options.type == NET_DHCP_ACK
        && stack->dhcp_state == 2u
        && options.server == stack->dhcp_server_id
        && options.server == source_ip
        && read_be32(packet + 16) == stack->dhcp_offered_ip
        && options.mask != 0 && options.gateway != 0 && options.dns != 0) {
        stack->ip = stack->dhcp_offered_ip;
        stack->netmask = options.mask;
        stack->gateway = options.gateway;
        stack->dns_server = options.dns;
        stack->dhcp_state = 4u;
        return;
    }
    ++stack->invalid_frames;
}

static int start_dhcp(net_stack *stack, u32 transaction_id)
{
    if (stack == 0 || stack->dhcp_state != 0 || transaction_id == 0) {
        return 0;
    }
    stack->dhcp_xid = transaction_id;
    stack->dhcp_state = 1u;
    if (!send_dhcp_message(stack, NET_DHCP_DISCOVER)) {
        stack->dhcp_state = 0;
        return 0;
    }
    return 1;
}

int os_network_stack_start_dhcp(
    os_network_stack *stack,
    u32 transaction_id
)
{
    if (stack == (os_network_stack *)0 || !stack->ready) {
        return 0;
    }
    return start_dhcp(stack, transaction_id);
}

static int dns_name_equal(const char *left, const char *right)
{
    for (usize i = 0; i < NET_DNS_NAME_MAX; ++i) {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
        if (a == '\0') {
            return 1;
        }
    }
    return 0;
}

static int dns_encode_name(const char *name, u8 *output, usize capacity, usize *length)
{
    usize start = 0;
    usize used = 0;
    usize name_length = 0;
    while (name[name_length] != '\0' && name_length < NET_DNS_NAME_MAX) {
        ++name_length;
    }
    if (name_length == 0 || name_length >= NET_DNS_NAME_MAX) {
        return 0;
    }
    while (start < name_length) {
        usize end = start;
        while (end < name_length && name[end] != '.') {
            ++end;
        }
        usize label_length = end - start;
        if (label_length == 0 || label_length > 63
            || used + label_length + 1u >= capacity) {
            return 0;
        }
        output[used++] = (u8)label_length;
        for (usize i = start; i < end; ++i) {
            char value = name[i];
            if (value >= 'A' && value <= 'Z') {
                value = (char)(value - 'A' + 'a');
            }
            output[used++] = (u8)value;
        }
        start = end + 1u;
    }
    if (used >= capacity) {
        return 0;
    }
    output[used++] = 0;
    *length = used;
    return 1;
}

static int start_dns_query(net_stack *stack, const char *name, u16 id)
{
    if (stack == 0 || stack->ip == 0 || stack->dns_server == 0
        || stack->dns_state != 0 || id == 0) {
        return 0;
    }
    u8 packet[256];
    zero_bytes(packet, sizeof(packet));
    write_be16(packet, id);
    write_be16(packet + 2, 0x0100u);
    write_be16(packet + 4, 1);
    usize name_length = 0;
    if (!dns_encode_name(name, packet + 12, sizeof(packet) - 16u,
            &name_length)) {
        return 0;
    }
    write_be16(packet + 12u + name_length, 1);
    write_be16(packet + 14u + name_length, 1);
    usize length = 16u + name_length;
    stack->dns_id = id;
    usize i = 0;
    while (name[i] != '\0') {
        stack->dns_name[i] = name[i];
        ++i;
    }
    stack->dns_name[i] = '\0';
    stack->dns_state = 1u;
    if (!send_udp(
            stack, stack->ip, stack->dns_server, NET_DNS_LOCAL_PORT,
            NET_DNS_PORT, packet, length)) {
        stack->dns_state = 0;
        return 0;
    }
    return 1;
}

static int dns_skip_name(const u8 *packet, usize length, usize *offset)
{
    usize position = *offset;
    usize traversed = 0;
    while (position < length && traversed < NET_DNS_NAME_MAX) {
        u8 label = packet[position++];
        ++traversed;
        if (label == 0) {
            *offset = position;
            return 1;
        }
        if ((label & 0xc0u) == 0xc0u) {
            if (position >= length) {
                return 0;
            }
            *offset = position + 1u;
            return 1;
        }
        if ((label & 0xc0u) != 0 || label > 63
            || label > length - position
            || traversed + label > NET_DNS_NAME_MAX) {
            return 0;
        }
        position += label;
        traversed += label;
    }
    return 0;
}

static int dns_question_matches(
    const net_stack *stack,
    const u8 *packet,
    usize length
)
{
    char name[NET_DNS_NAME_MAX];
    usize used = 0;
    usize offset = 12;
    while (offset < length) {
        u8 label_length = packet[offset++];
        if (label_length == 0) {
            if (used == 0) {
                return 0;
            }
            name[used] = '\0';
            return dns_name_equal(name, stack->dns_name);
        }
        if ((label_length & 0xc0u) != 0 || label_length > 63
            || label_length > length - offset
            || used + label_length + (used != 0) >= sizeof(name)) {
            return 0;
        }
        if (used != 0) {
            name[used++] = '.';
        }
        for (usize i = 0; i < label_length; ++i) {
            char value = (char)packet[offset++];
            if (value >= 'A' && value <= 'Z') {
                value = (char)(value - 'A' + 'a');
            }
            name[used++] = value;
        }
    }
    return 0;
}

static void process_dns(
    net_stack *stack,
    const u8 *packet,
    usize length
)
{
    if (stack->dns_state != 1u || length < 12
        || read_be16(packet) != stack->dns_id
        || (read_be16(packet + 2) & 0x8000u) == 0
        || (read_be16(packet + 2) & 0x000fu) != 0
        || read_be16(packet + 4) != 1
        || read_be16(packet + 6) == 0) {
        ++stack->invalid_frames;
        return;
    }
    usize offset = 12;
    if (!dns_question_matches(stack, packet, length)
        || !dns_skip_name(packet, length, &offset)
        || offset + 4u > length
        || read_be16(packet + offset) != 1
        || read_be16(packet + offset + 2u) != 1) {
        ++stack->invalid_frames;
        return;
    }
    offset += 4u;
    if (!dns_skip_name(packet, length, &offset)
        || offset + 10u > length) {
        ++stack->invalid_frames;
        return;
    }
    u16 type = read_be16(packet + offset);
    u16 class_code = read_be16(packet + offset + 2u);
    u16 data_length = read_be16(packet + offset + 8u);
    offset += 10u;
    if (type != 1 || class_code != 1 || data_length != 4
        || offset + data_length > length) {
        ++stack->invalid_frames;
        return;
    }
    stack->dns_result = read_be32(packet + offset);
    stack->dns_state = 2u;
}

static int dns_answer_name(
    const char *query,
    u8 *response,
    usize capacity,
    usize *length,
    u32 address
)
{
    usize query_length = 0;
    if (!dns_encode_name(query, response + 12, capacity - 28u, &query_length)) {
        return 0;
    }
    usize offset = 12u + query_length;
    write_be16(response + offset, 1);
    write_be16(response + offset + 2u, 1);
    offset += 4u;
    response[offset++] = 0xc0;
    response[offset++] = 0x0c;
    write_be16(response + offset, 1);
    write_be16(response + offset + 2u, 1);
    offset += 4u;
    write_be32(response + offset, 30);
    offset += 4u;
    write_be16(response + offset, 4);
    offset += 2u;
    write_be32(response + offset, address);
    offset += 4u;
    *length = offset;
    return 1;
}

static void process_tcp(
    net_stack *stack,
    u32 source_ip,
    const u8 *packet,
    usize length
)
{
    if (length < NET_TCP_HEADER) {
        ++stack->invalid_frames;
        return;
    }
    usize header_length = (usize)(packet[12] >> 4) * 4u;
    if (header_length < NET_TCP_HEADER || header_length > length
        || transport_checksum(
            source_ip, stack->ip, NET_PROTOCOL_TCP, packet, (u16)length) != 0) {
        ++stack->invalid_frames;
        return;
    }
    u16 source_port = read_be16(packet);
    u16 destination_port = read_be16(packet + 2);
    u32 sequence = read_be32(packet + 4);
    u32 acknowledgment = read_be32(packet + 8);
    u8 flags = packet[13];
    stack->last_tcp_source_port = source_port;
    stack->last_tcp_destination_port = destination_port;
    stack->last_tcp_sequence = sequence;
    stack->last_tcp_acknowledgment = acknowledgment;
    stack->last_tcp_flags = flags;
    ++stack->tcp_received;
    if (stack->tcp_state == NET_TCP_SYN_SENT
        && source_ip == stack->tcp_remote_ip
        && source_port == stack->tcp_remote_port
        && destination_port == stack->tcp_local_port
        && (flags & 0x12u) == 0x12u
        && acknowledgment == stack->tcp_sequence + 1u) {
        stack->tcp_receive_sequence = sequence + 1u;
        ++stack->tcp_sequence;
        stack->tcp_state = NET_TCP_ESTABLISHED;
        if (!send_tcp(
                stack, stack->ip, source_ip,
                stack->tcp_local_port, source_port,
                stack->tcp_sequence, stack->tcp_receive_sequence, 0x10u)) {
            stack->tcp_state = NET_TCP_CLOSED;
            ++stack->invalid_frames;
        }
    }
}

static int send_icmp_echo(
    net_stack *stack,
    u32 source_ip,
    u32 destination_ip,
    u8 type,
    const u8 *payload,
    usize length
)
{
    if (length < 8 || length > NET_FRAME_MAX
            - NET_ETHERNET_HEADER - NET_IPV4_HEADER
        || payload == 0 || length > 8u + NET_UDP_PAYLOAD_MAX) {
        return 0;
    }
    u8 packet[8u + NET_UDP_PAYLOAD_MAX];
    copy_bytes(packet, payload, length);
    packet[0] = type;
    packet[1] = 0;
    write_be16(packet + 2, 0);
    write_be16(packet + 2, internet_checksum(packet, length));
    u8 destination_mac[6];
    if (!find_neighbor(stack, destination_ip, destination_mac)) {
        return 0;
    }
    return send_ipv4(
        stack, destination_mac, source_ip, destination_ip,
        NET_PROTOCOL_ICMP, packet, length);
}

static void process_icmp(
    net_stack *stack,
    u32 source_ip,
    const u8 *packet,
    usize length
)
{
    if (length < 8 || internet_checksum(packet, length) != 0) {
        ++stack->invalid_frames;
        return;
    }
    ++stack->icmp_received;
    stack->last_icmp_type = packet[0];
    if (packet[0] == 8 && packet[1] == 0) {
        if (!send_icmp_echo(
                stack, stack->ip, source_ip, 0, packet, length)) {
            ++stack->invalid_frames;
        }
    } else if (packet[0] == 0 && packet[1] == 0) {
        ++stack->icmp_echo_replies;
    }
}

static void process_udp(
    net_stack *stack,
    u32 source_ip,
    u32 destination_ip,
    const u8 *packet,
    usize length
)
{
    if (length < NET_UDP_HEADER) {
        ++stack->invalid_frames;
        return;
    }
    u16 udp_length = read_be16(packet + 4);
    u16 checksum = read_be16(packet + 6);
    if (udp_length < NET_UDP_HEADER || udp_length > length
        || (checksum != 0 && transport_checksum(
            source_ip, destination_ip, NET_PROTOCOL_UDP,
            packet, udp_length) != 0)) {
        ++stack->invalid_frames;
        return;
    }
    ++stack->udp_received;
    u16 source_port = read_be16(packet);
    u16 destination_port = read_be16(packet + 2);
    usize payload_length = udp_length - NET_UDP_HEADER;
    const u8 *payload = packet + NET_UDP_HEADER;
    stack->last_udp_source_port = source_port;
    stack->last_udp_destination_port = destination_port;
    stack->last_udp_length = payload_length <= NET_UDP_PAYLOAD_MAX
        ? (u16)payload_length : 0;
    if (stack->last_udp_length != 0) {
        copy_bytes(
            stack->last_udp_payload, payload, stack->last_udp_length);
    }
    if (destination_port == NET_DHCP_CLIENT_PORT) {
        process_dhcp(stack, source_ip, payload, payload_length);
    } else if (destination_port == NET_DNS_LOCAL_PORT) {
        if (source_port != NET_DNS_PORT || source_ip != stack->dns_server) {
            ++stack->invalid_frames;
        } else {
            process_dns(stack, payload, payload_length);
        }
    }
}

static int process_arp(net_stack *stack, const u8 *frame, usize length)
{
    if (length < NET_ETHERNET_HEADER + 28u) {
        ++stack->invalid_frames;
        return 0;
    }
    const u8 *arp = frame + NET_ETHERNET_HEADER;
    u16 operation = read_be16(arp + 6);
    if (read_be16(arp) != 1 || read_be16(arp + 2) != NET_ETHERTYPE_IPV4
        || arp[4] != 6 || arp[5] != 4
        || (operation != 1 && operation != 2)) {
        ++stack->invalid_frames;
        return 0;
    }
    u32 sender_ip = read_be32(arp + 14);
    u32 target_ip = read_be32(arp + 24);
    learn_neighbor(stack, sender_ip, arp + 8);
    ++stack->arp_received;
    stack->last_arp_operation = operation;
    if (operation == 1 && target_ip == stack->ip && stack->ip != 0) {
        if (!send_arp(stack, 2, arp + 8, sender_ip, arp + 8)) {
            ++stack->invalid_frames;
            return 0;
        }
    }
    return 1;
}

static int process_ipv4(net_stack *stack, const u8 *frame, usize length)
{
    if (length < NET_ETHERNET_HEADER + NET_IPV4_HEADER) {
        ++stack->invalid_frames;
        return 0;
    }
    const u8 *ip = frame + NET_ETHERNET_HEADER;
    usize header_length = (usize)(ip[0] & 0x0fu) * 4u;
    u16 total_length = read_be16(ip + 2);
    u16 fragments = read_be16(ip + 6);
    if ((ip[0] >> 4) != 4 || header_length < NET_IPV4_HEADER
        || header_length > length - NET_ETHERNET_HEADER
        || total_length < header_length
        || total_length > length - NET_ETHERNET_HEADER
        || (fragments & 0x3fffu) != 0
        || internet_checksum(ip, header_length) != 0) {
        ++stack->invalid_frames;
        return 0;
    }
    u32 source_ip = read_be32(ip + 12);
    u32 destination_ip = read_be32(ip + 16);
    if (destination_ip != stack->ip
        && destination_ip != NET_IP_BROADCAST) {
        return 1;
    }
    learn_neighbor(stack, source_ip, frame + 6);
    const u8 *payload = ip + header_length;
    usize payload_length = total_length - header_length;
    if (ip[9] == NET_PROTOCOL_UDP) {
        process_udp(stack, source_ip, destination_ip, payload, payload_length);
    } else if (ip[9] == NET_PROTOCOL_TCP) {
        process_tcp(stack, source_ip, payload, payload_length);
    } else if (ip[9] == NET_PROTOCOL_ICMP) {
        process_icmp(stack, source_ip, payload, payload_length);
    }
    return 1;
}

int os_network_stack_poll(os_network_stack *stack)
{
    if (stack == (os_network_stack *)0 || !stack->ready
        || stack->device.send_frame == (os_network_send_frame)0
        || stack->device.receive_frame == (os_network_receive_frame)0) {
        return -1;
    }
    int processed = 0;
    u8 frame[NET_FRAME_MAX];
    for (u32 i = 0; i < NET_QUEUE_DEPTH; ++i) {
        usize length = 0;
        int result = stack->device.receive_frame(
            stack->device.context, frame, sizeof(frame), &length);
        if (result == 0) {
            break;
        }
        if (result < 0) {
            ++stack->invalid_frames;
            break;
        }
        ++stack->rx_frames;
        ++processed;
        if (length < NET_ETHERNET_HEADER) {
            ++stack->invalid_frames;
            continue;
        }
        u16 type = read_be16(frame + 12);
        if (type == NET_ETHERTYPE_IPV4) {
            (void)process_ipv4(stack, frame, length);
        } else if (type == NET_ETHERTYPE_ARP) {
            (void)process_arp(stack, frame, length);
        }
    }
    return processed;
}

static int poll_stack(net_stack *stack)
{
    return os_network_stack_poll(stack);
}

static int build_dhcp_response(
    const net_stack *peer,
    u8 message_type,
    u8 *packet,
    usize capacity,
    usize *length
)
{
    if (capacity < 260) {
        return 0;
    }
    zero_bytes(packet, capacity);
    packet[0] = 2;
    packet[1] = 1;
    packet[2] = 6;
    write_be32(packet + 4, 0x1234abcdu);
    write_be32(packet + 16, 0x0a000002u);
    write_be32(packet + 20, peer->ip);
    copy_bytes(packet + 28, g_client.mac, 6);
    packet[236] = 99;
    packet[237] = 130;
    packet[238] = 83;
    packet[239] = 99;
    usize used = 240;
    u8 value[4];
    if (!dhcp_append_option(packet, capacity, &used, 53, &message_type, 1)) {
        return 0;
    }
    write_be32(value, peer->ip);
    if (!dhcp_append_option(packet, capacity, &used, 54, value, 4)) {
        return 0;
    }
    write_be32(value, 0xffffff00u);
    if (!dhcp_append_option(packet, capacity, &used, 1, value, 4)) {
        return 0;
    }
    write_be32(value, peer->ip);
    if (!dhcp_append_option(packet, capacity, &used, 3, value, 4)
        || !dhcp_append_option(packet, capacity, &used, 6, value, 4)) {
        return 0;
    }
    if (used >= capacity) {
        return 0;
    }
    packet[used++] = 255;
    *length = used;
    return 1;
}

static int send_arp_request(net_stack *stack, u32 target_ip)
{
    return send_arp(stack, 1, g_broadcast_mac, target_ip, 0);
}

static int send_icmp_request(net_stack *stack, u32 destination_ip)
{
    u8 packet[12];
    zero_bytes(packet, sizeof(packet));
    packet[0] = 8;
    write_be16(packet + 4, 0x1234u);
    write_be16(packet + 6, 1);
    packet[8] = 'p';
    packet[9] = 'i';
    packet[10] = 'n';
    packet[11] = 'g';
    write_be16(packet + 2, internet_checksum(packet, sizeof(packet)));
    u8 mac[6];
    if (!find_neighbor(stack, destination_ip, mac)) {
        return 0;
    }
    return send_ipv4(
        stack, mac, stack->ip, destination_ip,
        NET_PROTOCOL_ICMP, packet, sizeof(packet));
}

static int network_mac_valid(const u8 mac[6])
{
    u8 any_nonzero = 0u;
    for (usize i = 0; i < 6u; ++i) {
        any_nonzero |= mac[i];
    }
    return any_nonzero != 0u && (mac[0] & 1u) == 0u;
}

int os_network_stack_init(
    os_network_stack *stack,
    const os_network_device *device,
    const u8 mac[6],
    u32 initial_ipv4_address
)
{
    if (stack == (os_network_stack *)0
        || device == (const os_network_device *)0
        || device->send_frame == (os_network_send_frame)0
        || device->receive_frame == (os_network_receive_frame)0
        || mac == (const u8 *)0 || !network_mac_valid(mac)) {
        return 0;
    }

    os_network_device selected_device = *device;
    u8 selected_mac[6];
    copy_bytes(selected_mac, mac, sizeof(selected_mac));
    zero_bytes(stack, sizeof(*stack));
    stack->device = selected_device;
    copy_bytes(stack->mac, selected_mac, sizeof(stack->mac));
    stack->ip = initial_ipv4_address;
    stack->ready = 1u;
    return 1;
}

static int init_stack(
    net_stack *stack,
    net_vm_endpoint *endpoint,
    const u8 mac[6],
    u32 address
)
{
    os_network_device device = {
        .context = endpoint,
        .send_frame = vm_send_frame,
        .receive_frame = vm_receive_frame,
    };
    return os_network_stack_init(stack, &device, mac, address);
}

int os_network_self_test(void)
{
    static const u8 client_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x07, 0x01};
    static const u8 peer_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x07, 0x02};
    static const u8 udp_message[] = "bounded UDP test";
    static const char dns_name[] = "node.experimentalos.test";
    static u8 packet[320];
    g_network_test_failure = 0;
    zero_bytes(&g_vm_link, sizeof(g_vm_link));
    g_endpoints[0].link = &g_vm_link;
    g_endpoints[0].endpoint = 0;
    g_endpoints[1].link = &g_vm_link;
    g_endpoints[1].endpoint = 1;
    init_stack(&g_client, &g_endpoints[0], client_mac, 0);
    init_stack(&g_peer, &g_endpoints[1], peer_mac, 0x0a000001u);
    {
        static const u8 invalid_mac[6] = {0, 0, 0, 0, 0, 0};
        os_network_stack rejected_stack;
        zero_bytes(&rejected_stack, sizeof(rejected_stack));
        NETWORK_TEST_REQUIRE(os_network_stack_init(
                &rejected_stack, &g_client.device, invalid_mac, 0)
            || rejected_stack.ready, 14);
        NETWORK_TEST_REQUIRE(
            os_network_stack_poll((os_network_stack *)0) != -1
                || os_network_stack_start_dhcp(
                    (os_network_stack *)0, 0x1234abcdu), 15);
    }
    {
        os_network_stack alias_stack;
        zero_bytes(&alias_stack, sizeof(alias_stack));
        alias_stack.device = g_client.device;
        copy_bytes(alias_stack.mac, g_client.mac, sizeof(alias_stack.mac));
        NETWORK_TEST_REQUIRE(!os_network_stack_init(
                &alias_stack, &alias_stack.device, alias_stack.mac, 0)
            || !alias_stack.ready
            || alias_stack.device.send_frame != vm_send_frame
            || alias_stack.device.receive_frame != vm_receive_frame
            || !bytes_equal(alias_stack.mac, client_mac, sizeof(client_mac)),
            16);
    }

    NETWORK_TEST_REQUIRE(!send_udp(
            &g_peer, g_peer.ip, NET_IP_BROADCAST, 9000, 9001,
            udp_message, sizeof(udp_message)), 1);
    net_frame_queue *client_queue = &g_vm_link.receive[0];
    NETWORK_TEST_REQUIRE(client_queue->count != 1, 2);
    u8 *bad_ip = client_queue->frames[client_queue->head]
        + NET_ETHERNET_HEADER;
    bad_ip[10] ^= 0x80u;
    NETWORK_TEST_REQUIRE(
        !(os_network_stack_poll(&g_client) == 1
            && g_client.invalid_frames == 1), 3);

    NETWORK_TEST_REQUIRE(!os_network_stack_start_dhcp(
            &g_client, 0x1234abcdu)
        || poll_stack(&g_peer) != 1
        || g_peer.last_udp_source_port != NET_DHCP_CLIENT_PORT
        || g_peer.last_udp_destination_port != NET_DHCP_SERVER_PORT
        || g_peer.last_udp_length < 240
        || g_peer.last_udp_payload[240] != 53
        || g_peer.last_udp_payload[242] != NET_DHCP_DISCOVER, 4);
    usize packet_length = 0;
    NETWORK_TEST_REQUIRE(!build_dhcp_response(
            &g_peer, NET_DHCP_OFFER, packet, sizeof(packet), &packet_length)
        || !send_udp(
            &g_peer, g_peer.ip, NET_IP_BROADCAST,
            NET_DHCP_SERVER_PORT, NET_DHCP_CLIENT_PORT,
            packet, packet_length)
        || poll_stack(&g_client) != 1
        || g_client.dhcp_state != 2u
        || poll_stack(&g_peer) != 1
        || g_peer.last_udp_payload[242] != NET_DHCP_REQUEST, 5);
    NETWORK_TEST_REQUIRE(!build_dhcp_response(
            &g_peer, NET_DHCP_ACK, packet, sizeof(packet), &packet_length)
        || !send_udp(
            &g_peer, g_peer.ip, NET_IP_BROADCAST,
            NET_DHCP_SERVER_PORT, NET_DHCP_CLIENT_PORT,
            packet, packet_length)
        || poll_stack(&g_client) != 1
        || g_client.dhcp_state != 4u
        || g_client.ip != 0x0a000002u
        || g_client.netmask != 0xffffff00u
        || g_client.gateway != g_peer.ip
        || g_client.dns_server != g_peer.ip, 6);

    NETWORK_TEST_REQUIRE(!send_udp(
            &g_client, g_client.ip, g_peer.ip, 40000, 7,
            udp_message, sizeof(udp_message))
        || poll_stack(&g_peer) != 1
        || g_peer.last_udp_source_port != 40000
        || g_peer.last_udp_destination_port != 7
        || g_peer.last_udp_length != sizeof(udp_message)
        || !bytes_equal(
            g_peer.last_udp_payload, udp_message, sizeof(udp_message))
        || !send_udp(
            &g_peer, g_peer.ip, g_client.ip, 7, 40000,
            g_peer.last_udp_payload, g_peer.last_udp_length)
        || poll_stack(&g_client) != 1
        || g_client.last_udp_source_port != 7
        || g_client.last_udp_destination_port != 40000
        || !bytes_equal(
            g_client.last_udp_payload, udp_message, sizeof(udp_message)), 7);

    NETWORK_TEST_REQUIRE(!start_dns_query(&g_client, dns_name, 0x5a51u)
        || poll_stack(&g_peer) != 1
        || g_peer.last_udp_source_port != NET_DNS_LOCAL_PORT
        || g_peer.last_udp_destination_port != NET_DNS_PORT
        || g_peer.last_udp_length < 12
        || read_be16(g_peer.last_udp_payload) != 0x5a51u
        || (read_be16(g_peer.last_udp_payload + 2) & 0x0100u) == 0, 8);
    zero_bytes(packet, sizeof(packet));
    write_be16(packet, 0x5a51u);
    write_be16(packet + 2, 0x8180u);
    write_be16(packet + 4, 1);
    write_be16(packet + 6, 1);
    usize dns_length = 0;
    NETWORK_TEST_REQUIRE(!dns_answer_name(
            dns_name, packet, sizeof(packet), &dns_length, 0x0a000063u)
        || !send_udp(
            &g_peer, 0x0a000003u, g_client.ip, NET_DNS_PORT,
            NET_DNS_LOCAL_PORT, packet, dns_length)
        || poll_stack(&g_client) != 1
        || g_client.dns_state != 1u
        || g_client.invalid_frames != 2u, 9);
    NETWORK_TEST_REQUIRE(!send_udp(
            &g_peer, g_peer.ip, g_client.ip, NET_DNS_PORT,
            NET_DNS_LOCAL_PORT, packet, dns_length)
        || poll_stack(&g_client) != 1
        || g_client.dns_state != 2u
        || g_client.dns_result != 0x0a000063u, 10);

    g_client.tcp_local_port = 40001;
    g_client.tcp_remote_port = 80;
    g_client.tcp_remote_ip = g_peer.ip;
    g_client.tcp_sequence = 1000;
    g_client.tcp_state = NET_TCP_SYN_SENT;
    NETWORK_TEST_REQUIRE(!send_tcp(
            &g_client, g_client.ip, g_peer.ip,
            g_client.tcp_local_port, g_client.tcp_remote_port,
            g_client.tcp_sequence, 0, 0x02u)
        || poll_stack(&g_peer) != 1
        || g_peer.last_tcp_flags != 0x02u
        || g_peer.last_tcp_sequence != 1000
        || !send_tcp(
            &g_peer, g_peer.ip, g_client.ip,
            g_client.tcp_remote_port, g_client.tcp_local_port,
            9000, 1001, 0x12u)
        || poll_stack(&g_client) != 1
        || g_client.tcp_state != NET_TCP_ESTABLISHED
        || poll_stack(&g_peer) != 1
        || g_peer.last_tcp_flags != 0x10u
        || g_peer.last_tcp_acknowledgment != 9001, 11);

    NETWORK_TEST_REQUIRE(!send_arp_request(&g_peer, g_client.ip)
        || poll_stack(&g_client) != 1
        || g_client.last_arp_operation != 1
        || poll_stack(&g_peer) != 1
        || g_peer.last_arp_operation != 2, 12);
    NETWORK_TEST_REQUIRE(!send_icmp_request(&g_peer, g_client.ip)
        || poll_stack(&g_client) != 1
        || g_client.last_icmp_type != 8
        || poll_stack(&g_peer) != 1
        || g_peer.last_icmp_type != 0
        || g_peer.icmp_echo_replies != 1
        || g_client.invalid_frames != 2
        || g_client.rx_frames < 7
        || g_client.tx_frames < 5
        || g_peer.tcp_received < 2
        || g_peer.udp_received < 4, 13);
    return 1;
}

u32 os_network_self_test_error(void)
{
    return g_network_test_failure;
}
