//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#include <kernel/net/skbuff.h>

#include <kernel/mm.h>
#include <kernel/mm/pool.h>
#include <kernel/clock.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG skbuff
#include <kernel/log.h>

// default buffer sizes
#define SKB_DEFAULT_HEADROOM 64
#define SKB_DEFAULT_SIZE     1536  // ethernet MTU + headers

struct skb_data {
  _refcount;
  size_t size;          // total buffer size
  uint8_t buffer[];     // buffer data
};

// memory pools
static pool_t *skb_pool;       // pool for sk_buff structures
static pool_t *skb_data_pool;  // pool for skb_data buffers

static void skb_init_pools() {
  skb_pool = pool_create("skb", pool_sizes(sizeof(sk_buff_t)), 0);
  if (!skb_pool) {
    panic("skb_init_pools: failed to create skb pool");
  }

  // common skb_data sizes: header + 1500, 2048, 4096, 9000 (jumbo frames)
  size_t default_size = sizeof(skb_data_t) + SKB_DEFAULT_SIZE + SKB_DEFAULT_HEADROOM;
  skb_data_pool = pool_create("skb_data", pool_sizes(
    default_size,
    sizeof(skb_data_t) + 2048,
    sizeof(skb_data_t) + 4096,
    sizeof(skb_data_t) + 9000
  ), 0);
  if (!skb_data_pool) {
    panic("skb_init_pools: failed to create skb_data pool");
  }

  // preload some buffers for the default size
  pool_preload_cache(skb_pool, sizeof(sk_buff_t), 256);
  pool_preload_cache(skb_data_pool, default_size, 256);
}
STATIC_INIT(skb_init_pools);

//
// MARK: Socket Buffer API
//

static __ref skb_data_t *skb_data_alloc(size_t size) {
  size_t total_size = sizeof(skb_data_t) + size;
  skb_data_t *buf = pool_alloc(skb_data_pool, total_size);
  if (!buf) {
    return NULL;
  }

  memset(buf, 0, total_size);
  initref(buf);
  buf->size = size;
  return buf;
}

sk_buff_t *skb_alloc(size_t size) {
  if (size == 0) {
    size = SKB_DEFAULT_SIZE;
  }

  sk_buff_t *skb = pool_alloc(skb_pool, sizeof(sk_buff_t));
  if (!skb) {
    return NULL;
  }

  memset(skb, 0, sizeof(sk_buff_t));

  // allocate the skb_data buffer
  size_t total_size = size + SKB_DEFAULT_HEADROOM;
  skb_data_t *buf = skb_data_alloc(total_size);
  if (!buf) {
    pool_free(skb_pool, skb);
    return NULL;
  }

  // set up pointers into the buffer
  skb->head = buf->buffer;
  skb->data = buf->buffer + SKB_DEFAULT_HEADROOM;
  skb->tail = skb->data;
  skb->end = buf->buffer + total_size;

  skb->len = 0;
  skb->data_len = 0;

  skb->buf = moveref(buf);
  skb->dev = NULL;

  skb->protocol = 0;
  skb->pkt_type = PACKET_HOST;

  skb->network_header = NULL;
  skb->transport_header = NULL;

  skb->csum = 0;
  skb->ip_summed = CHECKSUM_NONE;

  skb->timestamp = clock_get_nanos();
  return skb;
}

static void skb_data_free(skb_data_t *buf) {
  pool_free(skb_data_pool, buf);
}

void skb_free(sk_buff_t **skbp) {
  sk_buff_t *skb = moveptr(*skbp);
  if (!skb) {
    return;
  }

  putref(&skb->buf, skb_data_free);
  pool_free(skb_pool, skb);
}

sk_buff_t *skb_clone(sk_buff_t *skb) {
  ASSERT(skb != NULL);

  // grab reference to underlying buffer
  getref(skb->buf);

  sk_buff_t *clone = pool_alloc(skb_pool, sizeof(sk_buff_t));
  if (!clone) {
    putref(&skb->buf, skb_data_free);
    return NULL;
  }

  // shallow copy skb
  memcpy(clone, skb, sizeof(sk_buff_t));

  // clear list pointers to avoid corrupting lists
  LIST_ENTRY_INIT(&clone->list);

  return clone;
}

