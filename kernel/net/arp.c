//
// Created by Aaron Gill-Braun on 2025-09-18.
//

#include <kernel/net/arp.h>
#include <kernel/net/eth.h>
#include <kernel/net/netdev.h>
#include <kernel/net/in_dev.h>

#include <kernel/mm.h>
#include <kernel/clock.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("arp: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("arp: %s: " fmt, __func__, ##__VA_ARGS__)

#define ARP_CACHE_MAX_ENTRIES 256

typedef struct arp_cache {
  size_t num_entries;
  size_t max_entries;
  mtx_t lock;
  LIST_HEAD(struct arp_entry) entries;
} arp_cache_t;

typedef struct arp_entry {
  uint32_t ip_addr;             // ip address (host byte order)
  uint8_t hw_addr[6];           // hardware address
  netdev_t *dev;                // network device (ref)
  uint8_t state;                // entry state
  uint8_t retries;              // retry count

  id_t timer_id;                // expiry/retry timer id
  uint64_t last_used;           // last use timestamp

  // packets queued waiting for resolution
  LIST_HEAD(struct sk_buff) pending_queue;

  LIST_ENTRY(struct arp_entry) link; // cache list
} arp_entry_t;


static arp_cache_t arp_cache;

//
// MARK: ARP Cache Management
//

static void arp_entry_free(arp_entry_t *entry) {
  if (!entry) {
    return;
  }

  // cancel timer
  if (entry->timer_id) {
    alarm_unregister(entry->timer_id, NULL);
  }

  // free queued packets
  LIST_FOR_IN_SAFE(skb, &entry->pending_queue, list) {
    LIST_REMOVE(&entry->pending_queue, skb, list);
    skb_free(&skb);
  }

  netdev_putref(&entry->dev);
  kfree(entry);
}

static void arp_entry_timeout(alarm_t *alarm, void *data) {
  arp_entry_t *entry = (arp_entry_t *)data;

  mtx_lock(&arp_cache.lock);

  if (entry->state == ARP_STATE_INCOMPLETE) {
    if (entry->retries < ARP_MAX_RETRIES) {
      // retry ARP request
      entry->retries++;
      DPRINTF("retry %u for IP {:ip}\n", entry->retries, entry->ip_addr);

      // send another request
      struct in_ifaddr *ifa = LIST_FIRST(&entry->dev->ip_addrs);
      if (ifa) {
        arp_send_request(entry->dev, ifa->ifa_address, entry->ip_addr);
      }

      // reset timer for next retry
      alarm_unregister(entry->timer_id, NULL);
      alarm_t *new_alarm = alarm_alloc_relative(ARP_RETRY_INTERVAL * 1000000000ULL, alarm_cb(arp_entry_timeout, entry));
      entry->timer_id = new_alarm->id;
      alarm_register(new_alarm);
    } else {
      // failed to resolve
      DPRINTF("failed to resolve IP {:ip} after %u retries\n", entry->ip_addr, entry->retries);
      entry->state = ARP_STATE_FAILED;

      // drop all pending packets
      LIST_FOR_IN_SAFE(skb, &entry->pending_queue, list) {
        LIST_REMOVE(&entry->pending_queue, skb, list);
        skb_free(&skb);
      }

      // remove from cache
      LIST_REMOVE(&arp_cache.entries, entry, link);
      arp_cache.num_entries--;
      entry->timer_id = -1;
      arp_entry_free(entry);
    }
  } else if (entry->state == ARP_STATE_REACHABLE) {
    // entry expired, mark as stale
    entry->state = ARP_STATE_STALE;
    DPRINTF("entry for IP {:ip} marked stale\n", entry->ip_addr);
  } else if (entry->state == ARP_STATE_STALE) {
    // remove stale entry
    DPRINTF("removing stale entry for IP {:ip}\n", entry->ip_addr);
    LIST_REMOVE(&arp_cache.entries, entry, link);
    arp_cache.num_entries--;
    entry->timer_id = -1;
    arp_entry_free(entry);
  }

  mtx_unlock(&arp_cache.lock);
}

arp_entry_t *arp_cache_lookup(uint32_t ip_addr) {
  arp_entry_t *entry;

  mtx_lock(&arp_cache.lock);

  LIST_FOREACH(entry, &arp_cache.entries, link) {
    if (entry->ip_addr == ip_addr) {
      entry->last_used = clock_get_nanos();

      // refresh timer for reachable entries
      if (entry->state == ARP_STATE_REACHABLE) {
        alarm_unregister(entry->timer_id, NULL);
        alarm_t *alarm = alarm_alloc_relative(ARP_CACHE_TIMEOUT * 1000000000ULL, alarm_cb(arp_entry_timeout, entry));
        entry->timer_id = alarm->id;
        alarm_register(alarm);
      }

      mtx_unlock(&arp_cache.lock);
      return entry;
    }
  }

  mtx_unlock(&arp_cache.lock);
  return NULL;
}

