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
#include <kernel/proc.h>
#include <kernel/chan.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG arp
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("arp: %s: " fmt, __func__, ##__VA_ARGS__)

#define ARP_CACHE_MAX_ENTRIES 256

typedef struct arp_cache {
  size_t num_entries;
  size_t max_entries;
  mtx_t lock;
  LIST_HEAD(struct arp_entry) entries;
} arp_cache_t;

typedef struct arp_entry {
  _refcount;
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

#define arp_putref(entry)  putref(entry, _arp_entry_free)

typedef struct arp_timer_event {
  arp_entry_t *entry;
  id_t timer_id;
} arp_timer_event_t;

static arp_cache_t arp_cache;
static chan_t *arp_softirq_chan;
static pid_t arp_softirq_pid = -1;

static void _arp_entry_free(arp_entry_t *entry);
static void arp_entry_timeout(__ref arp_entry_t *entry, id_t fired_timer_id);

static void arp_entry_timeout_cb(alarm_t *alarm, __ref arp_entry_t *entry) {
  // this is the stub that runs in an interrupt context, defer to softirq
  arp_timer_event_t event = { entry, alarm->id };
  if (chan_send(arp_softirq_chan, &event) < 0) {
    EPRINTF("failed to send to softirq channel\n");
    arp_putref(&entry);
  }
}

static int arp_softirq_handler() {
  // this runs in a dedicated kernel process
  DPRINTF("starting arp softirq handler\n");

  arp_timer_event_t event;
  while (chan_recv(arp_softirq_chan, &event) == 0) {
    arp_entry_timeout(event.entry, event.timer_id);
  }

  DPRINTF("softirq channel closed, exiting handler\n");
  return 0;
}

static void _arp_entry_free(__ref arp_entry_t *entry) {
  if (!entry) {
    return;
  }

  ASSERT(read_refcount(entry) == 0);
  ASSERT(entry->timer_id == 0);

  // free queued packets
  LIST_FOR_IN_SAFE(skb, &entry->pending_queue, list) {
    LIST_REMOVE(&entry->pending_queue, skb, list);
    skb_free(&skb);
  }

  netdev_putref(&entry->dev);
  kfree(entry);
}

static id_t arp_setup_cache_timeout_alarm(arp_entry_t *entry, int seconds) {
  if (seconds == 0)
    seconds = ARP_CACHE_TIMEOUT;

  if (entry->timer_id > 0) {
    // cancel existing timer
    id_t old_timer_id = entry->timer_id;
    entry->timer_id = 0;
    struct callback cb;
    if (alarm_unregister(old_timer_id, &cb) == 0) {
      arp_entry_t *old_entry = (arp_entry_t *)cb.args[0];
      arp_putref(&old_entry);
    }
  }

  alarm_t *alarm = alarm_alloc_relative(SEC_TO_NS(seconds), alarm_cb(arp_entry_timeout_cb, getref(entry)));
  entry->timer_id = alarm->id;
  id_t id = alarm_register(alarm);
  ASSERT(id > 0);
  return id;
}

static void arp_entry_timeout(__ref arp_entry_t *entry, id_t fired_timer_id) {
  mtx_lock(&arp_cache.lock);

  // check if this timer is still current (wasn't replaced)
  if (entry->timer_id != fired_timer_id) {
    // timer was replaced while this callback was queued, ignore it
    DPRINTF("ignoring stale timer callback (fired=%u, current=%u) for IP {:ip}\n",
            fired_timer_id, entry->timer_id, entry->ip_addr);
    mtx_unlock(&arp_cache.lock);
    arp_putref(&entry);
    return;
  }

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

      // reset timer for next retry (this updates timer_id)
      arp_setup_cache_timeout_alarm(entry, ARP_RETRY_INTERVAL);
    } else {
      // failed to resolve
      DPRINTF("failed to resolve IP {:ip} after %u retries\n", entry->ip_addr, entry->retries);
      entry->state = ARP_STATE_FAILED;

      // drop all pending packets
      LIST_FOR_IN_SAFE(skb, &entry->pending_queue, list) {
        LIST_REMOVE(&entry->pending_queue, skb, list);
        skb_free(&skb);
      }

      LIST_REMOVE(&arp_cache.entries, entry, link);
      arp_cache.num_entries--;
      entry->timer_id = 0;
    }
  } else if (entry->state == ARP_STATE_REACHABLE) {
    // entry expired, mark as stale
    entry->state = ARP_STATE_STALE;
    DPRINTF("entry for IP {:ip} marked stale\n", entry->ip_addr);
    entry->timer_id = 0;
  } else {
    // STALE or FAILED entries should not have active timers
    EPRINTF("WARNING: timer fired for entry in state %u (should not happen!)\n", entry->state);
    entry->timer_id = 0;
  }

  mtx_unlock(&arp_cache.lock);
  arp_putref(&entry);
}

