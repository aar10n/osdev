//
// Created by Aaron Gill-Braun on 2025-09-16.
//

#include <kernel/net/in_dev.h>
#include <kernel/net/ip.h>
#include <kernel/net/netdev.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("in_dev: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("in_dev: %s: " fmt, __func__, ##__VA_ARGS__)

static mtx_t in_dev_lock;

void in_dev_static_init() {
  mtx_init(&in_dev_lock, MTX_SPIN, "in_dev");
}
STATIC_INIT(in_dev_static_init);

//

static uint32_t inet_make_mask(int logmask) {
  if (logmask == 0) {
    return 0;
  }
  return ~((1U << (32 - logmask)) - 1);
}

static uint32_t inet_make_broadcast(uint32_t addr, uint32_t mask) {
  return addr | ~mask;
}

//

void _ifa_cleanup(in_ifaddr_t **ifap) {
  in_ifaddr_t *ifa = moveptr(*ifap);
  if (!ifa) {
    return;
  }

  ASSERT(read_refcount(ifa) == 0);
  netdev_putref(&ifa->ifa_dev);
  str_free(&ifa->ifa_label);
  kfree(ifa);
}

void inet_dev_cleanup(netdev_t *dev) {
  ASSERT(dev != NULL);
  mtx_spin_lock(&in_dev_lock);
  LIST_FOR_IN_SAFE(ifa, &dev->ip_addrs, ifa_link) {
    LIST_REMOVE(&dev->ip_addrs, ifa, ifa_link);
    ifa_putref(&ifa);
  }
  mtx_spin_unlock(&in_dev_lock);
}

int inet_addr_add(netdev_t *dev, uint32_t addr, uint8_t prefixlen, uint32_t broadcast, uint8_t scope, cstr_t label) {
  ASSERT(dev != NULL);
  in_ifaddr_t *ifa = kmallocz(sizeof(in_ifaddr_t));
  if (!ifa) {
    return -ENOMEM;
  }

  ifa->ifa_dev = netdev_getref(dev);
  ifa->ifa_address = addr;
  ifa->ifa_local = addr;
  ifa->ifa_prefixlen = prefixlen;
  ifa->ifa_mask = inet_make_mask(prefixlen);
  ifa->ifa_scope = scope;
  initref(ifa);

  if (broadcast) {
    ifa->ifa_broadcast = broadcast;
  } else if (dev->flags & NETDEV_LOOPBACK) {
    ifa->ifa_broadcast = addr;
  } else {
    ifa->ifa_broadcast = inet_make_broadcast(addr, ifa->ifa_mask);
  }

  if (!cstr_isnull(label)) {
    ifa->ifa_label = str_from_cstr(label);
  } else {
    ifa->ifa_label = str_dup(dev->name);
  }

  mtx_spin_lock(&in_dev_lock);

  // check if address already exists
#define IS_SAME_ADDR(_a, _b) ((_a)->ifa_address == (_b)->ifa_address && (_a)->ifa_prefixlen == (_b)->ifa_prefixlen)
  in_ifaddr_t *existing = LIST_FIND(_ifa, &dev->ip_addrs, ifa_link, IS_SAME_ADDR(ifa, _ifa));
  if (existing) {
    mtx_spin_unlock(&in_dev_lock);
    ifa_putref(&ifa);
    return -EEXIST;
  }

  LIST_ADD_FRONT(&dev->ip_addrs, ifa, ifa_link);
  mtx_spin_unlock(&in_dev_lock);

  DPRINTF("added address {:ip}/%d to device {:str}\n", addr, prefixlen, &dev->name);
  uint32_t mask = inet_make_mask(prefixlen);
  uint32_t network = addr & mask;

  int ret = ip_route_add(network, mask, 0, dev, 0);
  if (ret < 0) {
    EPRINTF("failed to add route for network {:ip}/%d: {:err}\n", network, prefixlen, ret);
  }
  return 0;
}

int inet_addr_del(netdev_t *dev, uint32_t addr, uint8_t prefixlen) {
  ASSERT(dev != NULL);
  mtx_spin_lock(&in_dev_lock);
  in_ifaddr_t *ifa = LIST_FIND(_ifa, &dev->ip_addrs, ifa_link, _ifa->ifa_address == addr && _ifa->ifa_prefixlen == prefixlen);
  if (!ifa) {
    mtx_spin_unlock(&in_dev_lock);
    return -EADDRNOTAVAIL;
  }

  LIST_REMOVE(&dev->ip_addrs, ifa, ifa_link);
  mtx_spin_unlock(&in_dev_lock);

  DPRINTF("deleted address {:ip}/%d from device {:str}\n", addr, prefixlen, &dev->name);

  uint32_t mask = inet_make_mask(prefixlen);
  uint32_t network = addr & mask;

  int ret = ip_route_del(network, mask);
  if (ret < 0) {
    EPRINTF("failed to delete route for network {:ip}/%d: {:err}\n", network, prefixlen, ret);
  }

  ifa_putref(&ifa);
  return 0;
}

__ref in_ifaddr_t *inet_addr_find(netdev_t *dev, uint32_t addr) {
  ASSERT(dev != NULL);
  mtx_spin_lock(&in_dev_lock);

  in_ifaddr_t *ifa = LIST_FIND(_ifa, &dev->ip_addrs, ifa_link, _ifa->ifa_address == addr);
  if (ifa) {
    ifa = ifa_getref(ifa);
  }

  mtx_spin_unlock(&in_dev_lock);
  return ifa;
}


int inet_addr_iterate(netdev_t *dev, inet_addr_iter_func_t func, void *data) {
  ASSERT(dev != NULL);
  ASSERT(func != NULL);
  mtx_spin_lock(&in_dev_lock);

  int ret = 0;
  LIST_FOR_IN_SAFE(ifa, &dev->ip_addrs, ifa_link) {
    // TODO: dont hold lock during callbacks
    ret = func(dev, ifa, data);
    if (ret) {
      break;
    }
  }

  mtx_spin_unlock(&in_dev_lock);
  return ret;
}

struct iter_data {
  inet_addr_iter_func_t func;
  void *data;
  int ret;
};

int dev_iter(netdev_t *dev, void *arg) {
  ASSERT(dev != NULL);
  struct iter_data *iter = arg;
  iter->ret = inet_addr_iterate(dev, iter->func, iter->data);
  return iter->ret;
}

int inet_addr_iterate_all(inet_addr_iter_func_t func, void *data) {
  ASSERT(func != NULL);
  struct iter_data iter = { func, data, 0 };
  netdev_iterate_all(dev_iter, &iter);
  return iter.ret;
}