arp_entry_t *arp_cache_add(netdev_t *dev, uint32_t ip_addr, uint8_t *hw_addr) {
  ASSERT(dev != NULL);
  // hw_addr can be NULL for incomplete entries

  arp_entry_t *entry = arp_cache_lookup(ip_addr);
  if (entry) {
    // update existing entry
    if (hw_addr && !eth_addr_is_zero(hw_addr)) {
      eth_addr_copy(entry->hw_addr, hw_addr);
      entry->state = ARP_STATE_REACHABLE;
      entry->retries = 0;
      alarm_unregister(entry->timer_id, NULL);
      alarm_t *alarm = alarm_alloc_relative(ARP_CACHE_TIMEOUT * 1000000000ULL, alarm_cb(arp_entry_timeout, entry));
      entry->timer_id = alarm->id;
      alarm_register(alarm);

      // send queued packets
      LIST_FOR_IN_SAFE(skb, &entry->pending_queue, list) {
        LIST_REMOVE(&entry->pending_queue, skb, list);

        // add ethernet header
        if (eth_header(skb, entry->dev, ETH_P_IP, hw_addr, NULL) < 0) {
          skb_free(&skb);
          continue;
        }

        // transmit packet
        netdev_tx(entry->dev, skb);
      }
    }
    return entry;
  }

  mtx_lock(&arp_cache.lock);
  if (arp_cache.num_entries >= ARP_CACHE_MAX_ENTRIES) {
    // evict oldest entry
    arp_entry_t *oldest = NULL;
    uint64_t oldest_time = UINT64_MAX;

    LIST_FOREACH(entry, &arp_cache.entries, link) {
      if (entry->last_used < oldest_time && entry->state != ARP_STATE_INCOMPLETE) {
        oldest = entry;
        oldest_time = entry->last_used;
      }
    }

    if (oldest) {
      LIST_REMOVE(&arp_cache.entries, oldest, link);
      arp_cache.num_entries--;
      arp_entry_free(oldest);
    }
  }

  // create new entry
  entry = kmallocz(sizeof(arp_entry_t));
  if (!entry) {
    mtx_unlock(&arp_cache.lock);
    return NULL;
  }

  entry->ip_addr = ip_addr;
  entry->dev = netdev_getref(dev);
  entry->last_used = clock_get_nanos();

  if (hw_addr && !eth_addr_is_zero(hw_addr)) {
    eth_addr_copy(entry->hw_addr, hw_addr);
    entry->state = ARP_STATE_REACHABLE;
    alarm_t *alarm = alarm_alloc_relative(ARP_CACHE_TIMEOUT * 1000000000ULL, alarm_cb(arp_entry_timeout, entry));
    entry->timer_id = alarm->id;
    alarm_register(alarm);
  } else {
    memset(entry->hw_addr, 0, ETH_ALEN);
    entry->state = ARP_STATE_INCOMPLETE;
    alarm_t *alarm = alarm_alloc_relative(ARP_INCOMPLETE_TIMEOUT * 1000000000ULL, alarm_cb(arp_entry_timeout, entry));
    entry->timer_id = alarm->id;
    alarm_register(alarm);
  }

  LIST_ADD(&arp_cache.entries, entry, link);
  arp_cache.num_entries++;

  mtx_unlock(&arp_cache.lock);

  DPRINTF("added cache entry: IP {:ip} -> MAC {:mac} (state=%u)\n",
          ip_addr, hw_addr ? hw_addr : entry->hw_addr, entry->state);

  return entry;
}

void arp_cache_update(uint32_t ip_addr, uint8_t *hw_addr) {
  arp_cache_add(NULL, ip_addr, hw_addr);
}

void arp_cache_delete(uint32_t ip_addr) {
  mtx_lock(&arp_cache.lock);

  LIST_FOR_IN_SAFE(entry, &arp_cache.entries, link) {
    if (entry->ip_addr == ip_addr) {
      LIST_REMOVE(&arp_cache.entries, entry, link);
      arp_cache.num_entries--;
      arp_entry_free(entry);
      break;
    }
  }

  mtx_unlock(&arp_cache.lock);
}

void arp_cache_flush(netdev_t *dev) {
  mtx_lock(&arp_cache.lock);

  LIST_FOR_IN_SAFE(entry, &arp_cache.entries, link) {
    if (!dev || entry->dev == dev) {
      LIST_REMOVE(&arp_cache.entries, entry, link);
      arp_cache.num_entries--;
      arp_entry_free(entry);
    }
  }

  mtx_unlock(&arp_cache.lock);
}

