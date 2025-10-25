//
// Created by Aaron Gill-Braun on 2025-09-18.
//

#include <kernel/net/eth.h>
#include <kernel/net/netdev.h>
#include <kernel/net/skbuff.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("eth: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("eth: %s: " fmt, __func__, ##__VA_ARGS__)

const uint8_t eth_broadcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

//
// MARK: Ethernet Header Operations
//

int eth_header(sk_buff_t *skb, netdev_t *dev, uint16_t proto, const uint8_t *dst_addr, const uint8_t *src_addr) {
  struct ethhdr *eth = skb_push(skb, ETH_HLEN);
  if (!eth) {
    EPRINTF("failed to add ethernet header\n");
    return -ENOMEM;
  }

  eth->h_proto = htons(proto);

  // copy source address
  if (src_addr) {
    eth_addr_copy(eth->h_source, src_addr);
  } else {
    eth_addr_copy(eth->h_source, dev->dev_addr);
  }

  // copy destination address
  if (dst_addr) {
    eth_addr_copy(eth->h_dest, dst_addr);
  } else {
    eth_addr_copy(eth->h_dest, eth_broadcast_addr);
  }

  skb_set_network_header(skb, ETH_HLEN);
  return 0;
}

//
// MARK: Ethernet Packet Reception
//

uint16_t eth_type_trans(sk_buff_t *skb, netdev_t *dev) {
  ASSERT(skb != NULL);
  ASSERT(dev != NULL);

  if (skb->len < ETH_HLEN) {
    return 0;
  }

  struct ethhdr *eth = (struct ethhdr *)skb->data;
  skb_pull(skb, ETH_HLEN);

  uint16_t protocol = ntohs(eth->h_proto);
  skb->protocol = protocol;

  if (eth_addr_equal(eth->h_dest, dev->dev_addr)) {
    skb->pkt_type = PACKET_HOST;
  } else if (eth_addr_is_broadcast(eth->h_dest)) {
    skb->pkt_type = PACKET_BROADCAST;
  } else if (eth_addr_is_multicast(eth->h_dest)) {
    skb->pkt_type = PACKET_MULTICAST;
  } else {
    skb->pkt_type = PACKET_OTHERHOST;
  }

  return protocol;
}

int eth_rcv(sk_buff_t *skb) {
  ASSERT(skb != NULL);

  struct netdev *dev = skb->dev;
  if (!dev) {
    EPRINTF("no device for received packet\n");
    skb_free(&skb);
    return -ENODEV;
  }

  if (skb->len < ETH_HLEN) {
    EPRINTF("packet too small (%zu bytes)\n", skb->len);
    dev->stats.rx_errors++;
    dev->stats.rx_dropped++;
    skb_free(&skb);
    return -EINVAL;
  }

  uint16_t protocol = eth_type_trans(skb, dev);
  if (!protocol) {
    dev->stats.rx_errors++;
    dev->stats.rx_dropped++;
    skb_free(&skb);
    return -EINVAL;
  }

  skb_set_network_header(skb, 0);

  dev->stats.rx_packets++;
  dev->stats.rx_bytes += skb->len;

  if (skb->pkt_type == PACKET_OTHERHOST) {
    DPRINTF("packet not for us\n");
    skb_free(&skb);
    return 0;
  }

  DPRINTF("received packet: proto=0x%04x, len=%zu\n", protocol, skb->len);
  return netdev_receive_skb(skb);
}

//
// MARK: Link Type Registration
//

static link_type_t eth_link_type = {
  .type = ARPHRD_ETHER,
  .func = eth_rcv,
};

void eth_init() {
  netdev_add_link_type(&eth_link_type);
  DPRINTF("Ethernet link-layer initialized\n");
}
MODULE_INIT(eth_init);