//
// MARK: ARP Cache
//

__ref arp_entry_t *arp_cache_lookup(uint32_t ip_addr) {
  mtx_lock(&arp_cache.lock);

  arp_entry_t *entry;
  LIST_FOREACH(entry, &arp_cache.entries, link) {
    if (entry->ip_addr == ip_addr) {
      entry->last_used = clock_get_nanos();

      // refresh timer for reachable entries
      if (entry->state == ARP_STATE_REACHABLE && entry->timer_id > 0) {
        arp_setup_cache_timeout_alarm(entry, ARP_CACHE_TIMEOUT);
      }

      mtx_unlock(&arp_cache.lock);
      return getref(entry);
    }
  }

  mtx_unlock(&arp_cache.lock);
  return NULL;
}

__ref arp_entry_t *arp_cache_add(netdev_t *dev, uint32_t ip_addr, uint8_t *hw_addr) {
  ASSERT(dev != NULL);
  // hw_addr can be NULL for incomplete entries

  __ref arp_entry_t *entry = arp_cache_lookup(ip_addr);
  DPRINTF("arp_cache_add: IP {:ip} hw_addr={:mac} entry=%p\n",
          ip_addr, hw_addr ? hw_addr : (uint8_t[]){0,0,0,0,0,0}, entry);
  if (entry) {
    DPRINTF("  found existing entry: state=%u queue_empty=%d\n",
            entry->state, LIST_EMPTY(&entry->pending_queue));
    // update existing entry
    if (hw_addr && !eth_addr_is_zero(hw_addr)) {
      mtx_lock(&arp_cache.lock);
      eth_addr_copy(entry->hw_addr, hw_addr);
      entry->state = ARP_STATE_REACHABLE;
      entry->retries = 0;

      arp_setup_cache_timeout_alarm(entry, ARP_CACHE_TIMEOUT);
      mtx_unlock(&arp_cache.lock);

      // send queued packets
      size_t queued_count = 0;
      LIST_FOR_IN_SAFE(skb, &entry->pending_queue, list) {
        LIST_REMOVE(&entry->pending_queue, skb, list);
        queued_count++;

        DPRINTF("sending queued packet %zu for IP {:ip}: skb=%p len=%zu\n",
                queued_count, ip_addr, skb, skb->len);

        // dump first 60 bytes of packet before adding ethernet header
        {
          uint8_t *bytes = skb->data;
          size_t dump_len = skb->len < 60 ? skb->len : 60;
          DPRINTF("  packet before eth header (%zu bytes): ", dump_len);
          for (size_t i = 0; i < dump_len; i++) {
            kprintf("%02x ", bytes[i]);
            if ((i + 1) % 20 == 0) kprintf("\n    ");
          }
          kprintf("\n");
        }

        // add ethernet header
        if (eth_header(skb, entry->dev, ETH_P_IP, hw_addr, NULL) < 0) {
          EPRINTF("failed to add ethernet header to queued packet\n");
          skb_free(&skb);
          continue;
        }

        // dump packet after ethernet header is added
        {
          uint8_t *bytes = skb->data;
          size_t dump_len = skb->len < 60 ? skb->len : 60;
          DPRINTF("  final packet with eth header (%zu bytes total): ", skb->len);
          for (size_t i = 0; i < dump_len; i++) {
            kprintf("%02x ", bytes[i]);
            if ((i + 1) % 20 == 0) kprintf("\n    ");
          }
          kprintf("\n");
        }

        // transmit packet
        netdev_tx(entry->dev, skb);
        DPRINTF("transmitted queued packet %zu\n", queued_count);
      }
      if (queued_count > 0) {
        DPRINTF("sent %zu queued packets for IP {:ip}\n", queued_count, ip_addr);
      }
    }

    return moveref(entry);
  }

  DPRINTF("  creating new entry (lookup returned NULL)\n");
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
      arp_putref(&oldest);
    }
  }

  // create new entry
  entry = kmallocz(sizeof(arp_entry_t));
  ASSERT(entry != NULL);

  entry->ip_addr = ip_addr;
  entry->dev = netdev_getref(dev);
  entry->last_used = clock_get_nanos();
  initref(entry);

  if (hw_addr && !eth_addr_is_zero(hw_addr)) {
    eth_addr_copy(entry->hw_addr, hw_addr);
    entry->state = ARP_STATE_REACHABLE;
    arp_setup_cache_timeout_alarm(entry, ARP_CACHE_TIMEOUT);
  } else {
    entry->state = ARP_STATE_INCOMPLETE;
    arp_setup_cache_timeout_alarm(entry, ARP_INCOMPLETE_TIMEOUT);
  }

  LIST_ADD(&arp_cache.entries, entry, link);
  arp_cache.num_entries++;
  mtx_unlock(&arp_cache.lock);

  DPRINTF("added cache entry: IP {:ip} -> MAC {:mac} (state=%u)\n",
          ip_addr, hw_addr ? hw_addr : entry->hw_addr, entry->state);

  return getref(entry);
}

