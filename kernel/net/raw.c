//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#include <kernel/net/raw.h>
#include <kernel/net/inet.h>
#include <kernel/net/ip.h>
#include <kernel/net/in_dev.h>
#include <kernel/net/netdev.h>
#include <kernel/net/socket.h>

#include <kernel/cond.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <linux/icmp.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("raw: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("raw: %s: " fmt, __func__, ##__VA_ARGS__)

#define RAW_DEFAULT_TTL 64
#define RAW_DEFAULT_RCVBUF (256 * 1024UL)  // 256KB default receive buffer

typedef struct raw_sock {
  uint32_t src_addr;  // bound source address
  uint32_t dst_addr;  // connected destination address
  uint8_t protocol;   // IP protocol number
  bool closing;       // socket is closing
  int shutdown;       // shutdown state

  // socket options
  bool hdrincl;       // IP_HDRINCL option
  uint8_t ttl;        // IP_TTL option
  uint8_t tos;        // IP_TOS option
  size_t rcvbuf_max;  // SO_RCVBUF max queue size

  // receive queue
  LIST_HEAD(sk_buff_t) recv_queue;
  size_t recv_queue_bytes;
  mtx_t recv_lock;
  cond_t recv_cond;

  LIST_ENTRY(struct raw_sock) link;
} raw_sock_t;

static LIST_HEAD(raw_sock_t) raw_sockets;
static mtx_t socket_lock;

//
// Raw Socket Allocation
//

static raw_sock_t *raw_sock_alloc() {
  raw_sock_t *raw_sk = kmallocz(sizeof(raw_sock_t));
  if (!raw_sk) {
    return NULL;
  }

  raw_sk->ttl = RAW_DEFAULT_TTL;
  raw_sk->rcvbuf_max = RAW_DEFAULT_RCVBUF;
  raw_sk->shutdown = -1;

  LIST_INIT(&raw_sk->recv_queue);
  mtx_init(&raw_sk->recv_lock, 0, "raw_recv");
  cond_init(&raw_sk->recv_cond, "raw_recv");
  return raw_sk;
}

static void raw_sock_free(raw_sock_t *raw_sk) {
  if (!raw_sk) {
    return;
  }

  // mark socket as closing and wake up any threads waiting on recv
  mtx_lock(&raw_sk->recv_lock);
  raw_sk->closing = true;
  cond_broadcast(&raw_sk->recv_cond);
  mtx_unlock(&raw_sk->recv_lock);

  // free all queued packets
  LIST_FOR_IN_SAFE(skb, &raw_sk->recv_queue, list) {
    LIST_REMOVE(&raw_sk->recv_queue, skb, list);
    skb_free(&skb);
  }

  DPRINTF("freeing raw socket\n");
  cond_destroy(&raw_sk->recv_cond);
  mtx_destroy(&raw_sk->recv_lock);
  kfree(raw_sk);
}

//
// Raw Socket Operations
//

int raw_create(sock_t *sock, int protocol) {
  raw_sock_t *raw_sk = raw_sock_alloc();
  if (!raw_sk) {
    return -ENOMEM;
  }

  raw_sk->protocol = protocol;
  sock->sk = raw_sk;

  mtx_lock(&socket_lock);
  LIST_ADD(&raw_sockets, raw_sk, link);
  mtx_unlock(&socket_lock);

  DPRINTF("created raw socket for protocol %d\n", raw_sk->protocol);
  return 0;
}

int raw_release(sock_t *sock) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  if (!raw_sk) {
    return 0;
  }

  mtx_lock(&socket_lock);
  LIST_REMOVE(&raw_sockets, raw_sk, link);
  mtx_unlock(&socket_lock);

  raw_sock_free(raw_sk);
  sock->sk = NULL;
  return 0;
}

