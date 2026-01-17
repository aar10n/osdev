//
// Created by Aaron Gill-Braun on 2025-09-18.
//

#ifndef KERNEL_NET_ETH_H
#define KERNEL_NET_ETH_H

#include <kernel/base.h>
#include <kernel/string.h>

// temporarily undefine packed to avoid conflicts with Linux headers
#pragma push_macro("packed")
#undef packed
#include <linux/if_ether.h>
#pragma pop_macro("packed")

typedef struct netdev netdev_t;
typedef struct sk_buff sk_buff_t;

extern const uint8_t eth_broadcast_addr[ETH_ALEN];

//
// MARK: Ethernet API
//

int eth_rcv(sk_buff_t *skb);
uint16_t eth_type_trans(sk_buff_t *skb, netdev_t *dev);
int eth_header(sk_buff_t *skb, netdev_t *dev, uint16_t proto, const uint8_t *dst_addr, const uint8_t *src_addr);

// Ethernet address utilities

static inline int eth_addr_equal(const uint8_t *addr1, const uint8_t *addr2) {
  return memcmp(addr1, addr2, ETH_ALEN) == 0;
}

static inline void eth_addr_copy(uint8_t *dst, const uint8_t *src) {
  memcpy(dst, src, ETH_ALEN);
}

static inline int eth_addr_is_zero(const uint8_t *addr) {
  return (addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]) == 0;
}

static inline int eth_addr_is_broadcast(const uint8_t *addr) {
  return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

static inline int eth_addr_is_multicast(const uint8_t *addr) {
  return addr[0] & 0x01;
}

#endif
