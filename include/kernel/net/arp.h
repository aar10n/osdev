//
// Created by Aaron Gill-Braun on 2025-09-18.
//

#ifndef KERNEL_NET_ARP_H
#define KERNEL_NET_ARP_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/alarm.h>
#include <kernel/ref.h>

// temporarily undefine packed to avoid conflicts with Linux headers
#pragma push_macro("packed")
#undef packed
#include <linux/if_arp.h>
#pragma pop_macro("packed")

typedef struct netdev netdev_t;
typedef struct sk_buff sk_buff_t;
struct arp_entry;

// ARP cache entry states
#define ARP_STATE_INCOMPLETE    0   // Waiting for reply
#define ARP_STATE_REACHABLE     1   // Valid entry
#define ARP_STATE_STALE         2   // Needs revalidation
#define ARP_STATE_FAILED        3   // Resolution failed

// ARP cache timeouts (in seconds)
#define ARP_CACHE_TIMEOUT       300     // 5 minutes
#define ARP_INCOMPLETE_TIMEOUT  3       // 3 seconds for response
#define ARP_RETRY_INTERVAL      1       // 1 second between retries
#define ARP_MAX_RETRIES         3       // Maximum retries

/**
 * An ARP header.
 *
 * https://datatracker.ietf.org/doc/html/rfc826
 */
struct arp_hdr {
  uint16_t ar_hrd;      // hardware type
  uint16_t ar_pro;      // protocol type
  uint8_t ar_hln;       // hardware address length
  uint8_t ar_pln;       // protocol address length
  uint16_t ar_op;       // operation
};
static_assert(sizeof(struct arp_hdr) == 8);

// Ethernet ARP packet (follows arphdr)
struct packed arp_eth_ipv4 {
  uint8_t ar_sha[6];    // sender hardware address
  uint32_t ar_sip;      // sender ip address
  uint8_t ar_tha[6];    // target hardware address
  uint32_t ar_tip;      // target ip address
};
static_assert(sizeof(struct arp_eth_ipv4) == 20);

// Complete ARP packet for Ethernet/IPv4
struct arp_packet {
  struct arp_hdr arp;
  struct arp_eth_ipv4 eth_ipv4;
};
static_assert(sizeof(struct arp_packet) == 28);

//
// MARK: Address Resolution Protocol API
//

int arp_rcv(struct sk_buff *skb);

int arp_lookup(netdev_t *dev, uint32_t ip_addr, uint8_t *hw_addr);
int arp_resolve(netdev_t *dev, uint32_t ip_addr, sk_buff_t *skb);

__ref struct arp_entry *arp_cache_lookup(uint32_t ip_addr);
__ref struct arp_entry *arp_cache_add(netdev_t *dev, uint32_t ip_addr, uint8_t *hw_addr);
void arp_cache_update(uint32_t ip_addr, uint8_t *hw_addr);
void arp_cache_delete(uint32_t ip_addr);
void arp_cache_flush(netdev_t *dev);

int arp_send_request(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip);
int arp_send_reply(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, uint8_t *dst_hw);
int arp_send_gratuitous(netdev_t *dev, uint32_t ip_addr);

#endif // KERNEL_NET_ARP_H