int raw_bind(sock_t *sock, struct sockaddr *addr, int addrlen) {
  if (addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  struct sockaddr_in *sin = (struct sockaddr_in *)addr;

  raw_sk->src_addr = ntohl(sin->sin_addr.s_addr);
  DPRINTF("bound to address {:ip}\n", raw_sk->src_addr);
  return 0;
}

int raw_connect(sock_t *sock, struct sockaddr *addr, int addrlen, int flags) {
  if (addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  struct sockaddr_in *sin = (struct sockaddr_in *)addr;

  raw_sk->dst_addr = ntohl(sin->sin_addr.s_addr);
  sock->state = SS_CONNECTED;

  DPRINTF("connected to address {:ip}\n", raw_sk->dst_addr);
  return 0;
}

static int raw_listen(sock_t *sock, int backlog) {
  return -EOPNOTSUPP;
}

static int raw_accept(sock_t *sock, sock_t *newsock, int flags) {
  return -EOPNOTSUPP;
}

static int raw_shutdown(sock_t *sock, int how) {
  if (how < SHUT_RD || how > SHUT_RDWR) {
    return -EINVAL;
  }

  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  if (!raw_sk) {
    return -ENOTCONN;
  }

  mtx_lock(&raw_sk->recv_lock);
  raw_sk->shutdown = how;

  if (how == SHUT_RD || how == SHUT_RDWR) {
    cond_broadcast(&raw_sk->recv_cond);
  }
  mtx_unlock(&raw_sk->recv_lock);

  DPRINTF("shutdown socket how=%d\n", how);
  return 0;
}

static int raw_setsockopt(sock_t *sock, int level, int optname, const void *optval, socklen_t optlen) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  if (!raw_sk) {
    return -EINVAL;
  }

  if (level == IPPROTO_IP) {
    switch (optname) {
      case IP_HDRINCL: {
        if (optlen < sizeof(int)) {
          return -EINVAL;
        }
        int val = *(int *)optval;
        raw_sk->hdrincl = (val != 0);
        DPRINTF("set IP_HDRINCL=%d\n", raw_sk->hdrincl);
        return 0;
      }
      case IP_TTL: {
        if (optlen < sizeof(int)) {
          return -EINVAL;
        }
        int val = *(int *)optval;
        if (val < 1 || val > 255) {
          return -EINVAL;
        }
        raw_sk->ttl = (uint8_t)val;
        DPRINTF("set IP_TTL=%d\n", raw_sk->ttl);
        return 0;
      }
      case IP_TOS: {
        if (optlen < sizeof(int)) {
          return -EINVAL;
        }
        int val = *(int *)optval;
        raw_sk->tos = (uint8_t)val;
        DPRINTF("set IP_TOS=%d\n", raw_sk->tos);
        return 0;
      }
      default:
        return -ENOPROTOOPT;
    }
  } else if (level == SOL_SOCKET) {
    switch (optname) {
      case SO_RCVBUF: {
        if (optlen < sizeof(int)) {
          return -EINVAL;
        }
        int val = *(int *)optval;
        if (val < 256) {
          val = 256;
        }
        raw_sk->rcvbuf_max = (size_t)val;
        DPRINTF("set SO_RCVBUF=%zu\n", raw_sk->rcvbuf_max);
        return 0;
      }
      case SO_SNDBUF:
        return 0;
      default:
        return -ENOPROTOOPT;
    }
  }

  return -ENOPROTOOPT;
}

static int raw_getsockopt(sock_t *sock, int level, int optname, void *optval, socklen_t *optlen) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  if (!raw_sk || !optval || !optlen) {
    return -EINVAL;
  }

  if (level == IPPROTO_IP) {
    switch (optname) {
      case IP_HDRINCL: {
        if (*optlen < sizeof(int)) {
          return -EINVAL;
        }
        *(int *)optval = raw_sk->hdrincl ? 1 : 0;
        *optlen = sizeof(int);
        return 0;
      }
      case IP_TTL: {
        if (*optlen < sizeof(int)) {
          return -EINVAL;
        }
        *(int *)optval = raw_sk->ttl;
        *optlen = sizeof(int);
        return 0;
      }
      case IP_TOS: {
        if (*optlen < sizeof(int)) {
          return -EINVAL;
        }
        *(int *)optval = raw_sk->tos;
        *optlen = sizeof(int);
        return 0;
      }
      default:
        return -ENOPROTOOPT;
    }
  } else if (level == SOL_SOCKET) {
    switch (optname) {
      case SO_RCVBUF: {
        if (*optlen < sizeof(int)) {
          return -EINVAL;
        }
        *(int *)optval = (int)raw_sk->rcvbuf_max;
        *optlen = sizeof(int);
        return 0;
      }
      case SO_SNDBUF: {
        if (*optlen < sizeof(int)) {
          return -EINVAL;
        }
        *(int *)optval = 0;
        *optlen = sizeof(int);
        return 0;
      }
      default:
        return -ENOPROTOOPT;
    }
  }

  return -ENOPROTOOPT;
}

static int raw_getsockname(sock_t *sock, struct sockaddr *addr, socklen_t *addrlen) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  if (!raw_sk || !addr || !addrlen) {
    return -EINVAL;
  }

  if (*addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  sin->sin_family = AF_INET;
  sin->sin_port = 0;
  sin->sin_addr.s_addr = htonl(raw_sk->src_addr);
  *addrlen = sizeof(struct sockaddr_in);

  return 0;
}

