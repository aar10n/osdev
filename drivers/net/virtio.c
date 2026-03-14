//
// Created by Aaron Gill-Braun on 2025-10-02.
//

#include "virtio.h"

#include <kernel/device.h>
#include <kernel/irq.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <kernel/bus/pci.h>
#include <kernel/bus/pci_hw.h>
#include <kernel/net/eth.h>
#include <kernel/net/netdev.h>
#include <kernel/net/skbuff.h>

#include <linux/sockios.h>
#include <linux/if.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG virtio
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("virtio: ERROR: " fmt, ##__VA_ARGS__)

#define VIRTIO_NET_QUEUE_RX         0
#define VIRTIO_NET_QUEUE_TX         1
#define VIRTIO_NET_DEFAULT_QUEUE_SZ 256
#define VIRTIO_NET_MIN_QUEUE_SIZE   2
#define VIRTIO_NET_RX_BUFFER_SIZE   2048
#define VIRTIO_NET_TX_BUFFER_SIZE   2048

// fallback address used when host does not advertise mac
static const uint8_t virtio_net_fallback_mac[ETH_ALEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x58 };

// tracks a contiguous memory mapping shared with the device
typedef struct virtio_mem_region {
  uintptr_t vaddr;
  uint64_t paddr;
  size_t size;
} virtio_mem_region_t;

typedef struct virtio_desc_state {
  void *cookie;
  uint16_t chain_len;
  bool in_use;
} virtio_desc_state_t;

// per-queue tracking for descriptor tables and notify region
typedef struct virtio_queue {
  uint16_t index;
  uint16_t size;
  bool use_free_list;

  virtq_desc_t *desc;
  virtq_avail_t *avail;
  virtq_used_t *used;

  virtio_mem_region_t desc_region;
  virtio_mem_region_t avail_region;
  virtio_mem_region_t used_region;

  virtio_desc_state_t *desc_state;
  uint16_t *free_list;
  uint16_t num_free;
  uint16_t last_used_idx;

  volatile uint16_t *notify;
} virtio_queue_t;

typedef struct virtio_net_rx_buffer {
  uint8_t *data;
  size_t capacity;
} virtio_net_rx_buffer_t;

typedef struct virtio_net_tx_ctx {
  uint16_t data_desc;
  bool in_use;
} virtio_net_tx_ctx_t;

typedef struct virtio_net_priv {
  pci_device_t *pci_dev;                    // backing pci device
  netdev_t *netdev;                         // registered netdev handle

  volatile virtio_pci_common_cfg_t *common_cfg; // mapped common config
  volatile uint8_t *isr_status;                 // isr status byte
  volatile uint8_t *notify_base;                // notify base pointer
  volatile virtio_net_config_t *device_cfg;     // device specific config

  uint32_t notify_off_multiplier;          // notify multiplier from capability
  uint64_t device_features;                // raw device features
  uint64_t driver_features;                // negotiated feature mask

  uint8_t irq;                             // assigned interrupt line
  bool modern;                             // indicates modern pci interface
  bool queues_initialized;                 // queues configured and live
  bool link_up;                            // cached link status

  virtio_queue_t rxq;                      // receive queue tracking
  virtio_queue_t txq;                      // transmit queue tracking

  virtio_net_rx_buffer_t *rx_buffers;      // receive buffer pool
  virtio_net_tx_ctx_t *tx_ctx;             // transmit descriptor context
  virtio_net_hdr_v1_t *tx_hdrs;            // transmit header storage
  void **tx_buffers;                       // transmit payload storage

  mtx_t tx_lock;                           // guards tx ring operations
  struct virtio_net_priv *next_on_irq;     // linked list for shared IRQ
} virtio_net_priv_t;

static int virtio_net_next_unit = 0;
static virtio_net_priv_t *virtio_irq_devices[256] = {NULL};

// pci capability helpers
static pci_bar_t *virtio_net_get_bar(virtio_net_priv_t *priv, uint8_t index);
static void *virtio_net_map_cap(virtio_net_priv_t *priv, const virtio_pci_cap_t *cap, const char *name);
static int virtio_net_discover_caps(virtio_net_priv_t *priv);
static void virtio_net_reset_device(virtio_net_priv_t *priv);
static int virtio_net_negotiate_features(virtio_net_priv_t *priv);
static uint16_t virtio_round_down_pow2(uint16_t value);
static int virtio_alloc_region(virtio_mem_region_t *region, size_t size, const char *name);
static void virtio_free_region(virtio_mem_region_t *region);
static int virtio_net_setup_queue(virtio_net_priv_t *priv, virtio_queue_t *vq, uint16_t index, uint16_t requested_size, bool use_free_list, const char *name);
static void virtio_net_free_queue(virtio_net_priv_t *priv, virtio_queue_t *vq);
static int virtio_net_setup_queues(virtio_net_priv_t *priv);
static int virtio_net_init_tx_resources(virtio_net_priv_t *priv);
static void virtio_net_free_tx_resources(virtio_net_priv_t *priv);
static void virtio_net_free_rx_resources(virtio_net_priv_t *priv);
static int virtio_net_post_rx_buffer(virtio_net_priv_t *priv, uint16_t desc_idx);
static int virtio_net_fill_rx_queue(virtio_net_priv_t *priv);
static void virtio_net_handle_rx(virtio_net_priv_t *priv);
static void virtio_net_handle_config_change(virtio_net_priv_t *priv);
static void virtio_net_process_tx_completions_locked(virtio_net_priv_t *priv);
static int virtqueue_alloc_desc(virtio_queue_t *vq, uint16_t *idx);
static void virtqueue_free_desc(virtio_queue_t *vq, uint16_t idx);
static void virtqueue_free_chain(virtio_queue_t *vq, uint16_t head);
static void virtqueue_submit(virtio_queue_t *vq, uint16_t head);
static void virtqueue_notify(virtio_queue_t *vq);
static void virtio_net_cleanup(virtio_net_priv_t *priv);

static int virtio_net_open(netdev_t *dev);
static int virtio_net_close(netdev_t *dev);
static int virtio_net_start_tx(netdev_t *dev, sk_buff_t *skb);
static void virtio_net_get_stats(netdev_t *dev, struct netdev_stats *stats);
static void virtio_net_irq_handler(struct trapframe *frame);
static bool virtio_net_check_device(struct device_driver *drv, struct device *dev);
static int virtio_net_setup_device(struct device *dev);
static int virtio_net_remove_device(struct device *dev);

static uint16_t virtio_round_down_pow2(uint16_t value) {
  if (value <= 1) {
    return 1;
  }
  uint16_t result = 1;
  while ((uint16_t)(result << 1) != 0 && (uint16_t)(result << 1) <= value) {
    result <<= 1;
  }
  return result;
}

// allocate and map shared virtqueue memory
static int virtio_alloc_region(virtio_mem_region_t *region, size_t size, const char *name) {
  size_t total_size = align(size, PAGE_SIZE);
  __ref page_t *pages = alloc_pages(SIZE_TO_PAGES(total_size));
  if (!pages) {
    EPRINTF("failed to allocate pages for %s\n", name);
    return -ENOMEM;
  }

  uintptr_t vaddr = vmap_pages(moveref(pages), 0, total_size, VM_RDWR | VM_NOCACHE, name);
  if (vaddr == 0) {
    EPRINTF("failed to map region %s\n", name);
    return -ENOMEM;
  }

  memset((void *) vaddr, 0, total_size);
  region->vaddr = vaddr;
  region->size = total_size;
  region->paddr = virt_to_phys(vaddr);
  DPRINTF("mapped %s: virt=%p phys=0x%llx size=%zu\n", name, (void *) vaddr,
          (unsigned long long) region->paddr, total_size);
  return 0;
}

static void virtio_free_region(virtio_mem_region_t *region) {
  if (region->vaddr && region->size) {
    DPRINTF("unmapping region virt=%p size=%zu\n", (void *) region->vaddr, region->size);
    vmap_free(region->vaddr, region->size);
  }
  region->vaddr = 0;
  region->paddr = 0;
  region->size = 0;
}

static pci_bar_t *virtio_net_get_bar(virtio_net_priv_t *priv, uint8_t index) {
  pci_bar_t *bar = priv->pci_dev->bars;
  while (bar) {
    if (bar->num == index) {
      return bar;
    }
    bar = bar->next;
  }
  return NULL;
}

static void *virtio_net_map_cap(virtio_net_priv_t *priv, const virtio_pci_cap_t *cap, const char *name) {
  pci_bar_t *bar = virtio_net_get_bar(priv, cap->bar);
  if (!bar) {
    EPRINTF("missing BAR%u for capability\n", cap->bar);
    return NULL;
  }
  if (bar->kind != 0) {
    EPRINTF("BAR%u is not memory mapped\n", cap->bar);
    return NULL;
  }

  if (bar->virt_addr == 0) {
    size_t map_size = align(bar->size, PAGE_SIZE);
    uintptr_t mapped = vmap_phys(bar->phys_addr, 0, map_size, VM_RDWR | VM_NOCACHE, name);
    if (mapped == 0) {
      EPRINTF("failed to map BAR%u\n", cap->bar);
      return NULL;
    }
    bar->virt_addr = mapped;
  }

  if ((uint64_t) cap->offset + cap->length > bar->size) {
    EPRINTF("capability region outside BAR%u\n", cap->bar);
    return NULL;
  }

  return (void *)(uintptr_t)(bar->virt_addr + cap->offset);
}

// enumerate virtio pci capabilities and map required regions
static int virtio_net_discover_caps(virtio_net_priv_t *priv) {
  pci_device_t *pci_dev = priv->pci_dev;
  bool have_common = false;
  bool have_notify = false;
  bool have_isr = false;
  bool have_device = false;

  priv->notify_off_multiplier = 1;

  DPRINTF("discovering capabilities for %02x:%02x.%x\n",
          pci_dev->bus, pci_dev->device, pci_dev->function);

  for (pci_cap_t *cap = pci_dev->caps; cap; cap = cap->next) {
    if (cap->id != 0x09) {
      continue;
    }

    const virtio_pci_cap_t *cfg = (const virtio_pci_cap_t *) cap->offset;
    DPRINTF("  cap type=%u bar=%u offset=0x%x length=0x%x\n",
            cfg->cfg_type, cfg->bar, cfg->offset, cfg->length);
    switch (cfg->cfg_type) {
      case VIRTIO_PCI_CAP_COMMON_CFG: {
        void *ptr = virtio_net_map_cap(priv, cfg, "virtio-net-common");
        if (!ptr) {
          return -ENODEV;
        }
        priv->common_cfg = ptr;
        have_common = true;
        break;
      }
      case VIRTIO_PCI_CAP_NOTIFY_CFG: {
        const virtio_pci_notify_cap_t *notify_cap = (const virtio_pci_notify_cap_t *) cfg;
        void *ptr = virtio_net_map_cap(priv, cfg, "virtio-net-notify");
        if (!ptr) {
          return -ENODEV;
        }
        priv->notify_base = ptr;
        priv->notify_off_multiplier = notify_cap->notify_off_multiplier ? notify_cap->notify_off_multiplier : 1;
        have_notify = true;
        break;
      }
      case VIRTIO_PCI_CAP_ISR_CFG: {
        void *ptr = virtio_net_map_cap(priv, cfg, "virtio-net-isr");
        if (!ptr) {
          return -ENODEV;
        }
        priv->isr_status = ptr;
        have_isr = true;
        break;
      }
      case VIRTIO_PCI_CAP_DEVICE_CFG: {
        void *ptr = virtio_net_map_cap(priv, cfg, "virtio-net-device");
        if (!ptr) {
          return -ENODEV;
        }
        priv->device_cfg = ptr;
        have_device = true;
        break;
      }
      default:
        break;
    }
  }

  if (!have_common || !have_notify || !have_isr || !have_device) {
    EPRINTF("missing required virtio capabilities (common=%d notify=%d isr=%d device=%d)\n",
            have_common, have_notify, have_isr, have_device);
    return -ENODEV;
  }

  DPRINTF("mapped required capabilities (notify mult=%u)\n", priv->notify_off_multiplier);
  return 0;
}

static void virtio_net_reset_device(virtio_net_priv_t *priv) {
  if (!priv->common_cfg) {
    return;
  }
  priv->common_cfg->device_status = 0;
  barrier();
}

// negotiate virtio feature bits and cache device config
static int virtio_net_negotiate_features(virtio_net_priv_t *priv) {
  if (!priv->common_cfg) {
    return -ENODEV;
  }

  priv->common_cfg->device_feature_select = 0;
  uint64_t device_features = priv->common_cfg->device_feature;
  priv->common_cfg->device_feature_select = 1;
  device_features |= ((uint64_t) priv->common_cfg->device_feature) << 32;
  priv->device_features = device_features;

  if (!(device_features & (1ULL << VIRTIO_F_VERSION_1))) {
    EPRINTF("device does not support VirtIO 1.0\n");
    return -ENOTSUP;
  }

  uint64_t driver_features = (1ULL << VIRTIO_F_VERSION_1);
  if (device_features & (1ULL << VIRTIO_NET_F_MAC)) {
    driver_features |= (1ULL << VIRTIO_NET_F_MAC);
  }
  if (device_features & (1ULL << VIRTIO_NET_F_STATUS)) {
    driver_features |= (1ULL << VIRTIO_NET_F_STATUS);
  }
  if (device_features & (1ULL << VIRTIO_NET_F_MTU)) {
    driver_features |= (1ULL << VIRTIO_NET_F_MTU);
  }

  priv->common_cfg->driver_feature_select = 0;
  priv->common_cfg->driver_feature = (uint32_t) driver_features;
  priv->common_cfg->driver_feature_select = 1;
  priv->common_cfg->driver_feature = (uint32_t) (driver_features >> 32);

  priv->common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
  if (!(priv->common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
    EPRINTF("device rejected negotiated features\n");
    return -EIO;
  }

  priv->driver_features = driver_features;

  DPRINTF("device features=0x%llx driver features=0x%llx\n",
          (unsigned long long) device_features, (unsigned long long) driver_features);

  if (driver_features & (1ULL << VIRTIO_NET_F_MAC)) {
    memcpy(priv->netdev->dev_addr, priv->device_cfg->mac, ETH_ALEN);
  } else {
    memcpy(priv->netdev->dev_addr, virtio_net_fallback_mac, ETH_ALEN);
  }
  priv->netdev->addr_len = ETH_ALEN;

  DPRINTF("mac {:mac}\n", priv->netdev->dev_addr);

  if (driver_features & (1ULL << VIRTIO_NET_F_MTU)) {
    uint16_t mtu = priv->device_cfg->mtu;
    if (mtu >= ETH_ZLEN && mtu <= ETH_DATA_LEN) {
      priv->netdev->mtu = mtu;
    }
  }

  if (driver_features & (1ULL << VIRTIO_NET_F_STATUS)) {
    uint16_t status = priv->device_cfg->status;
    priv->link_up = (status & VIRTIO_NET_S_LINK_UP) != 0;
    DPRINTF("device status=0x%04x, link=%s\n", status, priv->link_up ? "up" : "down");
  } else {
    priv->link_up = true;
  }

  priv->link_up = true;

  return 0;
}

// configure a single virtqueue and map its rings
static int virtio_net_setup_queue(virtio_net_priv_t *priv, virtio_queue_t *vq, uint16_t index,
                                  uint16_t requested_size, bool use_free_list, const char *name) {
  if (!priv->common_cfg || !priv->notify_base) {
    return -ENODEV;
  }

  memset(vq, 0, sizeof(*vq));
  vq->index = index;
  vq->use_free_list = use_free_list;

  priv->common_cfg->queue_select = index;
  uint16_t max_size = priv->common_cfg->queue_size;
  if (max_size == 0) {
    EPRINTF("queue %u not available\n", index);
    return -ENODEV;
  }

  uint16_t actual_size = requested_size ? min(requested_size, max_size) : max_size;
  if (!is_pow2(actual_size)) {
    actual_size = virtio_round_down_pow2(actual_size);
  }
  if (actual_size < VIRTIO_NET_MIN_QUEUE_SIZE) {
    actual_size = VIRTIO_NET_MIN_QUEUE_SIZE;
  }
  if (actual_size > max_size) {
    actual_size = max_size;
  }
  vq->size = actual_size;

  vq->desc_state = kmallocz(sizeof(virtio_desc_state_t) * vq->size);
  if (!vq->desc_state) {
    return -ENOMEM;
  }

  if (use_free_list) {
    vq->free_list = kmalloc(sizeof(uint16_t) * vq->size);
    if (!vq->free_list) {
      kfree(vq->desc_state);
      vq->desc_state = NULL;
      return -ENOMEM;
    }
    for (uint16_t i = 0; i < vq->size; i++) {
      vq->free_list[i] = vq->size - 1 - i;
    }
    vq->num_free = vq->size;
  }

  char region_name[32];
  ksnprintf(region_name, sizeof(region_name), "%s-desc", name);
  int ret = virtio_alloc_region(&vq->desc_region, sizeof(virtq_desc_t) * vq->size, region_name);
  if (ret < 0) {
    virtio_net_free_queue(priv, vq);
    return ret;
  }

  ksnprintf(region_name, sizeof(region_name), "%s-avail", name);
  ret = virtio_alloc_region(&vq->avail_region,
                            sizeof(virtq_avail_t) + (sizeof(uint16_t) * vq->size + sizeof(uint16_t)),
                            region_name);
  if (ret < 0) {
    virtio_net_free_queue(priv, vq);
    return ret;
  }

  ksnprintf(region_name, sizeof(region_name), "%s-used", name);
  ret = virtio_alloc_region(&vq->used_region,
                            sizeof(virtq_used_t) + (sizeof(virtq_used_elem_t) * vq->size + sizeof(uint16_t)),
                            region_name);
  if (ret < 0) {
    virtio_net_free_queue(priv, vq);
    return ret;
  }

  vq->desc = (virtq_desc_t *) vq->desc_region.vaddr;
  vq->avail = (virtq_avail_t *) vq->avail_region.vaddr;
  vq->used = (virtq_used_t *) vq->used_region.vaddr;
  vq->last_used_idx = 0;

  priv->common_cfg->queue_select = index;
  priv->common_cfg->queue_size = vq->size;
  priv->common_cfg->queue_desc = vq->desc_region.paddr;
  priv->common_cfg->queue_driver = vq->avail_region.paddr;
  priv->common_cfg->queue_device = vq->used_region.paddr;
  priv->common_cfg->queue_msix_vector = 0xFFFF;

  uint16_t notify_off = priv->common_cfg->queue_notify_off;
  size_t notify_offset = (size_t) notify_off * priv->notify_off_multiplier;
  vq->notify = (volatile uint16_t *)(priv->notify_base + notify_offset);

  priv->common_cfg->queue_enable = 1;
  DPRINTF("queue %u configured size=%u desc=0x%llx avail=0x%llx used=0x%llx notify_off=%u\n",
          index, vq->size, (unsigned long long) vq->desc_region.paddr,
          (unsigned long long) vq->avail_region.paddr, (unsigned long long) vq->used_region.paddr,
          notify_off);
  return 0;
}

static void virtio_net_free_queue(virtio_net_priv_t *priv, virtio_queue_t *vq) {
  uint16_t index = vq->index;

  virtio_free_region(&vq->desc_region);
  virtio_free_region(&vq->avail_region);
  virtio_free_region(&vq->used_region);

  if (vq->free_list) {
    kfree(vq->free_list);
  }
  if (vq->desc_state) {
    kfree(vq->desc_state);
  }

  memset(vq, 0, sizeof(*vq));
  DPRINTF("queue %u released\n", index);
}

// ring the doorbell for the given queue
static void virtqueue_notify(virtio_queue_t *vq) {
  if (vq->notify) {
    *vq->notify = vq->index;
  }
}

static int virtqueue_alloc_desc(virtio_queue_t *vq, uint16_t *idx) {
  if (!vq->use_free_list || !vq->free_list || vq->num_free == 0) {
    return -ENOSPC;
  }
  vq->num_free--;
  *idx = vq->free_list[vq->num_free];
  return 0;
}

static void virtqueue_free_desc(virtio_queue_t *vq, uint16_t idx) {
  if (!vq->use_free_list || !vq->free_list) {
    return;
  }
  ASSERT(vq->num_free < vq->size);
  vq->free_list[vq->num_free++] = idx;
}

static void virtqueue_free_chain(virtio_queue_t *vq, uint16_t head) {
  uint16_t idx = head;
  while (true) {
    virtq_desc_t *desc = &vq->desc[idx];
    uint16_t next = desc->next;
    uint16_t flags = desc->flags;
    memset(desc, 0, sizeof(*desc));
    virtqueue_free_desc(vq, idx);
    if (!(flags & VIRTQ_DESC_F_NEXT)) {
      break;
    }
    idx = next;
  }
}

static void virtqueue_submit(virtio_queue_t *vq, uint16_t head) {
  uint16_t avail_idx = vq->avail->idx % vq->size;
  vq->avail->ring[avail_idx] = head;
  barrier();
  vq->avail->idx++;
}

// reclaim completed transmit descriptors
static void virtio_net_process_tx_completions_locked(virtio_net_priv_t *priv) {
  if (!priv->queues_initialized) {
    return;
  }

  virtio_queue_t *vq = &priv->txq;
  uint32_t completed = 0;
  while (vq->last_used_idx != vq->used->idx) {
    barrier();
    uint16_t used_idx = vq->last_used_idx % vq->size;
    virtq_used_elem_t *elem = &vq->used->ring[used_idx];
    uint16_t head = elem->id;

    virtqueue_free_chain(vq, head);
    if (priv->tx_ctx && head < priv->txq.size) {
      priv->tx_ctx[head].in_use = false;
      priv->tx_ctx[head].data_desc = 0;
    }
    if (vq->desc_state && head < priv->txq.size) {
      vq->desc_state[head].cookie = NULL;
      vq->desc_state[head].chain_len = 0;
      vq->desc_state[head].in_use = false;
    }

    vq->last_used_idx++;
    completed++;
  }

  if (completed) {
    DPRINTF("tx: reclaimed %u descriptors\n", completed);
  }
}

// prepare a single descriptor for device ownership
static int virtio_net_post_rx_buffer(virtio_net_priv_t *priv, uint16_t desc_idx) {
  virtio_net_rx_buffer_t *buf = &priv->rx_buffers[desc_idx];
  if (!buf->data) {
    buf->data = kmalloc(VIRTIO_NET_RX_BUFFER_SIZE);
    if (!buf->data) {
      return -ENOMEM;
    }
    buf->capacity = VIRTIO_NET_RX_BUFFER_SIZE;
  }

  virtq_desc_t *desc = &priv->rxq.desc[desc_idx];
  memset(desc, 0, sizeof(*desc));
  desc->addr = virt_to_phys(buf->data);
  desc->len = buf->capacity;
  desc->flags = VIRTQ_DESC_F_WRITE;
  desc->next = 0;

  if (priv->rxq.desc_state) {
    priv->rxq.desc_state[desc_idx].cookie = buf;
    priv->rxq.desc_state[desc_idx].chain_len = 1;
    priv->rxq.desc_state[desc_idx].in_use = true;
  }

  barrier();
  uint16_t avail_idx = priv->rxq.avail->idx % priv->rxq.size;
  priv->rxq.avail->ring[avail_idx] = desc_idx;
  barrier();
  priv->rxq.avail->idx++;
  return 0;
}

// populate the rx available ring with buffers
static int virtio_net_fill_rx_queue(virtio_net_priv_t *priv) {
  for (uint16_t i = 0; i < priv->rxq.size; i++) {
    int ret = virtio_net_post_rx_buffer(priv, i);
    if (ret < 0) {
      return ret;
    }
  }
  barrier();
  virtqueue_notify(&priv->rxq);
  DPRINTF("primed %u rx buffers (avail_idx=%u used_idx=%u last_used=%u)\n",
          priv->rxq.size, priv->rxq.avail->idx, priv->rxq.used->idx, priv->rxq.last_used_idx);
  return 0;
}

// process used descriptors from the receive queue
static void virtio_net_handle_rx(virtio_net_priv_t *priv) {
  if (!priv->queues_initialized) {
    return;
  }

  virtio_queue_t *vq = &priv->rxq;
  netdev_t *dev = priv->netdev;
  bool need_notify = false;

  DPRINTF("rx: checking queue (last_used=%u used=%u avail=%u)\n",
          vq->last_used_idx, vq->used->idx, vq->avail->idx);

  while (vq->last_used_idx != vq->used->idx) {
    barrier();
    uint16_t used_idx = vq->last_used_idx % vq->size;
    virtq_used_elem_t *elem = &vq->used->ring[used_idx];
    uint16_t head = elem->id;
    uint32_t len = elem->len;

    virtio_net_rx_buffer_t *buf = &priv->rx_buffers[head];
    if (!buf->data || len <= sizeof(virtio_net_hdr_v1_t)) {
      dev->stats.rx_errors++;
      DPRINTF("rx: descriptor %u short packet len=%u\n", head, len);
    } else {
      size_t packet_len = len - sizeof(virtio_net_hdr_v1_t);
      uint8_t *packet = buf->data + sizeof(virtio_net_hdr_v1_t);

      sk_buff_t *skb = skb_alloc(packet_len);
      if (skb) {
        memcpy(skb_put_data(skb, packet_len), packet, packet_len);
        DPRINTF("rx: packet len=%zu desc=%u\n", packet_len, head);
        netdev_rx(dev, skb);
      } else {
        dev->stats.rx_dropped++;
        DPRINTF("rx: dropped packet len=%zu desc=%u (skb alloc)\n", packet_len, head);
      }
    }

    if (virtio_net_post_rx_buffer(priv, head) == 0) {
      need_notify = true;
    }

    vq->last_used_idx++;
  }

  if (need_notify) {
    virtqueue_notify(vq);
  }
}

// re-read device config on config interrupt
static void virtio_net_handle_config_change(virtio_net_priv_t *priv) {
  if (!(priv->driver_features & (1ULL << VIRTIO_NET_F_STATUS)) || !priv->device_cfg) {
    return;
  }

  uint16_t status = priv->device_cfg->status;
  bool link_now = (status & VIRTIO_NET_S_LINK_UP) != 0;
  if (link_now != priv->link_up) {
    DPRINTF("link %s\n", link_now ? "up" : "down");
    priv->link_up = link_now;
  }
}

// release queue state and buffers
static void virtio_net_cleanup(virtio_net_priv_t *priv) {
  DPRINTF("tearing down queues\n");
  virtio_net_free_tx_resources(priv);
  virtio_net_free_rx_resources(priv);
  virtio_net_free_queue(priv, &priv->txq);
  virtio_net_free_queue(priv, &priv->rxq);
  priv->queues_initialized = false;
  priv->device_features = 0;
  priv->driver_features = 0;
  priv->link_up = false;
}

// configure both rx and tx virtqueues
static int virtio_net_setup_queues(virtio_net_priv_t *priv) {
  virtio_net_cleanup(priv);

  int ret = virtio_net_setup_queue(priv, &priv->rxq, VIRTIO_NET_QUEUE_RX,
                                   VIRTIO_NET_DEFAULT_QUEUE_SZ, false, "virtio-net-rx");
  if (ret < 0) {
    return ret;
  }

  ret = virtio_net_setup_queue(priv, &priv->txq, VIRTIO_NET_QUEUE_TX,
                               VIRTIO_NET_DEFAULT_QUEUE_SZ, true, "virtio-net-tx");
  if (ret < 0) {
    virtio_net_free_queue(priv, &priv->rxq);
    return ret;
  }

  priv->rx_buffers = kmallocz(sizeof(virtio_net_rx_buffer_t) * priv->rxq.size);
  if (!priv->rx_buffers) {
    virtio_net_cleanup(priv);
    return -ENOMEM;
  }

  priv->tx_ctx = kmallocz(sizeof(virtio_net_tx_ctx_t) * priv->txq.size);
  if (!priv->tx_ctx) {
    virtio_net_cleanup(priv);
    return -ENOMEM;
  }

  priv->tx_hdrs = kmallocz(sizeof(virtio_net_hdr_v1_t) * priv->txq.size);
  if (!priv->tx_hdrs) {
    virtio_net_cleanup(priv);
    return -ENOMEM;
  }

  priv->tx_buffers = kmallocz(sizeof(void *) * priv->txq.size);
  if (!priv->tx_buffers) {
    virtio_net_cleanup(priv);
    return -ENOMEM;
  }

  priv->queues_initialized = true;
  DPRINTF("queues ready (rx=%u tx=%u)\n", priv->rxq.size, priv->txq.size);
  return 0;
}

// populate backing buffers for transmit queue
static int virtio_net_init_tx_resources(virtio_net_priv_t *priv) {
  if (!priv->tx_buffers) {
    return -EINVAL;
  }

  for (uint16_t i = 0; i < priv->txq.size; i++) {
    if (!priv->tx_buffers[i]) {
      priv->tx_buffers[i] = kmalloc(VIRTIO_NET_TX_BUFFER_SIZE);
      if (!priv->tx_buffers[i]) {
        return -ENOMEM;
      }
    }
  }

  DPRINTF("tx buffers ready count=%u size=%u\n", priv->txq.size, VIRTIO_NET_TX_BUFFER_SIZE);
  return 0;
}

static void virtio_net_free_tx_resources(virtio_net_priv_t *priv) {
  if (priv->tx_buffers) {
    for (uint16_t i = 0; i < priv->txq.size; i++) {
      if (priv->tx_buffers[i]) {
        kfree(priv->tx_buffers[i]);
      }
    }
    kfree(priv->tx_buffers);
    priv->tx_buffers = NULL;
  }

  if (priv->tx_hdrs) {
    kfree(priv->tx_hdrs);
    priv->tx_hdrs = NULL;
  }

  if (priv->tx_ctx) {
    kfree(priv->tx_ctx);
    priv->tx_ctx = NULL;
  }
}

static void virtio_net_free_rx_resources(virtio_net_priv_t *priv) {
  if (priv->rx_buffers) {
    for (uint16_t i = 0; i < priv->rxq.size; i++) {
      if (priv->rx_buffers[i].data) {
        kfree(priv->rx_buffers[i].data);
        priv->rx_buffers[i].data = NULL;
      }
    }
    kfree(priv->rx_buffers);
    priv->rx_buffers = NULL;
  }
}

static int virtio_net_start_tx(netdev_t *dev, sk_buff_t *skb) {
  virtio_net_priv_t *priv = netdev_data(dev);

  if (!priv->queues_initialized) {
    skb_free(&skb);
    return -ENETDOWN;
  }

  size_t len = skb->len;
  if (len == 0) {
    skb_free(&skb);
    return 0;
  }
  if (len > VIRTIO_NET_TX_BUFFER_SIZE) {
    DPRINTF("tx: packet too large len=%zu\n", len);
    skb_free(&skb);
    return -EMSGSIZE;
  }

  mtx_spin_lock(&priv->tx_lock);
  virtio_net_process_tx_completions_locked(priv);

  if (priv->txq.num_free < 2) {
    DPRINTF("tx: ring full\n");
    mtx_spin_unlock(&priv->tx_lock);
    skb_free(&skb);
    return -EBUSY;
  }

  uint16_t hdr_desc = 0;
  uint16_t data_desc = 0;
  if (virtqueue_alloc_desc(&priv->txq, &hdr_desc) < 0) {
    DPRINTF("tx: failed to allocate header descriptor\n");
    mtx_spin_unlock(&priv->tx_lock);
    skb_free(&skb);
    return -EBUSY;
  }
  if (virtqueue_alloc_desc(&priv->txq, &data_desc) < 0) {
    DPRINTF("tx: failed to allocate data descriptor\n");
    virtqueue_free_desc(&priv->txq, hdr_desc);
    mtx_spin_unlock(&priv->tx_lock);
    skb_free(&skb);
    return -EBUSY;
  }

  virtio_net_hdr_v1_t *hdr = &priv->tx_hdrs[hdr_desc];
  memset(hdr, 0, sizeof(*hdr));

  virtq_desc_t *hdr_desc_ptr = &priv->txq.desc[hdr_desc];
  memset(hdr_desc_ptr, 0, sizeof(*hdr_desc_ptr));
  hdr_desc_ptr->addr = virt_to_phys(hdr);
  hdr_desc_ptr->len = sizeof(*hdr);
  hdr_desc_ptr->flags = VIRTQ_DESC_F_NEXT;
  hdr_desc_ptr->next = data_desc;

  void *buffer = priv->tx_buffers[data_desc];
  ASSERT(buffer != NULL);
  memcpy(buffer, skb->data, len);

  virtq_desc_t *data_desc_ptr = &priv->txq.desc[data_desc];
  memset(data_desc_ptr, 0, sizeof(*data_desc_ptr));
  data_desc_ptr->addr = virt_to_phys(buffer);
  data_desc_ptr->len = len;
  data_desc_ptr->flags = 0;
  data_desc_ptr->next = 0;

  priv->tx_ctx[hdr_desc].data_desc = data_desc;
  priv->tx_ctx[hdr_desc].in_use = true;

  if (priv->txq.desc_state) {
    priv->txq.desc_state[hdr_desc].cookie = &priv->tx_ctx[hdr_desc];
    priv->txq.desc_state[hdr_desc].chain_len = 2;
    priv->txq.desc_state[hdr_desc].in_use = true;
  }

  virtqueue_submit(&priv->txq, hdr_desc);
  virtqueue_notify(&priv->txq);

  mtx_spin_unlock(&priv->tx_lock);
  DPRINTF("tx: queued len=%zu hdr=%u data=%u\n", len, hdr_desc, data_desc);
  skb_free(&skb);
  return 0;
}

// transition device to operational state
static int virtio_net_open(netdev_t *dev) {
  virtio_net_priv_t *priv = netdev_data(dev);
  int ret;

  virtio_net_reset_device(priv);
  DPRINTF("init sequence start\n");
  priv->common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
  priv->common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

  ret = virtio_net_negotiate_features(priv);
  if (ret < 0) {
    EPRINTF("feature negotiation failed {:err}\n", ret);
    virtio_net_reset_device(priv);
    return ret;
  }

  priv->common_cfg->msix_config = 0xFFFF;

  ret = virtio_net_setup_queues(priv);
  if (ret < 0) {
    EPRINTF("queue setup failed {:err}\n", ret);
    virtio_net_reset_device(priv);
    return ret;
  }

  ret = virtio_net_init_tx_resources(priv);
  if (ret < 0) {
    EPRINTF("tx resource init failed {:err}\n", ret);
    virtio_net_cleanup(priv);
    virtio_net_reset_device(priv);
    return ret;
  }

  ret = virtio_net_fill_rx_queue(priv);
  if (ret < 0) {
    EPRINTF("rx queue fill failed {:err}\n", ret);
    virtio_net_cleanup(priv);
    virtio_net_reset_device(priv);
    return ret;
  }

  irq_enable_interrupt(priv->irq);
  priv->common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
  barrier();
  virtqueue_notify(&priv->rxq);

  DPRINTF("interface {:str} ready (mtu=%u, link=%s)\n",
          &dev->name, dev->mtu, priv->link_up ? "up" : "down");
  return 0;
}

static int virtio_net_close(netdev_t *dev) {
  virtio_net_priv_t *priv = netdev_data(dev);

  DPRINTF("stopping interface\n");
  irq_disable_interrupt(priv->irq);
  mtx_spin_lock(&priv->tx_lock);
  virtio_net_process_tx_completions_locked(priv);
  mtx_spin_unlock(&priv->tx_lock);

  virtio_net_cleanup(priv);
  virtio_net_reset_device(priv);
  DPRINTF("interface stopped\n");
  return 0;
}

static int virtio_net_ioctl(netdev_t *dev, unsigned long cmd, void *arg) {
  virtio_net_priv_t *priv = netdev_data(dev);
  struct ifreq *ifr = (struct ifreq *)arg;

  switch (cmd) {
    case SIOCGIFTXQLEN:
      ifr->ifr_qlen = priv->txq.size;
      return 0;
    case SIOCSIFTXQLEN:
    default:
      return -EOPNOTSUPP;
  }
}

static void virtio_net_get_stats(netdev_t *dev, struct netdev_stats *stats) {
  if (!stats) {
    return;
  }
  *stats = dev->stats;
}

// top level interrupt handler invoked from legacy irq
static void virtio_net_irq_handler(struct trapframe *frame) {
  uint8_t irq = (uint8_t)(frame->vector - 32);
  virtio_net_priv_t *priv = virtio_irq_devices[irq];

  while (priv) {
    if (priv->queues_initialized) {
      uint8_t isr = *priv->isr_status;
      if (isr != 0) {
        if (isr & 0x1) {
          virtio_net_handle_rx(priv);
          mtx_spin_lock(&priv->tx_lock);
          virtio_net_process_tx_completions_locked(priv);
          mtx_spin_unlock(&priv->tx_lock);
        }

        if (isr & 0x2) {
          virtio_net_handle_config_change(priv);
        }
      }
    }
    priv = priv->next_on_irq;
  }
}

static const struct netdev_ops virtio_net_ops = {
  .net_open = virtio_net_open,
  .net_close = virtio_net_close,
  .net_start_tx = virtio_net_start_tx,
  .net_ioctl = virtio_net_ioctl,
  .net_set_mac_addr = NULL,
  .net_get_stats = virtio_net_get_stats,
};

//
// MARK: Device Operations
//

static struct device_ops virtio_net_device_ops = {
  .d_open = NULL,
  .d_close = NULL,
  .d_read = NULL,
  .d_write = NULL,
  .d_getpage = NULL,
  .d_putpage = NULL,
};

//
// MARK: PCI Device Driver
//

static bool virtio_net_check_device(struct device_driver *drv, struct device *dev) {
  (void) drv;
  pci_device_t *pci_dev = dev->bus_device;
  if (!pci_dev) {
    return false;
  }
  if (pci_dev->vendor_id != VIRTIO_VENDOR_ID) {
    return false;
  }
  return pci_dev->device_id == VIRTIO_NET_DEVICE_ID_MODERN ||
         pci_dev->device_id == VIRTIO_NET_DEVICE_ID_LEGACY;
}

// bind the pci function and register a netdev instance
static int virtio_net_setup_device(struct device *dev) {
  pci_device_t *pci_dev = dev->bus_device;
  char name[16];
  ksnprintf(name, sizeof(name), "eth%d", virtio_net_next_unit++);

  netdev_t *ndev = netdev_alloc(str_from(name), sizeof(virtio_net_priv_t));
  if (!ndev) {
    return -ENOMEM;
  }

  virtio_net_priv_t *priv = netdev_data(ndev);
  priv->pci_dev = pci_dev;
  priv->netdev = ndev;
  priv->modern = true;
  priv->notify_off_multiplier = 1;

  DPRINTF("setting up %02x:%02x.%x as {:str}\n", pci_dev->bus, pci_dev->device,
          pci_dev->function, &ndev->name);

  struct pci_segment_group *seg = get_segment_group_for_bus_number(pci_dev->bus);
  if (seg) {
    struct pci_header *header = pci_device_address(seg, pci_dev->bus, pci_dev->device, pci_dev->function);
    header->command.mem_space = 1;
    header->command.bus_master = 1;
    header->command.int_disable = 0;
    DPRINTF("enabled bus mastering and interrupts\n");
  }

  int ret = virtio_net_discover_caps(priv);
  if (ret < 0) {
    netdev_putref(&ndev);
    return ret;
  }

  virtio_net_reset_device(priv);

  mtx_init(&priv->tx_lock, MTX_SPIN, "virtio_net_tx");

  ndev->type = ARPHRD_ETHER;
  ndev->flags = 0;
  ndev->mtu = 1500;
  ndev->addr_len = ETH_ALEN;
  memset(ndev->dev_addr, 0, sizeof(ndev->dev_addr));
  ndev->netdev_ops = &virtio_net_ops;

  priv->irq = pci_dev->int_line;
  DPRINTF("PCI interrupt line=%u, pin=%u\n", pci_dev->int_line, pci_dev->int_pin);

  if (priv->irq == 0 || priv->irq == 0xFF) {
    DPRINTF("Invalid IRQ %u, allocating hardware IRQ\n", priv->irq);
    int irq = irq_alloc_hardware_irqnum();
    if (irq < 0) {
      netdev_putref(&ndev);
      return irq;
    }
    priv->irq = (uint8_t) irq;
  }

  bool is_first_on_irq = (virtio_irq_devices[priv->irq] == NULL);

  priv->next_on_irq = virtio_irq_devices[priv->irq];
  virtio_irq_devices[priv->irq] = priv;

  if (is_first_on_irq) {
    ret = irq_register_handler(priv->irq, virtio_net_irq_handler, priv);
    if (ret < 0) {
      virtio_irq_devices[priv->irq] = priv->next_on_irq;
      netdev_putref(&ndev);
      return ret;
    }
  } else {
    DPRINTF("IRQ %u already has handler (shared interrupt)\n", priv->irq);
  }

  ret = netdev_register(ndev);
  if (ret < 0) {
    irq_unregister_handler(priv->irq);
    netdev_putref(&ndev);
    return ret;
  }

  pci_dev->registered = true;
  dev->data = ndev;
  DPRINTF("registered interface {:str} on %02x:%02x.%x (irq=%u)\n",
          &ndev->name, pci_dev->bus, pci_dev->device, pci_dev->function, priv->irq);
  return 0;
}

// unbind device and release all resources
static int virtio_net_remove_device(struct device *dev) {
  netdev_t *ndev = dev->data;
  if (!ndev) {
    return 0;
  }

  virtio_net_priv_t *priv = netdev_data(ndev);
  DPRINTF("removing interface {:str}\n", &ndev->name);
  irq_disable_interrupt(priv->irq);
  irq_unregister_handler(priv->irq);

  if (ndev->flags & NETDEV_UP) {
    virtio_net_close(ndev);
  } else {
    virtio_net_cleanup(priv);
    virtio_net_reset_device(priv);
  }

  netdev_unregister(ndev);
  netdev_putref(&ndev);
  dev->data = NULL;
  DPRINTF("interface removed\n");
  return 0;
}

static device_driver_t virtio_net_driver = {
  .name = "virtio-net",
  .data = NULL,
  .ops = &virtio_net_device_ops,
  .f_ops = NULL,
  .check_device = virtio_net_check_device,
  .setup_device = virtio_net_setup_device,
  .remove_device = virtio_net_remove_device,
};

static void virtio_net_init_module(void) {
  if (register_driver("pci", &virtio_net_driver) < 0) {
    panic("virtio-net: failed to register driver");
  }
}
MODULE_INIT(virtio_net_init_module);
