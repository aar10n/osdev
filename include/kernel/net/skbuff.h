//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#ifndef KERNEL_NET_SKBUFF_H
#define KERNEL_NET_SKBUFF_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/queue.h>

typedef struct netdev netdev_t;
typedef struct skb_data skb_data_t;

/**
 * A socket buffer (skb).
 *
 * Used to pass packets through the network stack. The buffer layout is:
 *   [head ... data ... tail ... end]
 * As the packet moves through protocol layers, skb_push/skb_pull adjust
 * the data/tail pointers to add or remove headers without copying data.
 * The underlying skb_data buffer is refcounted to enable shallow cloning.
 */
typedef struct sk_buff {
  uint8_t *data;        // pointer to current data start
  uint8_t *head;        // pointer to buffer start
  uint8_t *tail;        // pointer to current data end
  uint8_t *end;         // pointer to buffer end

  size_t len;           // length of current data
  size_t data_len;      // length of data in fragments (unused initially)

  skb_data_t *buf;      // underlying buffer (ref)
  netdev_t *dev;        // device this skb came from/goes to (ref)

  /* protocol info */
  uint16_t protocol;    // ethernet protocol
  uint16_t pkt_type;    // packet classification
  /* network layer info */
  uint8_t *network_header;  // pointer to network header (IP)
  uint8_t *transport_header; // pointer to transport header (UDP/TCP)
  /* checksum info */
  uint16_t csum;        // checksum
  uint8_t ip_summed;    // checksum status
  /* timestamps */
  uint64_t timestamp;   // packet timestamp

  LIST_ENTRY(struct sk_buff) list;
} sk_buff_t;

// packet types
#define PACKET_HOST     0  // to us
#define PACKET_BROADCAST 1  // to all
#define PACKET_MULTICAST 2  // to group
#define PACKET_OTHERHOST 3  // to someone else
#define PACKET_OUTGOING  4  // outgoing
#define PACKET_LOOPBACK  5  // loopback packet

// checksum status
#define CHECKSUM_NONE       0  // no checksum
#define CHECKSUM_UNNECESSARY 1  // checksum verified
#define CHECKSUM_COMPLETE   2  // checksum provided
#define CHECKSUM_PARTIAL    3  // partial checksum

//
// Socket Buffer API
//

sk_buff_t *skb_alloc(size_t size);
void skb_free(sk_buff_t **skbp);

sk_buff_t *skb_clone(sk_buff_t *skb);
sk_buff_t *skb_copy(sk_buff_t *skb);
size_t skb_headroom(sk_buff_t *skb);
size_t skb_tailroom(sk_buff_t *skb);

void *skb_put_data(sk_buff_t *skb, size_t len);
void *skb_push(sk_buff_t *skb, size_t len);
void *skb_pull(sk_buff_t *skb, size_t len);
void skb_trim(sk_buff_t *skb, size_t len);

static inline void skb_set_network_header(sk_buff_t *skb, int offset) {
  skb->network_header = skb->data + offset;
}

static inline void skb_set_transport_header(sk_buff_t *skb, int offset) {
  skb->transport_header = skb->data + offset;
}

static inline void *skb_network_header(sk_buff_t *skb) {
  return (void *)skb->network_header;
}

static inline void *skb_transport_header(sk_buff_t *skb) {
  return (void *)skb->transport_header;
}

#endif