static int raw_getpeername(sock_t *sock, struct sockaddr *addr, socklen_t *addrlen) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;
  if (!raw_sk || !addr || !addrlen) {
    return -EINVAL;
  }

  if (raw_sk->dst_addr == 0) {
    return -ENOTCONN;
  }

  if (*addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  sin->sin_family = AF_INET;
  sin->sin_port = 0;
  sin->sin_addr.s_addr = htonl(raw_sk->dst_addr);
  *addrlen = sizeof(struct sockaddr_in);

  return 0;
}

int raw_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;

  if (raw_sk->shutdown == SHUT_WR || raw_sk->shutdown == SHUT_RDWR) {
    return -EPIPE;
  }

  // determine destination address
  uint32_t dst_addr = raw_sk->dst_addr;
  if (msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_in)) {
    struct sockaddr_in *sin = (struct sockaddr_in *)msg->msg_name;
    dst_addr = ntohl(sin->sin_addr.s_addr);
  }

  if (dst_addr == 0) {
    EPRINTF("no destination address\n");
    return -EDESTADDRREQ;
  }

  if (raw_sk->protocol == IPPROTO_ICMP && len < sizeof(struct icmphdr)) {
    EPRINTF("ICMP packet too small\n");
    return -EINVAL;
  }

  sk_buff_t *skb = skb_alloc(len + 128);
  if (!skb) {
    return -ENOMEM;
  }

  uint8_t *data = skb_put_data(skb, len);
  size_t copied = 0;
  for (int i = 0; i < msg->msg_iovlen && copied < len; i++) {
    size_t to_copy = min(msg->msg_iov[i].iov_len, len - copied);
    memcpy(data + copied, msg->msg_iov[i].iov_base, to_copy);
    copied += to_copy;
  }

  skb_set_transport_header(skb, 0);

  if (raw_sk->protocol == IPPROTO_ICMP) {
    // recalculate checksum
    struct icmphdr *icmph = (struct icmphdr *)data;
    DPRINTF("sending ICMP packet type=%u, code=%u, id=%u, seq=%u\n", icmph->type, icmph->code,
            ntohs(icmph->un.echo.id), ntohs(icmph->un.echo.sequence));
    icmph->checksum = 0;
    icmph->checksum = ip_checksum(icmph, len);
  }

  // find output route first
  route_t *route = ip_route_lookup(dst_addr);
  if (!route) {
    EPRINTF("no route to {:ip}\n", dst_addr);
    skb_free(&skb);
    return -EHOSTUNREACH;
  }

  if (!route->dev) {
    EPRINTF("route has NULL device for dst {:ip}\n", dst_addr);
    skb_free(&skb);
    return -EHOSTUNREACH;
  }

  // determine source address
  uint32_t src_addr = raw_sk->src_addr;
  if (src_addr == 0) {
    if (!LIST_EMPTY(&route->dev->ip_addrs)) {
      in_ifaddr_t *ifa = LIST_FIRST(&route->dev->ip_addrs);
      src_addr = ifa->ifa_address;
      DPRINTF("using source address {:ip} from interface {:str}\n", src_addr, &route->dev->name);
    } else {
      DPRINTF("no IP address on interface {:str}, using loopback\n", &route->dev->name);
      src_addr = INADDR_LOOPBACK;
    }
  }

  skb->dev = route->dev;
  int ret = ip_output(skb, src_addr, dst_addr, raw_sk->protocol, route->dev);

  skb_free(&skb);
  return (int)((ret < 0) ? ret : len);
}

