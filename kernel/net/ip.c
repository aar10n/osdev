//
// Created by Aaron Gill-Braun on 2025-09-18.
//

#include <kernel/net/ip.h>
#include <kernel/net/arp.h>
#include <kernel/net/eth.h>
#include <kernel/net/in_dev.h>
#include <kernel/net/skbuff.h>
#include <kernel/net/netdev.h>

#include <kernel/bits.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG ip
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("ip: %s: " fmt, __func__, ##__VA_ARGS__)

static LIST_HEAD(route_t) routing_table;
static mtx_t routing_lock;

static int (*protocol_handlers[256])(sk_buff_t *skb) = { NULL };
static mtx_t proto_handlers_lock;

static uint16_t ip_id_counter = 1;

void ip_static_init() {
  mtx_init(&routing_lock, 0, "ip_routing");
  mtx_init(&proto_handlers_lock, 0, "ip_protocols");
}
STATIC_INIT(ip_static_init);

//
// MARK: Routing Functions
//

void _route_free(route_t **routep) {
  route_t *route = moveptr(*routep);
  if (!route) {
    return;
  }

  ASSERT(read_refcount(route) == 0);
  netdev_putref(&route->dev);
  kfree(route);
}

int ip_route_add(uint32_t dest, uint32_t mask, uint32_t gateway, netdev_t *dev, int metric) {
  ASSERT(dev != NULL);
  route_t *route = kmallocz(sizeof(route_t));
  if (!route) {
    return -ENOMEM;
  }

  route->dest = dest;
  route->mask = mask;
  route->gateway = gateway;
  route->dev = netdev_getref(dev);
  route->metric = metric;
  initref(route);

  mtx_lock(&routing_lock);
  LIST_ADD(&routing_table, route, list);
  mtx_unlock(&routing_lock);

  DPRINTF("added route: {:ip}/{:ip} via {:ip} dev {:str} metric %d\n", dest, mask, gateway, &dev->name, metric);
  return 0;
}

int ip_route_del(uint32_t dest, uint32_t mask) {
  mtx_lock(&routing_lock);
  route_t *route = LIST_FIND(_route, &routing_table, list, _route->dest == dest && _route->mask == mask);
  if (!route) {
    mtx_unlock(&routing_lock);
    return -ENOENT;
  }

  LIST_REMOVE(&routing_table, route, list);
  mtx_unlock(&routing_lock);

  route_putref(&route);
  return 0;
}

__ref route_t *ip_route_lookup(uint32_t dest) {
  route_t *best_route = NULL;
  int best_metric = INT_MAX;
  int best_prefix_len = -1;

  mtx_lock(&routing_lock);

  route_t *route;
  LIST_FOREACH(route, &routing_table, list) {
    if ((dest & route->mask) == (route->dest & route->mask)) {
      int prefix_len = bit_popcnt32(route->mask);

      // prefer routes with lower metric, but if metrics are equal, prefer longer prefix lengths
      if (route->metric < best_metric ||
          (route->metric == best_metric && prefix_len > best_prefix_len)) {
        best_route = route;
        best_metric = route->metric;
        best_prefix_len = prefix_len;
      }
    }
  }

  best_route = route_getref(best_route);
  mtx_unlock(&routing_lock);
  return best_route;
}

int ip_route_iterate(int (*callback)(route_t *route, void *data), void *data) {
  if (!callback) {
    return -EINVAL;
  }

  int count = 0;
  // TODO: dont hold lock during callbacks
  mtx_lock(&routing_lock);

  route_t *route;
  LIST_FOREACH(route, &routing_table, list) {
    int ret = callback(route, data);
    if (ret < 0) {
      mtx_unlock(&routing_lock);
      return ret;
    }
    count++;
  }

  mtx_unlock(&routing_lock);
  return count;
}

//
// MARK: Protocol Registration
//

int ip_register_protocol(uint8_t protocol, int (*handler)(sk_buff_t *skb)) {
  if (!handler) {
    return -EINVAL;
  }

  mtx_lock(&proto_handlers_lock);

  if (protocol_handlers[protocol]) {
    mtx_unlock(&proto_handlers_lock);
    return -EEXIST;
  }

  protocol_handlers[protocol] = handler;
  mtx_unlock(&proto_handlers_lock);

  DPRINTF("registered IP protocol %d\n", protocol);
  return 0;
}

void ip_unregister_protocol(uint8_t protocol) {
  mtx_lock(&proto_handlers_lock);
  protocol_handlers[protocol] = NULL;
  mtx_unlock(&proto_handlers_lock);

  DPRINTF("unregistered ip protocol %d\n", protocol);
}

//
// MARK: IP Packet Processing
//

uint16_t ip_checksum(const void *data, size_t len) {
  const uint16_t *ptr = (const uint16_t *)data;
  uint32_t sum = 0;
  while (len > 1) {
    sum += *ptr++;
    len -= 2;
  }

  if (len == 1) {
    sum += *(const uint8_t *)ptr << 8;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return ~sum;
}

int ip_rcv(sk_buff_t *skb) {
  int res;
  if (!skb || skb->len < sizeof(struct iphdr)) {
    EPRINTF("received packet too small for IP header\n");
    goto_res(error, -EINVAL);
  }

  struct iphdr *iph = (struct iphdr *)skb->data;
  if (IPVERSION_GET(iph->version_ihl) != IPVERSION) {
    EPRINTF("received non-IPv4 packet (version %d)\n", IPVERSION_GET(iph->version_ihl));
    goto_res(error, -EINVAL);
  }

  int hdrlen = IPHDR_LEN_GET(iph->version_ihl);
  if (hdrlen < sizeof(struct iphdr) || hdrlen > skb->len) {
    EPRINTF("invalid IP header length: %d\n", hdrlen);
    goto_res(error, -EINVAL);
  }

  uint16_t tot_len = ntohs(iph->tot_len);
  if (tot_len < hdrlen || tot_len > skb->len) {
    EPRINTF("invalid IP total length: %u (packet len: %zu)\n", tot_len, skb->len);
    goto_res(error, -EINVAL);
  }

  // verify checksum
  uint16_t orig_check = iph->check;
  iph->check = 0;
  uint16_t calc_check = ip_checksum(iph, hdrlen);
  iph->check = orig_check;
  if (orig_check != calc_check) {
    EPRINTF("IP checksum mismatch: got 0x%04x, expected 0x%04x\n", orig_check, calc_check);
    goto_res(error, -EINVAL);
  }

  skb_trim(skb, tot_len);
  skb_set_network_header(skb, 0);

  // check if packet is for us
  uint32_t daddr = ntohl(iph->daddr);
  bool is_for_us = false;
  if (daddr == INADDR_LOOPBACK || daddr == INADDR_ANY) {
    is_for_us = true;
  } else if (skb->dev && !LIST_EMPTY(&skb->dev->ip_addrs)) {
    LIST_FOR_IN(ifa, &skb->dev->ip_addrs, ifa_link) {
      if (ifa->ifa_address == daddr) {
        is_for_us = true;
        break;
      }
    }
  }

  if (!is_for_us) {
    DPRINTF("packet not for us (dest: {:ip})\n", daddr);
    goto_res(error, 0);
  }

  DPRINTF("received IP packet: {:ip} -> {:ip}, proto %d, len %u\n", ntohl(iph->saddr), daddr, iph->protocol, tot_len);

  if (ntohs(iph->frag_off) & (IP_MF | IP_OFFMASK)) {
    // TODO: support fragmented packets
    EPRINTF("fragmented packets not supported\n");
    goto_res(error, -ENOTSUP);
  }

  // pull ip header and pass to protocol handler
  skb_pull(skb, hdrlen);
  skb_set_transport_header(skb, 0);

  mtx_lock(&proto_handlers_lock);
  int (*handler)(sk_buff_t *) = protocol_handlers[iph->protocol];
  mtx_unlock(&proto_handlers_lock);

  if (!handler) {
    EPRINTF("no handler for IP protocol %d\n", iph->protocol);
    goto_res(error, -ENOTSUP);
  }
  return handler(skb);
LABEL(error);
  skb_free(&skb);
  return res;
}

int ip_output(sk_buff_t *skb, uint32_t saddr, uint32_t daddr, uint8_t protocol, netdev_t *dev) {
  ASSERT(skb != NULL);
  ASSERT(dev != NULL);

  size_t needed_headroom = sizeof(struct iphdr);
  if (dev->type == ARPHRD_ETHER) {
    needed_headroom += ETH_HLEN;
  }

  if (skb_headroom(skb) < needed_headroom) {
    EPRINTF("not enough headroom (need %zu, have %zu)\n", needed_headroom, skb_headroom(skb));
    skb_free(&skb);
    return -ENOBUFS;
  }

  // add ip header
  struct iphdr *iph = skb_push(skb, sizeof(struct iphdr));
  memset(iph, 0, sizeof(struct iphdr));

  iph->version_ihl = IPVERSION_IHL(IPVERSION, sizeof(struct iphdr));
  iph->tos = 0;
  iph->tot_len = htons(skb->len);
  iph->id = htons(ip_id_counter++);
  iph->frag_off = htons(IP_DF);  // don't fragment
  iph->ttl = 64;  // default TTL
  iph->protocol = protocol;
  iph->saddr = htonl(saddr);
  iph->daddr = htonl(daddr);

  iph->check = 0;
  iph->check = ip_checksum(iph, sizeof(struct iphdr));

  skb_set_network_header(skb, 0);
  skb->protocol = ETH_P_IP;

  DPRINTF("sending IP packet: {:ip} -> {:ip}, proto %d, len %u\n", saddr, daddr, protocol, ntohs(iph->tot_len));

  // for ethernet devices, add ethernet header with ARP resolution
  if (dev->type == ARPHRD_ETHER) {
    // lookup route to determine next hop
    uint32_t resolve_ip = daddr;
    route_t *route = ip_route_lookup(daddr);
    if (route && route->gateway != 0) {
      resolve_ip = route->gateway;
    }

    // try to resolve MAC address via ARP
    uint8_t dst_mac[ETH_ALEN];
    int ret = arp_lookup(dev, resolve_ip, dst_mac);
    if (ret < 0) {
      // arp resolution needed - queue packet or add header if resolved immediately
      DPRINTF("ARP resolution needed for {:ip}\n", resolve_ip);
      ret = arp_resolve(dev, resolve_ip, skb);
      if (ret < 0) {
        return ret;  // error
      } else if (ret == 0) {
        return 0;  // packet queued successfully
      }
      // ret == 1: ethernet header already added, continue to transmit
    } else {
      // add ethernet header with resolved MAC address
      ret = eth_header(skb, dev, ETH_P_IP, dst_mac, NULL);
      if (ret < 0) {
        EPRINTF("failed to add ethernet header: {:err}\n", ret);
        skb_free(&skb);
        return ret;
      }
    }
  }

  // transmit packet
  skb->dev = dev;
  return netdev_tx(dev, skb);
}

//
// MARK: Packet Type Registration
//

static packet_type_t ip_packet_type = {
  .type = ETH_P_IP,
  .func = ip_rcv,
};

void ip_init() {
  netdev_add_packet_type(&ip_packet_type);
  DPRINTF("IP protocol initialized\n");
}
MODULE_INIT(ip_init);
