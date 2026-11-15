#include "network.h"

#include <stdio.h>

int main(void)
{
    if (!os_network_self_test()) {
        fprintf(stderr, "FAIL G07-NETWORK: protocol test stage %u failed\n",
            os_network_self_test_error());
        return 1;
    }
    puts("PASS G07-NETWORK: VM NIC, IP, DHCP, DNS, TCP, UDP, ARP and ICMP");
    return 0;
}