//
// MARK: ARP Packet Transmission
//

int arp_send_request(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip) {
  ASSERT(dev != NULL);
  struct sk_buff *skb = skb_alloc(ETH_HLEN + sizeof(struct arp_packet) + 32);
  if (!skb) {
    return -ENOMEM;
  }

  // build ARP packet
  struct arp_packet *arp = skb_put_data(skb, sizeof(struct arp_packet));
  arp->arp.ar_hrd = htons(ARPHRD_ETHER);
  arp->arp.ar_pro = htons(ETH_P_IP);
  arp->arp.ar_hln = ETH_ALEN;
  arp->arp.ar_pln = 4;
  arp->arp.ar_op = htons(ARPOP_REQUEST);

  eth_addr_copy(arp->eth_ipv4.ar_sha, dev->dev_addr);
  arp->eth_ipv4.ar_sip = htonl(src_ip);
  memset(arp->eth_ipv4.ar_tha, 0, ETH_ALEN);  // unknown target hardware
  arp->eth_ipv4.ar_tip = htonl(dst_ip);

  // add ethernet header (broadcast for ARP request)
  eth_header(skb, dev, ETH_P_ARP, eth_broadcast_addr, dev->dev_addr);

  DPRINTF("sending ARP request: who has {:ip}? tell {:ip}\n", dst_ip, src_ip);

  // transmit packet
  skb->dev = dev;
  return netdev_tx(dev, skb);
}

int arp_send_reply(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, uint8_t *dst_hw) {
  ASSERT(dev != NULL);
  ASSERT(dst_hw != NULL);

  struct sk_buff *skb = skb_alloc(ETH_HLEN + sizeof(struct arp_packet) + 32);
  if (!skb) {
    return -ENOMEM;
  }

  // build ARP packet
  struct arp_packet *arp = skb_put_data(skb, sizeof(struct arp_packet));
  arp->arp.ar_hrd = htons(ARPHRD_ETHER);
  arp->arp.ar_pro = htons(ETH_P_IP);
  arp->arp.ar_hln = ETH_ALEN;
  arp->arp.ar_pln = 4;
  arp->arp.ar_op = htons(ARPOP_REPLY);

  eth_addr_copy(arp->eth_ipv4.ar_sha, dev->dev_addr);
  arp->eth_ipv4.ar_sip = htonl(src_ip);
  eth_addr_copy(arp->eth_ipv4.ar_tha, dst_hw);
  arp->eth_ipv4.ar_tip = htonl(dst_ip);

  // add ethernet header (unicast to requester)
  eth_header(skb, dev, ETH_P_ARP, dst_hw, dev->dev_addr);

  DPRINTF("sending ARP reply: {:ip} is at {:mac}\n", src_ip, dst_hw);

  // transmit packet
  skb->dev = dev;
  return netdev_tx(dev, skb);
}

int arp_send_gratuitous(netdev_t *dev, uint32_t ip_addr) {
  // gratuitous ARP is a request for our own IP
  return arp_send_request(dev, ip_addr, ip_addr);
}

//
// MARK: ARP Resolution
//

int arp_lookup(netdev_t *dev, uint32_t ip_addr, uint8_t *hw_addr) {
  ASSERT(dev != NULL);
  ASSERT(hw_addr != NULL);

  if ((ip_addr & 0xFF000000) == 0x7F000000) {  // 127.x.x.x
    // loopback address
    memset(hw_addr, 0, ETH_ALEN);
    hw_addr[0] = 0x00;
    hw_addr[1] = 0x00;
    hw_addr[2] = 0x00;
    hw_addr[3] = 0x00;
    hw_addr[4] = 0x00;
    hw_addr[5] = 0x01;
    return 0;
  }

  // check cache
  arp_entry_t *entry = arp_cache_lookup(ip_addr);
  if (entry && entry->state == ARP_STATE_REACHABLE) {
    eth_addr_copy(hw_addr, entry->hw_addr);
    return 0;
  }

  return -EAGAIN;
}