int raw_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  raw_sock_t *raw_sk = (raw_sock_t *)sock->sk;

  mtx_lock(&raw_sk->recv_lock);

  // check if receiving is allowed
  if (raw_sk->shutdown == SHUT_RD || raw_sk->shutdown == SHUT_RDWR) {
    mtx_unlock(&raw_sk->recv_lock);
    return 0;  // return EOF
  }

  // wait for packets if queue is empty
  while (LIST_EMPTY(&raw_sk->recv_queue) && !raw_sk->closing &&
         raw_sk->shutdown != SHUT_RD && raw_sk->shutdown != SHUT_RDWR) {
    if (flags & MSG_DONTWAIT) {
      EPRINTF("no packets in queue and MSG_DONTWAIT set, returning EAGAIN\n");
      mtx_unlock(&raw_sk->recv_lock);
      return -EAGAIN;
    }

    int ret = cond_wait_sig(&raw_sk->recv_cond, &raw_sk->recv_lock);
    if (ret != 0) {
      mtx_unlock(&raw_sk->recv_lock);
      return -EINTR;
    }
  }

  if (raw_sk->closing) {
    mtx_unlock(&raw_sk->recv_lock);
    return 0;  // EOF when socket is closed
  }

  sk_buff_t *skb = LIST_FIRST(&raw_sk->recv_queue);
  size_t actual_len = skb->len;

  // if MSG_PEEK, don't remove from queue
  if (!(flags & MSG_PEEK)) {
    LIST_REMOVE(&raw_sk->recv_queue, skb, list);
    raw_sk->recv_queue_bytes -= skb->len;
  }
  mtx_unlock(&raw_sk->recv_lock);

  size_t copy_len = min(len, skb->len);
  size_t copied = 0;
  for (int i = 0; i < msg->msg_iovlen && copied < copy_len; i++) {
    size_t to_copy = min(msg->msg_iov[i].iov_len, copy_len - copied);
    memcpy(msg->msg_iov[i].iov_base, skb->data + copied, to_copy);
    copied += to_copy;
  }

  if (msg->msg_name && msg->msg_namelen > 0) {
    struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
    DPRINTF("extracting source address from IP header: saddr={:ip} (network byte order)\n", ntohl(iph->saddr));

    struct sockaddr_in sin = {
      .sin_family = AF_INET,
      .sin_port = 0,
      .sin_addr.s_addr = iph->saddr,  // already in network byte order
    };

    size_t addr_len = min(msg->msg_namelen, sizeof(sin));
    memcpy(msg->msg_name, &sin, addr_len);
    msg->msg_namelen = addr_len;
  }

  // if MSG_PEEK, don't free the skb
  if (!(flags & MSG_PEEK)) {
    skb_free(&skb);
  }

  // if MSG_TRUNC, return the actual packet length even if buffer was smaller
  if (flags & MSG_TRUNC) {
    return (int)actual_len;
  }

  return (int)copy_len;
}

//
// Raw Socket Receive Handler
//

int raw_rcv(sk_buff_t *skb, uint8_t protocol) {
  ASSERT(skb != NULL);

  struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
  uint32_t src_addr = ntohl(iph->saddr);
  uint32_t dst_addr = ntohl(iph->daddr);

  DPRINTF("raw_rcv: IP header at %p: saddr={:ip} daddr={:ip}\n", iph, src_addr, dst_addr);

  int delivered = 0;
  mtx_lock(&socket_lock);

  // deliver to all matching raw sockets
  raw_sock_t *raw_sk;
  LIST_FOREACH(raw_sk, &raw_sockets, link) {
    if (raw_sk->protocol != protocol) {
      continue;
    }

    DPRINTF("checking socket: sk_src={:ip} sk_dst={:ip}, pkt_src={:ip} pkt_dst={:ip}\n",
            raw_sk->src_addr, raw_sk->dst_addr, src_addr, dst_addr);

    // check address match if connected/bound, only filter if socket has a specific address set
    if (raw_sk->dst_addr != 0 && raw_sk->dst_addr != src_addr) {
      continue;
    }
    if (raw_sk->src_addr != 0 && raw_sk->src_addr != dst_addr) {
      continue;
    }

    sk_buff_t *clone = skb_copy(skb);
    if (!clone) {
      continue;
    }

    mtx_lock(&raw_sk->recv_lock);

    // check if adding this packet would exceed the receive buffer limit
    if (raw_sk->recv_queue_bytes + clone->len > raw_sk->rcvbuf_max) {
      mtx_unlock(&raw_sk->recv_lock);
      skb_free(&clone);
      DPRINTF("dropped packet: receive queue full (bytes=%zu, max=%zu)\n",
              raw_sk->recv_queue_bytes, raw_sk->rcvbuf_max);
      continue;
    }

    LIST_ADD(&raw_sk->recv_queue, clone, list);
    raw_sk->recv_queue_bytes += clone->len;
    cond_signal(&raw_sk->recv_cond);
    mtx_unlock(&raw_sk->recv_lock);
    delivered++;

    DPRINTF("delivered packet to raw socket (proto=%d, queue_bytes=%zu)\n",
            protocol, raw_sk->recv_queue_bytes);
  }

  mtx_unlock(&socket_lock);
  return delivered;
}

//
// MARK: Raw Socket Protocol Registration
//

const struct proto_ops raw_ops = {
  .family = AF_INET,
  .create = raw_create,
  .release = raw_release,
  .bind = raw_bind,
  .connect = raw_connect,
  .listen = raw_listen,
  .accept = raw_accept,
  .sendmsg = raw_sendmsg,
  .recvmsg = raw_recvmsg,
  .shutdown = raw_shutdown,
  .setsockopt = raw_setsockopt,
  .getsockopt = raw_getsockopt,
  .getsockname = raw_getsockname,
  .getpeername = raw_getpeername,
};

void raw_init() {
  mtx_init(&socket_lock, 0, "raw_sockets");
  inet_register_protocol(SOCK_RAW, 0, &raw_ops);
  DPRINTF("raw socket support initialized\n");
}
MODULE_INIT(raw_init);