sk_buff_t *skb_copy(sk_buff_t *skb) {
  ASSERT(skb != NULL);

  // save critical pointers before allocation (which might corrupt the original skb)
  uint8_t *orig_head = skb->head;
  uint8_t *orig_data = skb->data;
  uint8_t *orig_tail = skb->tail;
  uint8_t *orig_end = skb->end;
  uint8_t *orig_network_header = skb->network_header;
  uint8_t *orig_transport_header = skb->transport_header;
  size_t orig_len = skb->len;
  size_t orig_data_len = skb->data_len;
  uint16_t orig_protocol = skb->protocol;
  uint16_t orig_pkt_type = skb->pkt_type;
  uint16_t orig_csum = skb->csum;
  uint8_t orig_ip_summed = skb->ip_summed;

  size_t buffer_size = orig_end - orig_head;
  sk_buff_t *new_skb = skb_alloc(buffer_size - SKB_DEFAULT_HEADROOM);
  if (!new_skb) {
    return NULL;
  }

  // check if we got the same skb back (pool corruption)
  if (new_skb == skb) {
    DPRINTF("skb_copy: pool returned the same skb! (skb=%p)\n", skb);
    return NULL;
  }

  size_t head_offset = orig_data - orig_head;
  size_t total_size = orig_tail - orig_head;
  new_skb->data = new_skb->head + head_offset;
  new_skb->tail = new_skb->data + orig_len;
  memcpy(new_skb->head, orig_head, total_size);

  // copy metadata
  new_skb->len = orig_len;
  new_skb->data_len = orig_data_len;
  new_skb->protocol = orig_protocol;
  new_skb->pkt_type = orig_pkt_type;
  new_skb->csum = orig_csum;
  new_skb->ip_summed = orig_ip_summed;

  // adjust header pointers if they were set
  if (orig_network_header && orig_network_header >= orig_head && orig_network_header < orig_end) {
    size_t offset = orig_network_header - orig_head;
    new_skb->network_header = new_skb->head + offset;
  }
  if (orig_transport_header && orig_transport_header >= orig_head && orig_transport_header < orig_end) {
    size_t offset = orig_transport_header - orig_head;
    new_skb->transport_header = new_skb->head + offset;
  }

  return new_skb;
}

size_t skb_headroom(sk_buff_t *skb) {
  ASSERT(skb != NULL);
  return skb->data - skb->head;
}

size_t skb_tailroom(sk_buff_t *skb) {
  ASSERT(skb != NULL);
  return skb->end - skb->tail;
}

void *skb_put_data(sk_buff_t *skb, size_t len) {
  ASSERT(skb != NULL);
  if (skb->tail + len > skb->end) {
    panic("skb_put_data: buffer overflow");
  }

  uint8_t *ptr = skb->tail;
  skb->tail += len;
  skb->len += len;
  return ptr;
}

void *skb_push(sk_buff_t *skb, size_t len) {
  ASSERT(skb != NULL);
  if (skb->data - len < skb->head) {
    panic("skb_push: buffer underflow");
  }

  skb->data -= len;
  skb->len += len;
  return skb->data;
}

void *skb_pull(sk_buff_t *skb, size_t len) {
  ASSERT(skb != NULL);
  if (len > skb->len) {
    panic("skb_pull: not enough data");
  }

  skb->data += len;
  skb->len -= len;
  return skb->data;
}

void skb_trim(sk_buff_t *skb, size_t len) {
  ASSERT(skb != NULL);
  if (len > skb->len) {
    return;
  }

  skb->len = len;
  skb->tail = skb->data + len;
}

size_t skb_copy_from_iovec(sk_buff_t *skb, const struct iovec *iov, size_t offset, size_t len) {
  ASSERT(skb != NULL);
  ASSERT(iov != NULL);

  if (offset >= iov->iov_len) {
    return 0;
  }

  size_t available = iov->iov_len - offset;
  size_t to_copy = len < available ? len : available;

  if (to_copy > skb_tailroom(skb)) {
    to_copy = skb_tailroom(skb);
  }

  if (to_copy == 0) {
    return 0;
  }

  uint8_t *dst = skb_put_data(skb, to_copy);
  memcpy(dst, (uint8_t *)iov->iov_base + offset, to_copy);
  return to_copy;
}

size_t skb_copy_to_iovec(sk_buff_t *skb, struct iovec *iov, size_t offset, size_t len, bool consume) {
  ASSERT(skb != NULL);
  ASSERT(iov != NULL);

  if (offset >= iov->iov_len) {
    return 0;
  }

  size_t available_iov = iov->iov_len - offset;
  size_t available_skb = skb->len;
  size_t to_copy = len;

  if (to_copy > available_iov) {
    to_copy = available_iov;
  }
  if (to_copy > available_skb) {
    to_copy = available_skb;
  }

  if (to_copy == 0) {
    return 0;
  }

  memcpy((uint8_t *)iov->iov_base + offset, skb->data, to_copy);

  if (consume) {
    skb->data += to_copy;
    skb->len -= to_copy;
  }

  return to_copy;
}
