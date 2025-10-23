//
// Created by Aaron Gill-Braun on 2025-09-16.
//

#ifndef KERNEL_NET_IN_DEV_H
#define KERNEL_NET_IN_DEV_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/str.h>
#include <kernel/queue.h>

#include <linux/in.h>

typedef struct netdev netdev_t;

/**
 * An IPv4 interface address.
 */
typedef struct in_ifaddr {
  uint32_t ifa_address;           // ip address
  uint32_t ifa_local;             // local address (same as ifa_address for normal interfaces)
  uint32_t ifa_mask;              // network mask
  uint32_t ifa_broadcast;         // broadcast address

  uint8_t ifa_prefixlen;          // prefix length (e.g., 24 for /24)
  uint8_t ifa_scope;              // address scope (RT_SCOPE_*)
  uint8_t ifa_flags;              // flags (IFA_F_*)

  netdev_t *ifa_dev;              // associated network device (ref)

  str_t ifa_label;                // interface label (usually same as device name)

  _refcount;
  LIST_ENTRY(struct in_ifaddr) ifa_link;
} in_ifaddr_t;

#define ifa_getref(ifa) ({ \
  ASSERT_IS_TYPE(in_ifaddr_t *, ifa); \
  in_ifaddr_t *__ifa = (ifa); \
  __ifa ? ref_get(&__ifa->refcount) : NULL; \
  __ifa; \
})

#define ifa_putref(ifaref) ({ \
  ASSERT_IS_TYPE(in_ifaddr_t **, ifaref); \
  in_ifaddr_t *__ifa = *(ifaref); \
  *(ifaref) = NULL; \
  if (__ifa) { \
    if (ref_put(&__ifa->refcount)) { \
      _ifa_cleanup(&__ifa); \
    } \
  } \
})

//
// IPv4 Address Management API
//

void _ifa_cleanup(in_ifaddr_t **ifap);
void inet_dev_cleanup(netdev_t *dev);

int inet_addr_add(netdev_t *dev, uint32_t addr, uint8_t prefixlen, uint32_t broadcast, uint8_t scope, cstr_t label);
int inet_addr_del(netdev_t *dev, uint32_t addr, uint8_t prefixlen);
__ref in_ifaddr_t *inet_addr_find(netdev_t *dev, uint32_t addr);

typedef int (*inet_addr_iter_func_t)(netdev_t *dev, in_ifaddr_t *ifa, void *data);
int inet_addr_iterate(netdev_t *dev, inet_addr_iter_func_t func, void *data);
int inet_addr_iterate_all(inet_addr_iter_func_t func, void *data);

#endif