int arp_resolve(netdev_t *dev, uint32_t ip_addr, sk_buff_t *skb) {
  ASSERT(dev != NULL);
  ASSERT(skb != NULL);

  arp_entry_t *entry = arp_cache_lookup(ip_addr);
  if (!entry) {
    // create incomplete entry
    entry = arp_cache_add(dev, ip_addr, NULL);
    if (!entry) {
      return -ENOMEM;
    }

    // send ARP request
    struct in_ifaddr *ifa = LIST_FIRST(&dev->ip_addrs);
    if (ifa) {
      arp_send_request(dev, ifa->ifa_address, ip_addr);
    }
  }

  // handle stale entries - refresh them
  if (entry->state == ARP_STATE_STALE) {
    DPRINTF("refreshing stale entry for IP {:ip}\n", ip_addr);
    entry->state = ARP_STATE_INCOMPLETE;
    entry->retries = 0;

    // send ARP request
    struct in_ifaddr *ifa = LIST_FIRST(&dev->ip_addrs);
    if (ifa) {
      arp_send_request(dev, ifa->ifa_address, ip_addr);
    }

    // update timer
    alarm_unregister(entry->timer_id, NULL);
    alarm_t *alarm = alarm_alloc_relative(ARP_INCOMPLETE_TIMEOUT * 1000000000ULL, alarm_cb(arp_entry_timeout, entry));
    entry->timer_id = alarm->id;
    alarm_register(alarm);
  }

  // queue packet for later transmission
  if (entry->state == ARP_STATE_INCOMPLETE) {
    DPRINTF("queuing packet for IP {:ip}: skb=%p len=%zu data=%p tail=%p\n", ip_addr, skb, skb->len, skb->data, skb->tail);
    LIST_ADD(&entry->pending_queue, skb, list);
    DPRINTF("queued packet for IP {:ip} (pending resolution)\n", ip_addr);
    return 0;  // packet queued successfully
  }

  // entry became reachable between lookup and queue
  // this can happen if ARP reply arrives very quickly
  if (entry->state == ARP_STATE_REACHABLE) {
    DPRINTF("ARP resolved while queuing for IP {:ip}, adding eth header\n", ip_addr);
    int ret = eth_header(skb, dev, ETH_P_IP, entry->hw_addr, NULL);
    if (ret < 0) {
      skb_free(&skb);
      return ret;
    }
    return 1;  // signal caller to transmit immediately
  }

  // entry is in unexpected state (e.g., FAILED)
  DPRINTF("entry for IP {:ip} in unexpected state %u\n", ip_addr, entry->state);
  return -EINVAL;
}

//
// MARK: ARP Packet Reception
//

int arp_rcv(struct sk_buff *skb) {
  ASSERT(skb != NULL);

  struct netdev *dev = skb->dev;
  if (!dev) {
    EPRINTF("no device for ARP packet\n");
    skb_free(&skb);
    return -ENODEV;
  }

  if (skb->len < sizeof(struct arp_packet)) {
    EPRINTF("packet too small (%zu bytes)\n", skb->len);
    skb_free(&skb);
    return -EINVAL;
  }

  struct arp_packet *arp = (struct arp_packet *)skb->data;

  // validate ARP packet
  if (ntohs(arp->arp.ar_hrd) != ARPHRD_ETHER ||
      ntohs(arp->arp.ar_pro) != ETH_P_IP ||
      arp->arp.ar_hln != ETH_ALEN ||
      arp->arp.ar_pln != 4) {
    EPRINTF("unsupported ARP format\n");
    skb_free(&skb);
    return -EINVAL;
  }

  uint16_t op = ntohs(arp->arp.ar_op);
  uint32_t sip = ntohl(arp->eth_ipv4.ar_sip);
  uint32_t tip = ntohl(arp->eth_ipv4.ar_tip);

  DPRINTF("received ARP %s: {:mac} ({:ip}) -> {:mac} ({:ip})\n",
          op == ARPOP_REQUEST ? "request" : "reply",
          arp->eth_ipv4.ar_sha, sip, arp->eth_ipv4.ar_tha, tip);

  // update cache with sender's information
  if (!eth_addr_is_zero(arp->eth_ipv4.ar_sha)) {
    arp_cache_add(dev, sip, arp->eth_ipv4.ar_sha);
  }

  struct in_ifaddr *ifa = LIST_FIRST(&dev->ip_addrs);
  if (!ifa) {
    DPRINTF("device has no IP address\n");
    skb_free(&skb);
    return 0;
  }

  uint32_t our_ip = ifa->ifa_address;
  if (tip == our_ip) {
    if (op == ARPOP_REQUEST) {
      arp_send_reply(dev, our_ip, sip, arp->eth_ipv4.ar_sha);
    } else if (op == ARPOP_REPLY) {
      DPRINTF("received ARP reply for our request\n");
    }
  }

  skb_free(&skb);
  return 0;
}

//
// MARK: Initialization
//

static packet_type_t arp_packet_type = {
  .type = ETH_P_ARP,
  .func = arp_rcv,
};

static void arp_init() {
  arp_cache.num_entries = 0;
  arp_cache.max_entries = ARP_CACHE_MAX_ENTRIES;
  LIST_INIT(&arp_cache.entries);
  mtx_init(&arp_cache.lock, 0, "arp_cache");

  netdev_add_packet_type(&arp_packet_type);
  DPRINTF("ARP protocol initialized (cache size: %zu)\n", arp_cache.max_entries);
}
MODULE_INIT(arp_init);
