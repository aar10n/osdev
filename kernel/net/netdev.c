//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#include <kernel/net/netdev.h>
#include <kernel/net/in_dev.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/route.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG netdev
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("netdev: %s: " fmt, __func__, ##__VA_ARGS__)

static LIST_HEAD(netdev_t) netdev_list;
static mtx_t netdev_list_lock;
static int next_ifindex = 1;

static LIST_HEAD(link_type_t) ltype_list;
static mtx_t ltype_list_lock;

static LIST_HEAD(packet_type_t) ptype_list;
static mtx_t ptype_list_lock;

void netdev_static_init() {
  LIST_INIT(&netdev_list);
  mtx_init(&netdev_list_lock, 0, "netdev_list");
  LIST_INIT(&ltype_list);
  mtx_init(&ltype_list_lock, 0, "ltype_list");
  LIST_INIT(&ptype_list);
  mtx_init(&ptype_list_lock, 0, "ptype_list");
}
STATIC_INIT(netdev_static_init);

//
// MARK: Link-Layer Handler Registration
//

void netdev_add_link_type(link_type_t *lt) {
  ASSERT(lt != NULL);
  ASSERT(lt->func != NULL);

  mtx_lock(&ltype_list_lock);
  LIST_ADD(&ltype_list, lt, list);
  mtx_unlock(&ltype_list_lock);

  DPRINTF("registered link type %d\n", lt->type);
}

void netdev_remove_link_type(link_type_t *lt) {
  ASSERT(lt != NULL);

  mtx_lock(&ltype_list_lock);
  LIST_REMOVE(&ltype_list, lt, list);
  mtx_unlock(&ltype_list_lock);

  DPRINTF("unregistered link type %d\n", lt->type);
}

//
// MARK: Protocol Handler Registration
//

void netdev_add_packet_type(packet_type_t *pt) {
  ASSERT(pt != NULL);
  ASSERT(pt->func != NULL);

  mtx_lock(&ptype_list_lock);
  LIST_ADD(&ptype_list, pt, list);
  mtx_unlock(&ptype_list_lock);

  DPRINTF("registered packet type 0x%04x\n", pt->type);
}

void netdev_remove_packet_type(packet_type_t *pt) {
  ASSERT(pt != NULL);

  mtx_lock(&ptype_list_lock);
  LIST_REMOVE(&ptype_list, pt, list);
  mtx_unlock(&ptype_list_lock);

  DPRINTF("unregistered packet type 0x%04x\n", pt->type);
}


//
// MARK: Device Registration
//

int netdev_register(__ref netdev_t *dev) {
  ASSERT(dev != NULL);
  mtx_lock(&netdev_list_lock);

  // check for duplicate name
  netdev_t *existing;
  LIST_FOREACH(existing, &netdev_list, list) {
    if (str_eq(existing->name, dev->name)) {
      mtx_unlock(&netdev_list_lock);
      netdev_putref(&dev);
      return -EEXIST;
    }
  }

  dev->ifindex = next_ifindex++;
  LIST_ADD(&netdev_list, netdev_getref(dev), list);
  mtx_unlock(&netdev_list_lock);

  DPRINTF("registered device {:str} (index %d)\n", &dev->name, dev->ifindex);
  return 0;
}

void netdev_unregister(netdev_t *dev) {
  ASSERT(dev != NULL);
  mtx_lock(&netdev_list_lock);
  LIST_REMOVE(&netdev_list, dev, list);
  mtx_unlock(&netdev_list_lock);

  if (dev->flags & NETDEV_UP) {
    netdev_close(dev);
  }

  DPRINTF("unregistered device {:str}\n", &dev->name);
  netdev_putref(&dev);
}


netdev_t *netdev_alloc(str_t name, size_t priv_size) {
  netdev_t *dev = kmallocz(sizeof(netdev_t));
  if (!dev) {
    return NULL;
  }

  if (priv_size > 0) {
    dev->data = kmallocz(priv_size);
    if (!dev->data) {
      kfree(dev);
      return NULL;
    }
  }

  dev->name = name;
  dev->type = ARPHRD_VOID;
  dev->flags = 0;
  dev->mtu = 1500;  // default ethernet MTU
  dev->addr_len = 0;

  dev->netdev_ops = NULL;
  dev->ifindex = 0;

  mtx_init(&dev->lock, MTX_RECURSIVE, "netdev");
  initref(dev);
  return dev;
}

void _netdev_free(netdev_t **devp) {
  netdev_t *dev = moveptr(*devp);
  if (!dev) {
    return;
  }

  ASSERT(read_refcount(dev) == 0);
  inet_dev_cleanup(dev);
  str_free(&dev->name);

  if (dev->data) {
    kfree(dev->data);
  }

  mtx_destroy(&dev->lock);
  kfree(dev);
}

//
// MARK: Device Lookup
//

__ref netdev_t *netdev_get_by_name(const char *name) {
  if (!name) {
    return NULL;
  }

  mtx_lock(&netdev_list_lock);
  netdev_t *dev = LIST_FIND(_netdev, &netdev_list, list, str_eq_charp(_netdev->name, name));
  if (dev) {
    dev = netdev_getref(dev);
  }
  mtx_unlock(&netdev_list_lock);
  return dev;
}

__ref netdev_t *netdev_get_by_index(int ifindex) {
  if (ifindex <= 0) {
    return NULL;
  }

  mtx_lock(&netdev_list_lock);
  netdev_t *dev = LIST_FIND(_netdev, &netdev_list, list, _netdev->ifindex == ifindex);
  if (dev) {
    dev = netdev_getref(dev);
  }
  mtx_unlock(&netdev_list_lock);
  return dev;
}

//
// MARK: Device Operations
//

int netdev_open(netdev_t *dev) {
  ASSERT(dev != NULL);
  ASSERT(dev->netdev_ops != NULL);

  mtx_lock(&dev->lock);
  if (dev->flags & NETDEV_UP) {
    mtx_unlock(&dev->lock);
    return 0;  // already up
  }

  int ret = 0;
  if (dev->netdev_ops->net_open) {
    ret = dev->netdev_ops->net_open(dev);
    if (ret < 0) {
      mtx_unlock(&dev->lock);
      return ret;
    }
  }

  dev->flags |= NETDEV_UP | NETDEV_RUNNING;
  mtx_unlock(&dev->lock);

  DPRINTF("device {:str} opened\n", &dev->name);
  return 0;
}

int netdev_close(netdev_t *dev) {
  ASSERT(dev != NULL);
  ASSERT(dev->netdev_ops != NULL);

  mtx_lock(&dev->lock);
  if (!(dev->flags & NETDEV_UP)) {
    mtx_unlock(&dev->lock);
    return 0;  // already down
  }

  dev->flags &= ~(NETDEV_UP | NETDEV_RUNNING);
  if (dev->netdev_ops->net_close) {
    dev->netdev_ops->net_close(dev);
  }
  mtx_unlock(&dev->lock);

  DPRINTF("device {:str} closed\n", &dev->name);
  return 0;
}

int netdev_tx(netdev_t *dev, sk_buff_t *skb) {
  ASSERT(skb != NULL);
  ASSERT(dev != NULL);
  ASSERT(dev->netdev_ops != NULL);

  mtx_lock(&dev->lock);
  if (!(dev->flags & NETDEV_RUNNING)) {
    mtx_unlock(&dev->lock);
    skb_free(&skb);
    return -ENETDOWN;
  }
  mtx_unlock(&dev->lock);

  skb->dev = dev;

  int ret = 0;
  if (dev->netdev_ops->net_start_tx) {
    ret = dev->netdev_ops->net_start_tx(dev, skb);
  } else {
    ret = -EOPNOTSUPP;
  }

  mtx_lock(&dev->lock);
  if (ret == 0) {
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;
  } else {
    dev->stats.tx_errors++;
    dev->stats.tx_dropped++;
  }
  mtx_unlock(&dev->lock);

  return ret;
}

int netdev_receive_skb(sk_buff_t *skb) {
  ASSERT(skb != NULL);

  mtx_lock(&ptype_list_lock);
  packet_type_t *pt = LIST_FIND(_pt, &ptype_list, list, _pt->type == skb->protocol);
  mtx_unlock(&ptype_list_lock);
  if (!pt) {
    DPRINTF("no handler for protocol 0x%04x\n", skb->protocol);
    skb_free(&skb);
    return -ENOTSUP;
  }
  return pt->func(skb);
}

int netdev_rx(netdev_t *dev, sk_buff_t *skb) {
  ASSERT(skb != NULL);
  ASSERT(dev != NULL);
  ASSERT(dev->netdev_ops != NULL);

  mtx_lock(&dev->lock);
  if (!(dev->flags & NETDEV_RUNNING)) {
    mtx_unlock(&dev->lock);
    skb_free(&skb);
    return -ENETDOWN;
  }
  uint16_t dev_type = dev->type;
  mtx_unlock(&dev->lock);

  skb->dev = dev;

  // lookup link-layer handler for this device type
  mtx_lock(&ltype_list_lock);
  link_type_t *lt = LIST_FIND(_lt, &ltype_list, list, _lt->type == dev_type);
  mtx_unlock(&ltype_list_lock);

  if (lt) {
    int ret = lt->func(skb);
    if (ret == 0) {
      mtx_lock(&dev->lock);
      dev->stats.rx_packets++;
      dev->stats.rx_bytes += skb->len;
      mtx_unlock(&dev->lock);
    }
    return ret;
  }

  // no link-layer handler registered - assume protocol is already set and pass through
  mtx_lock(&dev->lock);
  dev->stats.rx_packets++;
  dev->stats.rx_bytes += skb->len;
  mtx_unlock(&dev->lock);
  return netdev_receive_skb(skb);
}

int netdev_ioctl(unsigned long request, uintptr_t argp) {
  switch (request) {
    case SIOCSIFFLAGS: {
      struct ifreq *ifr = (struct ifreq *)argp;
      if (!ifr) {
        return -EINVAL;
      }

      netdev_t *dev = netdev_get_by_name(ifr->ifr_name);
      if (!dev) {
        return -ENODEV;
      }

      short flags = ifr->ifr_flags;
      int ret = 0;

      mtx_lock(&dev->lock);
      if (flags & IFF_UP) {
        if (!(dev->flags & NETDEV_UP)) {
          mtx_unlock(&dev->lock);
          ret = netdev_open(dev);
        } else {
          mtx_unlock(&dev->lock);
        }
      } else {
        if (dev->flags & NETDEV_UP) {
          mtx_unlock(&dev->lock);
          ret = netdev_close(dev);
        } else {
          mtx_unlock(&dev->lock);
        }
      }

      netdev_putref(&dev);
      return ret;
    }
    case SIOCGIFFLAGS: {
      struct ifreq *ifr = (struct ifreq *)argp;
      if (!ifr) {
        return -EINVAL;
      }

      netdev_t *dev = netdev_get_by_name(ifr->ifr_name);
      if (!dev) {
        return -ENODEV;
      }

      mtx_lock(&dev->lock);
      short flags = 0;
      if (dev->flags & NETDEV_UP) {
        flags |= IFF_UP;
      }
      if (dev->flags & NETDEV_RUNNING) {
        flags |= IFF_RUNNING;
      }
      if (dev->flags & NETDEV_LOOPBACK) {
        flags |= IFF_LOOPBACK;
      }
      mtx_unlock(&dev->lock);

      ifr->ifr_flags = flags;
      netdev_putref(&dev);
      return 0;
    }
    case SIOCGIFINDEX: {
      struct ifreq *ifr = (struct ifreq *)argp;
      if (!ifr) {
        return -EINVAL;
      }

      netdev_t *dev = netdev_get_by_name(ifr->ifr_name);
      if (!dev) {
        DPRINTF("SIOCGIFINDEX: device '%s' not found\n", ifr->ifr_name);
        return -ENODEV;
      }

      ifr->ifr_ifindex = dev->ifindex;
      DPRINTF("SIOCGIFINDEX: device '%s' has index %d\n", ifr->ifr_name, dev->ifindex);

      netdev_putref(&dev);
      return 0;
    }
    case SIOCGIFTXQLEN:
    case SIOCSIFTXQLEN: {
      struct ifreq *ifr = (struct ifreq *)argp;
      if (!ifr) {
        return -EINVAL;
      }

      netdev_t *dev = netdev_get_by_name(ifr->ifr_name);
      if (!dev) {
        return -ENODEV;
      }

      int ret = -EOPNOTSUPP;
      if (dev->netdev_ops && dev->netdev_ops->net_ioctl) {
        ret = dev->netdev_ops->net_ioctl(dev, request, ifr);
      }

      netdev_putref(&dev);
      return ret;
    }
    case SIOCADDRT: {
      struct rtentry *rt = (struct rtentry *)argp;
      if (!rt) {
        return -EINVAL;
      }

      struct sockaddr_in *dst_sin = (struct sockaddr_in *)&rt->rt_dst;
      struct sockaddr_in *gw_sin = (struct sockaddr_in *)&rt->rt_gateway;
      struct sockaddr_in *mask_sin = (struct sockaddr_in *)&rt->rt_genmask;
      if (dst_sin->sin_family != AF_INET || (rt->rt_flags & RTF_GATEWAY && gw_sin->sin_family != AF_INET)) {
        return -EAFNOSUPPORT;
      }

      uint32_t dest = ntohl(dst_sin->sin_addr.s_addr);
      uint32_t gateway = 0;
      uint32_t mask = 0xFFFFFFFF;  // default to host route

      if (rt->rt_flags & RTF_GATEWAY) {
        gateway = ntohl(gw_sin->sin_addr.s_addr);
      }

      if (mask_sin->sin_family == AF_INET) {
        mask = ntohl(mask_sin->sin_addr.s_addr);
      } else if (!(rt->rt_flags & RTF_HOST)) {
        // if not a host route and no mask specified, use default for network
        mask = 0xFFFFFF00;  // assume /24
      }

      // find the output device
      netdev_t *dev = NULL;
      if (rt->rt_dev) {
        dev = netdev_get_by_name(rt->rt_dev);
        if (!dev) {
          return -ENODEV;
        }
      } else {
        // no device specified, try to find one. For now, just use the first non-loopback interface
        mtx_lock(&netdev_list_lock);
        dev = LIST_FIND(_dev, &netdev_list, list, !(_dev->flags & NETDEV_LOOPBACK));
        mtx_unlock(&netdev_list_lock);
        if (!dev) {
          return -ENETUNREACH;
        }
      }

      // TODO: add ip route
      // int metric = max(rt->rt_metric, 0);
      // int ret = ip_route_add(dest, mask, gateway, dev, metric);
      (void)dest;
      (void)gateway;
      (void)mask;
      netdev_putref(&dev);
      return 0;
    }
    case SIOCDELRT: {
      struct rtentry *rt = (struct rtentry *)argp;
      if (!rt) {
        return -EINVAL;
      }

      // extract destination from sockaddr structure
      struct sockaddr_in *dst_sin = (struct sockaddr_in *)&rt->rt_dst;
      if (dst_sin->sin_family != AF_INET) {
        return -EAFNOSUPPORT;
      }

      uint32_t dest = ntohl(dst_sin->sin_addr.s_addr);
      (void)dest;
      // TODO: implement ip_route_del
      // int ret = ip_route_del(dest, mask);
      return 0;
    }
    default:
      EPRINTF("unsupported cmd 0x%lx\n", request);
      return -ENOTTY;
  }
}

int netdev_iterate_all(netdev_iter_func_t func, void *data) {
  if (!func) {
    return -EINVAL;
  }

  // TODO: dont hold lock during callbacks
  mtx_lock(&netdev_list_lock);

  int count = 0;
  LIST_FOR_IN_SAFE(dev, &netdev_list, list) {
    dev = netdev_getref(dev);
    int ret = func(dev, data);
    netdev_putref(&dev);
    if (ret < 0) {
      mtx_unlock(&netdev_list_lock);
      return ret;
    }

    count++;
  }

  mtx_unlock(&netdev_list_lock);
  return count;
}
