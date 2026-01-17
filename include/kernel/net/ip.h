//
// Created by Aaron Gill-Braun on 2025-09-18.
//

#ifndef KERNEL_NET_IP_H
#define KERNEL_NET_IP_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/queue.h>

typedef struct netdev netdev_t;
typedef struct sk_buff sk_buff_t;

// IP protocol constants
#define IPVERSION       4
#define IP_MAXPACKET    65535   // maximum packet size
#define IP_DF           0x4000  // don't fragment flag
#define IP_MF           0x2000  // more fragments flag
#define IP_OFFMASK      0x1fff  // mask for fragmenting bits

/**
 * An IPv4 header.
 *
 * https://datatracker.ietf.org/doc/html/rfc791#section-3.1
 */
struct iphdr {
  uint8_t version_ihl;    // version (4) + internet header length (4)
  uint8_t tos;            // type of service
  uint16_t tot_len;       // total length
  uint16_t id;            // identification
  uint16_t frag_off;      // fragment offset field
  uint8_t ttl;            // time to live
  uint8_t protocol;       // protocol
  uint16_t check;         // checksum
  uint32_t saddr;         // source address
  uint32_t daddr;         // destination address
  // TODO: support options
};
static_assert(sizeof(struct iphdr) == 20);

#define IPVERSION_GET(ihl) (((ihl) >> 4) & 0xF)
#define IPHDR_LEN_GET(ihl) (((ihl) & 0xF) << 2)
#define IPVERSION_IHL(ver, len) (((ver) << 4) | ((len) >> 2))

//
// Routing Table
//

typedef struct route {
  uint32_t dest;          // destination network
  uint32_t mask;          // network mask
  uint32_t gateway;       // gateway address
  netdev_t *dev;          // output device (ref)
  int metric;             // route metric

  _refcount;
  LIST_ENTRY(struct route) list;
} route_t;

#define route_getref(route) ({ \
  ASSERT_IS_TYPE(route_t *, route); \
  route_t *__route = (route); \
  __route ? ref_get(&__route->refcount) : NULL; \
  __route; \
})

#define route_putref(routeref) ({ \
  ASSERT_IS_TYPE(route_t **, routeref); \
  route_t *__route = *(routeref); \
  *(routeref) = NULL; \
  if (__route) { \
    if (ref_put(&__route->refcount)) { \
      _route_free(&__route); \
    } \
  } \
})

//
// MARK: IP Routing and Protocol API
//

void _route_free(route_t **routep);
int ip_route_add(uint32_t dest, uint32_t mask, uint32_t gateway, netdev_t *dev, int metric);
int ip_route_del(uint32_t dest, uint32_t mask);
__ref route_t *ip_route_lookup(uint32_t dest);
int ip_route_iterate(int (*callback)(route_t *route, void *data), void *data);

int ip_register_protocol(uint8_t protocol, int (*handler)(sk_buff_t *skb));
void ip_unregister_protocol(uint8_t protocol);

uint16_t ip_checksum(const void *data, size_t len);
int ip_rcv(sk_buff_t *skb);
int ip_output(sk_buff_t *skb, uint32_t saddr, uint32_t daddr, uint8_t protocol, netdev_t *dev);

#endif