void arp_cache_update(uint32_t ip_addr, uint8_t *hw_addr) {
  arp_entry_t *entry = arp_cache_add(NULL, ip_addr, hw_addr);
  arp_putref(&entry);
}

void arp_cache_delete(uint32_t ip_addr) {
  mtx_lock(&arp_cache.lock);

  LIST_FOR_IN_SAFE(entry, &arp_cache.entries, link) {
    if (entry->ip_addr == ip_addr) {
      LIST_REMOVE(&arp_cache.entries, entry, link);
      arp_cache.num_entries--;
      arp_putref(&entry);
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
      arp_putref(&entry);
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
  int res = 0;

  arp_entry_t *entry = arp_cache_lookup(ip_addr); __ref
  if (!entry) {
    // create incomplete entry
    entry = arp_cache_add(dev, ip_addr, NULL);
    if (!entry) {
      goto_res(done, -ENOMEM);
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

    mtx_lock(&arp_cache.lock);
    entry->state = ARP_STATE_INCOMPLETE;
    entry->retries = 0;
    arp_setup_cache_timeout_alarm(entry, ARP_INCOMPLETE_TIMEOUT);
    mtx_unlock(&arp_cache.lock);

    // send ARP request
    struct in_ifaddr *ifa = LIST_FIRST(&dev->ip_addrs);
    if (ifa) {
      arp_send_request(dev, ifa->ifa_address, ip_addr);
    }
  }

  // queue packet for later transmission
  if (entry->state == ARP_STATE_INCOMPLETE) {
    LIST_ADD(&entry->pending_queue, skb, list);
    DPRINTF("queued packet for IP {:ip} (pending resolution)\n", ip_addr);
    goto_res(done, 0); // packet queued successfully
  }

  // entry became reachable between lookup and queue
  // this can happen if ARP reply arrives very quickly
  if (entry->state == ARP_STATE_REACHABLE) {
    DPRINTF("ARP resolved while queuing for IP {:ip}, adding eth header\n", ip_addr);
    int ret = eth_header(skb, dev, ETH_P_IP, entry->hw_addr, NULL);
    if (ret < 0) {
      skb_free(&skb);
      goto_res(done, ret);
    }
    goto_res(done, 1); // signal caller to transmit immediately
  }

  // entry is in unexpected state (e.g., FAILED)
  DPRINTF("entry for IP {:ip} in unexpected state %u\n", ip_addr, entry->state);

LABEL(done);
  arp_putref(&entry);
  return res;
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
    arp_entry_t *entry = arp_cache_add(dev, sip, arp->eth_ipv4.ar_sha);
    arp_putref(&entry);
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

  arp_softirq_chan = chan_alloc(32, sizeof(arp_timer_event_t), 0, "arp_softirq");
  ASSERT(arp_softirq_chan != NULL);

  // create softirq handler process
  __ref proc_t *softirq_proc = proc_alloc_new(getref(curproc->creds));
  arp_softirq_pid = softirq_proc->pid;
  proc_setup_add_thread(softirq_proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  proc_setup_entry(softirq_proc, (uintptr_t) arp_softirq_handler, 0);
  proc_setup_name(softirq_proc, cstr_make("arp_softirq"));
  proc_finish_setup_and_submit_all(moveref(softirq_proc));

  netdev_add_packet_type(&arp_packet_type);
  DPRINTF("ARP protocol initialized (cache size: %zu)\n", arp_cache.max_entries);
}
MODULE_INIT(arp_init);
