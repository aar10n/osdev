//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#ifndef KERNEL_NET_UDP_H
#define KERNEL_NET_UDP_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>
#include <kernel/queue.h>
#include <kernel/kevent.h>

typedef struct sock sock_t;
typedef struct netdev netdev_t;
typedef struct sk_buff sk_buff_t;
struct msghdr;

#include <linux/socket.h>
#include <linux/udp.h>

/**
 * A UDP socket.
 */
typedef struct udp_sock {
  uint32_t saddr;     // source address *
  uint16_t sport;     // source port *
  uint32_t daddr;     // destination address *
  uint16_t dport;     // destination port *
                      // * = fields are in network byte order

  bool bound;         // socket is bound to address
  bool connected;     // socket is connected
  mtx_t lock;         // protects socket state

  // receive queue
  LIST_HEAD(sk_buff_t) rx_queue;
  size_t rx_queue_len;
  mtx_t rx_lock;
  cond_t rx_cond;
  struct knlist knlist;

  _refcount;

  LIST_ENTRY(struct udp_sock) link;
} udp_sock_t;

#define udp_sock_getref(sock) ({ \
  ASSERT_IS_TYPE(udp_sock_t *, sock); \
  udp_sock_t *__sock = (sock); \
  __sock ? ref_get(&__sock->refcount) : NULL; \
  __sock; \
})

#define udp_sock_putref(sockref) ({ \
  ASSERT_IS_TYPE(udp_sock_t **, sockref); \
  udp_sock_t *__sock = *(sockref); \
  *(sockref) = NULL; \
  if (__sock) { \
    if (ref_put(&__sock->refcount)) { \
      _udp_sock_cleanup(&__sock); \
    } \
  } \
})

#define UDP_EPHEMERAL_MIN 32768
#define UDP_EPHEMERAL_MAX 65535

//
// MARK: UDP Socket API
//

__ref udp_sock_t *udp_sock_alloc();
void _udp_sock_cleanup(__move udp_sock_t **udp_skp);
int udp_bind(udp_sock_t *udp_sk, uint32_t addr, uint16_t port);
int udp_connect(udp_sock_t *udp_sk, uint32_t addr, uint16_t port);

int udp_rcv(sk_buff_t *skb);
int udp_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags);
int udp_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags);

#endif
