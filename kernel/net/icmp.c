//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#include <kernel/net/icmp.h>
#include <kernel/net/ip.h>
#include <kernel/net/in_dev.h>
#include <kernel/net/skbuff.h>
#include <kernel/net/netdev.h>
#include <kernel/net/raw.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <linux/in.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG icmp
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("icmp: %s: " fmt, __func__, ##__VA_ARGS__)

//
// MARK: ICMP Message Senders
//

int icmp_send_echo_reply(sk_buff_t *skb) {
  struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
  struct icmphdr *icmph = skb_transport_header(skb);

  netdev_t *dev = skb->dev;
  if (!dev) {
    DPRINTF("no device for echo reply\n");
    return -ENODEV;
  }

  struct in_ifaddr *ifa = LIST_FIRST(&dev->ip_addrs);
  if (!ifa) {
    DPRINTF("no IP address configured on {:str}\n", &dev->name);
    return -ENOENT;
  }

  size_t icmp_len = skb->len - ((uintptr_t)skb_transport_header(skb) - (uintptr_t)skb->data);
  if (icmp_len < sizeof(struct icmphdr)) {
    DPRINTF("ICMP packet too small\n");
    return -EINVAL;
  }

  struct sk_buff *reply_skb = skb_alloc(icmp_len + 64);  // extra space for headers
  if (!reply_skb) {
    DPRINTF("failed to allocate reply skb\n");
    return -ENOMEM;
  }

  uint8_t *reply_data = skb_put_data(reply_skb, icmp_len);
  memcpy(reply_data, icmph, icmp_len);

  skb_set_transport_header(reply_skb, 0);

  // modify the icmp header to be an echo reply
  struct icmphdr *reply_icmph = skb_transport_header(reply_skb);
  reply_icmph->type = ICMP_ECHOREPLY;
  reply_icmph->code = 0;
  reply_icmph->checksum = 0;

  reply_icmph->checksum = ip_checksum(reply_icmph, icmp_len);

  DPRINTF("sending echo reply: src={:ip} dst={:ip} via {:str}\n", ifa->ifa_address, ntohl(iph->saddr), &dev->name);
  reply_skb->dev = dev;

  int ret = ip_output(reply_skb, ifa->ifa_address, ntohl(iph->saddr), IPPROTO_ICMP, dev);
  skb_free(&reply_skb);
  return ret;
}

int icmp_send_dest_unreach(uint32_t dest, uint8_t code, struct sk_buff *orig_skb) {
  // find a device with an ip address to send from
  netdev_t *dev = netdev_get_by_name("lo");  // use loopback for now
  if (!dev) {
    return -ENODEV;
  }

  struct in_ifaddr *ifa = LIST_FIRST(&dev->ip_addrs);
  if (!ifa) {
    netdev_putref(&dev);
    return -ENOENT;
  }

  struct sk_buff *skb = skb_alloc(576);  // min reassembly size
  if (!skb) {
    netdev_putref(&dev);
    return -ENOMEM;
  }

  skb_push(skb, sizeof(struct iphdr));
  skb_set_network_header(skb, 0);

  // build icmp header
  struct icmphdr *icmph = skb_put_data(skb, sizeof(struct icmphdr));
  icmph->type = ICMP_DEST_UNREACH;
  icmph->code = code;
  icmph->checksum = 0;
  icmph->un.gateway = 0;  // unused for dest unreach

  // add original ip header + first 8 bytes of original data
  if (orig_skb) {
    struct iphdr *orig_iph = (struct iphdr *)skb_network_header(orig_skb);
    size_t copy_len = sizeof(struct iphdr) + 8;

    if (orig_skb->len >= sizeof(struct iphdr) + 8) {
      uint8_t *data = skb_put_data(skb, copy_len);
      memcpy(data, orig_iph, copy_len);
    }
  }

  size_t icmp_len = skb->len - sizeof(struct iphdr);
  skb_set_transport_header(skb, sizeof(struct iphdr));
  icmph->checksum = ip_checksum(icmph, icmp_len);

  skb->dev = dev;
  int ret = ip_output(skb, ifa->ifa_address, dest, IPPROTO_ICMP, dev);

  skb_free(&skb);
  netdev_putref(&dev);
  return ret;
}

//
// MARK: ICMP Protocol Handler
//

int icmp_rcv(struct sk_buff *skb) {
  ASSERT(skb != NULL);

  // validate minimum size
  if (skb->len < sizeof(struct icmphdr)) {
    EPRINTF("packet too small (%zu bytes)\n", skb->len);
    return -EINVAL;
  }

  struct icmphdr *icmph = skb_transport_header(skb);
  size_t icmp_len = skb->len - ((uintptr_t)skb_transport_header(skb) - (uintptr_t)skb->data);

  uint16_t orig_checksum = icmph->checksum;
  icmph->checksum = 0;
  uint16_t calc_checksum = ip_checksum(icmph, icmp_len);
  icmph->checksum = orig_checksum;

  if (orig_checksum != calc_checksum) {
    EPRINTF("checksum mismatch (got 0x%04x, expected 0x%04x)\n", orig_checksum, calc_checksum);
    return -EINVAL;
  }

  switch (icmph->type) {
    case ICMP_ECHO:
      DPRINTF("received echo request (id=%u, seq=%u)\n", ntohs(icmph->un.echo.id), ntohs(icmph->un.echo.sequence));
      icmp_send_echo_reply(skb);
      skb_free(&skb);
      break;
    case ICMP_ECHOREPLY:
      DPRINTF("received echo reply (id=%u, seq=%u)\n", ntohs(icmph->un.echo.id), ntohs(icmph->un.echo.sequence));
      raw_rcv(skb, IPPROTO_ICMP);
      skb_free(&skb);
      break;
    case ICMP_DEST_UNREACH:
      EPRINTF("received destination unreachable (code=%u)\n", icmph->code);
      // TODO: Notify higher layers
      skb_free(&skb);
      break;
    case ICMP_TIME_EXCEEDED:
      EPRINTF("received time exceeded (code=%u)\n", icmph->code);
      // TODO: Notify higher layers
      skb_free(&skb);
      break;
    default:
      EPRINTF("unsupported ICMP type %u\n", icmph->type);
      skb_free(&skb);
      break;
  }

  return 0;
}

//
// MARK: ICMP Protocol Registration
//

void icmp_init() {
  int ret = ip_register_protocol(IPPROTO_ICMP, icmp_rcv);
  if (ret < 0) {
    kprintf("icmp: failed to register protocol handler: %d\n", ret);
    return;
  }

  DPRINTF("ICMP protocol handler registered\n");
}
MODULE_INIT(icmp_init);
