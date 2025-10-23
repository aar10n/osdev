//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#ifndef KERNEL_NET_NETDEV_H
#define KERNEL_NET_NETDEV_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/str.h>
#include <kernel/queue.h>

#include <kernel/net/skbuff.h>

// temporarily undefine packed to avoid conflicts with Linux headers
#pragma push_macro("packed")
#undef packed
#include <linux/if_arp.h>
#pragma pop_macro("packed")

//
// Network Device Framework
//

struct netdev;
struct netdev_stats;
struct in_ifaddr;
struct packet_type;

struct netdev_stats {
  uint64_t rx_packets;    // total packets received
  uint64_t tx_packets;    // total packets transmitted
  uint64_t rx_bytes;      // total bytes received
  uint64_t tx_bytes;      // total bytes transmitted
  uint64_t rx_errors;     // bad packets received
  uint64_t tx_errors;     // packet transmit problems
  uint64_t rx_dropped;    // no space in linux buffers
  uint64_t tx_dropped;    // no space available in linux
  uint64_t multicast;     // multicast packets received
  uint64_t collisions;    // collision count
};

/**
 * Packet type registration.
 *
 * Packet type handlers register to receive packets of a specific ethernet type.
 * When a packet arrives with a matching type field, the handler function is invoked.
 */
typedef struct packet_type {
  uint16_t type;                        // ethernet packet type (ETH_P_IP, ETH_P_ARP, etc.)
  int (*func)(sk_buff_t *skb);          // receive handler function
  LIST_ENTRY(struct packet_type) list;  // registry linkage
} packet_type_t;

/**
 * A network device.
 *
 * This structure represents a network interface device in the system.
 */
typedef struct netdev {
  str_t name;                       // interface name (e.g., "lo", "eth0")
  uint16_t type;                    // hardware type (ARPHRD_*)
  uint16_t flags;                   // device flags (NETDEV_*)
  uint32_t mtu;                     // maximum transmission unit

  uint8_t dev_addr[6];              // hardware address (MAC)
  uint8_t addr_len;                 // hardware address length
  LIST_HEAD(struct in_ifaddr) ip_addrs;  // ipv4 addresses

  struct netdev_stats stats;         // device statistics
  const struct netdev_ops *netdev_ops;   // device operations
  void *data;                       // private driver data

  _refcount;

  /* registration */
  int ifindex;                      // interface index
  LIST_ENTRY(struct netdev) list;   // device list linkage
} netdev_t;

struct netdev_ops {
  int (*net_open)(struct netdev *dev);
  int (*net_close)(struct netdev *dev);
  int (*net_start_tx)(struct netdev *dev, sk_buff_t *skb);
  int (*net_ioctl)(struct netdev *dev, unsigned long cmd, void *arg);
  int (*net_set_mac_addr)(struct netdev *dev, void *addr);
  void (*net_get_stats)(struct netdev *dev, struct netdev_stats *stats);
};

// network device states
#define NETDEV_UP       0x0001  // device is up
#define NETDEV_RUNNING  0x0002  // device is running
#define NETDEV_LOOPBACK 0x0004  // loopback device

#define netdev_getref(dev) ({ \
  ASSERT_IS_TYPE(netdev_t *, dev); \
  netdev_t *__dev = (dev); \
  __dev ? ref_get(&__dev->refcount) : NULL; \
  __dev; \
})

#define netdev_putref(devref) ({ \
  ASSERT_IS_TYPE(netdev_t **, devref); \
  netdev_t *__dev = *(devref); \
  *(devref) = NULL; \
  if (__dev) { \
    if (ref_put(&__dev->refcount)) { \
      _netdev_free(&__dev); \
    } \
  } \
})

//
// Network Device API
//

void netdev_add_packet_type(packet_type_t *pt);
void netdev_remove_packet_type(packet_type_t *pt);

int netdev_register(netdev_t *dev);
void netdev_unregister(netdev_t *dev);

netdev_t *netdev_alloc(str_t name, size_t priv_size);
void _netdev_free(netdev_t **devp);
__ref netdev_t *netdev_get_by_name(const char *name);
__ref netdev_t *netdev_get_by_index(int ifindex);

int netdev_open(netdev_t *dev);
int netdev_close(netdev_t *dev);
int netdev_tx(netdev_t *dev, sk_buff_t *skb);
int netdev_rx(netdev_t *dev, sk_buff_t *skb);
int netdev_receive_skb(sk_buff_t *skb);
int netdev_ioctl(unsigned long request, uintptr_t argp);

typedef int (*netdev_iter_func_t)(netdev_t *dev, void *data);
int netdev_iterate_all(netdev_iter_func_t func, void *data);

static inline void *netdev_data(netdev_t *dev) {
  return dev->data;
}

#endif
