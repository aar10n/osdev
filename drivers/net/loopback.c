//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#include <kernel/net/netdev.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <linux/sockios.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG loopback
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("loopback: %s: " fmt, __func__, ##__VA_ARGS__)

static netdev_t *loopback_dev = NULL;

//
// MARK: Netdev Operations
//

static int loopback_open(netdev_t *dev) {
  DPRINTF("opening loopback device\n");
  return 0;
}

static int loopback_close(netdev_t *dev) {
  DPRINTF("stopping loopback device\n");
  return 0;
}

static int loopback_tx(netdev_t *dev, sk_buff_t *skb) {
  if (!dev || !skb) {
    return -EINVAL;
  }

  skb->pkt_type = PACKET_LOOPBACK;
  sk_buff_t *rx_skb = skb_copy(skb);
  if (!rx_skb) {
    return -ENOMEM;
  }

  netdev_rx(dev, rx_skb);
  return 0;
}

static int loopback_ioctl(netdev_t *dev, unsigned long cmd, void *arg) {
  struct ifreq *ifr = arg;
  switch (cmd) {
    case SIOCGIFTXQLEN:
      ifr->ifr_qlen = 0;
      return 0;
    case SIOCSIFTXQLEN:
      return 0;
    default:
      EPRINTF("ioctl: unsupported cmd 0x%lx\n", cmd);
      return -EOPNOTSUPP;
  }
}

static void loopback_get_stats(netdev_t *dev, struct netdev_stats *stats) {
  if (dev && stats) {
    *stats = dev->stats;
  }
}


static const struct netdev_ops loopback_ops = {
  .net_open = loopback_open,
  .net_close = loopback_close,
  .net_start_tx = loopback_tx,
  .net_ioctl = loopback_ioctl,
  .net_get_stats = loopback_get_stats,
};

//
// MARK: Module Initialization
//

void loopback_init() {
  loopback_dev = netdev_alloc(str_from("lo"), 0);
  ASSERT(loopback_dev != NULL);

  loopback_dev->type = ARPHRD_LOOPBACK;
  loopback_dev->flags = NETDEV_LOOPBACK;
  loopback_dev->mtu = 65536;  // large MTU for loopback
  loopback_dev->netdev_ops = &loopback_ops;

  // set loopback address (127.0.0.1)
  loopback_dev->addr_len = 4;
  loopback_dev->dev_addr[0] = 127;
  loopback_dev->dev_addr[1] = 0;
  loopback_dev->dev_addr[2] = 0;
  loopback_dev->dev_addr[3] = 1;

  // register device
  int ret = netdev_register(loopback_dev);
  if (ret < 0) {
    DPRINTF("failed to register loopback device: {:err}\n", ret);
    netdev_putref(&loopback_dev);
    loopback_dev = NULL;
    return;
  }

  DPRINTF("loopback device initialized (down)\n");
}
MODULE_INIT(loopback_init);
