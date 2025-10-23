//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#include <kernel/net/skbuff.h>

#include <kernel/mm.h>
#include <kernel/clock.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("skbuff: " fmt, ##__VA_ARGS__)

// default buffer sizes
#define SKB_DEFAULT_HEADROOM 64
#define SKB_DEFAULT_SIZE     1536  // ethernet MTU + headers

struct skb_data {
  _refcount;
  size_t size;          // total buffer size
  uint8_t buffer[];     // buffer data
};

//
// MARK: Socket Buffer API
//

static __ref skb_data_t *skb_data_alloc(size_t size) {
  skb_data_t *buf = kmallocz(sizeof(skb_data_t) + size);
  if (!buf) {
    return NULL;
  }

  initref(buf);
  buf->size = size;
  return buf;
}

sk_buff_t *skb_alloc(size_t size) {
  if (size == 0) {
    size = SKB_DEFAULT_SIZE;
  }

  sk_buff_t *skb = kmallocz(sizeof(sk_buff_t));
  if (!skb) {
    return NULL;
  }

  // allocate the skb_data buffer
  size_t total_size = size + SKB_DEFAULT_HEADROOM;
  skb_data_t *buf = skb_data_alloc(total_size);
  if (!buf) {
    kfree(skb);
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

void skb_free(sk_buff_t **skbp) {
  sk_buff_t *skb = moveptr(*skbp);
  if (!skb) {
    return;
  }

  putref(&skb->buf, kfree);
  kfree(skb);
}

sk_buff_t *skb_clone(sk_buff_t *skb) {
  ASSERT(skb != NULL);

  // grab reference to underlying buffer
  getref(skb->buf);

  sk_buff_t *clone = kmalloc(sizeof(sk_buff_t));
  if (!clone) {
    return NULL;
  }

  // shallow copy skb
  memcpy(clone, skb, sizeof(sk_buff_t));
  return clone;
}

sk_buff_t *skb_copy(sk_buff_t *skb) {
  ASSERT(skb != NULL);

  size_t buffer_size = skb->end - skb->head;
  sk_buff_t *new_skb = skb_alloc(buffer_size - SKB_DEFAULT_HEADROOM);
  if (!new_skb) {
    return NULL;
  }

  size_t head_offset = skb->data - skb->head;
  size_t total_size = skb->tail - skb->head;
  new_skb->data = new_skb->head + head_offset;
  new_skb->tail = new_skb->data + skb->len;
  memcpy(new_skb->head, skb->head, total_size);

  // copy metadata
  new_skb->len = skb->len;
  new_skb->data_len = skb->data_len;
  new_skb->protocol = skb->protocol;
  new_skb->pkt_type = skb->pkt_type;
  new_skb->csum = skb->csum;
  new_skb->ip_summed = skb->ip_summed;

  // adjust header pointers if they were set
  if (skb->network_header) {
    new_skb->network_header = new_skb->data + (skb->network_header - skb->data);
  }
  if (skb->transport_header) {
    new_skb->transport_header = new_skb->data + (skb->transport_header - skb->data);
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
