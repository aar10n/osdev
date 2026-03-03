//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#include <kernel/net/udp.h>
#include <kernel/net/inet.h>
#include <kernel/net/ip.h>
#include <kernel/net/socket.h>
#include <kernel/net/netdev.h>
#include <kernel/net/in_dev.h>

#include <kernel/mm.h>
#include <kernel/mm/pool.h>
#include <kernel/mutex.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <linux/in.h>

#include <bitmap.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("udp: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("udp: %s: " fmt, __func__, ##__VA_ARGS__)

static bitmap_t *port_bitmap;
static mtx_t port_lock;
static uint16_t next_ephemeral_port = UDP_EPHEMERAL_MIN;

static LIST_HEAD(udp_sock_t) udp_sockets;
static mtx_t socket_lock;
static pool_t *udp_pool;

static void udp_pool_init() {
  udp_pool = pool_create("udp", pool_sizes(sizeof(udp_sock_t)), 0);
}
STATIC_INIT(udp_pool_init);

static void udp_static_init() {
  port_bitmap = create_bitmap(65536);
  mtx_init(&port_lock, 0, "udp_ports");
  mtx_init(&socket_lock, 0, "udp_sockets");
}
STATIC_INIT(udp_static_init);

//
// MARK: Port Management
//

static uint16_t udp_get_port() {
  mtx_lock(&port_lock);

  index_t port = bitmap_get_set_free_range(port_bitmap, next_ephemeral_port, UDP_EPHEMERAL_MIN, UDP_EPHEMERAL_MAX);
  if (port >= 0) {
    next_ephemeral_port = (uint16_t)(port + 1);
    if (next_ephemeral_port > UDP_EPHEMERAL_MAX) {
      next_ephemeral_port = UDP_EPHEMERAL_MIN;
    }
  }

  mtx_unlock(&port_lock);
  return (port >= 0) ? (uint16_t)port : 0;
}

static int udp_check_port(uint16_t port) {
  mtx_lock(&port_lock);
  int result = bitmap_get(port_bitmap, port);
  mtx_unlock(&port_lock);
  return result;
}

//
// MARK: UDP Socket Management
//

__ref udp_sock_t *udp_sock_alloc() {
  udp_sock_t *udp_sk = pool_alloc(udp_pool, sizeof(udp_sock_t));
  if (!udp_sk) {
    return NULL;
  }

  udp_sk->saddr = INADDR_ANY;
  udp_sk->daddr = INADDR_ANY;

  initref(udp_sk);
  mtx_init(&udp_sk->lock, 0, "udp_lock");
  mtx_init(&udp_sk->rx_lock, 0, "udp_rx_lock");
  cond_init(&udp_sk->rx_cond, "udp_rx_cond");
  knlist_init(&udp_sk->knlist, &udp_sk->lock.lo);
  return udp_sk;
}

void _udp_sock_cleanup(__move udp_sock_t **udp_skp) {
  udp_sock_t *udp_sk = moveref(*udp_skp);
  if (!udp_sk) {
    return;
  }

  ASSERT(read_refcount(udp_sk) == 0);
  if (udp_sk->sport) {
    mtx_lock(&port_lock);
    bitmap_clear(port_bitmap, ntohs(udp_sk->sport));
    mtx_unlock(&port_lock);
  }

  mtx_lock(&udp_sk->rx_lock);
  LIST_FOR_IN_SAFE(skb, &udp_sk->rx_queue, list) {
    LIST_REMOVE(&udp_sk->rx_queue, skb, list);
    skb_free(&skb);
  }
  mtx_unlock(&udp_sk->rx_lock);

  mtx_destroy(&udp_sk->lock);
  mtx_destroy(&udp_sk->rx_lock);
  cond_destroy(&udp_sk->rx_cond);
  pool_free(udp_pool, udp_sk);
}

int udp_bind(udp_sock_t *udp_sk, uint32_t addr, uint16_t port) {
  ASSERT(udp_sk != NULL);

  mtx_lock(&udp_sk->lock);
  if (udp_sk->bound) {
    mtx_unlock(&udp_sk->lock);
    return -EINVAL;
  }

  if (port != 0 && udp_check_port(port)) {
    mtx_unlock(&udp_sk->lock);
    return -EADDRINUSE;
  }

  // allocate ephemeral port if needed
  if (port == 0) {
    port = udp_get_port();
    if (port == 0) {
      mtx_unlock(&udp_sk->lock);
      return -EADDRNOTAVAIL;
    }
  } else {
    mtx_lock(&port_lock);
    bitmap_set(port_bitmap, port);
    mtx_unlock(&port_lock);
  }

  udp_sk->saddr = htonl(addr);
  udp_sk->sport = htons(port);
  udp_sk->bound = true;

  mtx_unlock(&udp_sk->lock);
  DPRINTF("bound socket to {:ip}:%u\n", addr, port);
  return 0;
}

int udp_connect(udp_sock_t *udp_sk, uint32_t addr, uint16_t port) {
  ASSERT(udp_sk != NULL);

  mtx_lock(&udp_sk->lock);
  udp_sk->daddr = htonl(addr);
  udp_sk->dport = htons(port);
  udp_sk->connected = true;
  mtx_unlock(&udp_sk->lock);

  DPRINTF("connected socket to {:ip}:%u\n", addr, port);
  return 0;
}

//
// MARK: UDP Protocol Processing
//

static uint16_t udp_checksum(uint32_t saddr, uint32_t daddr, struct udphdr *udph, size_t len) {
  uint32_t sum = 0;
  sum += (saddr >> 16) & 0xFFFF;
  sum += saddr & 0xFFFF;
  sum += (daddr >> 16) & 0xFFFF;
  sum += daddr & 0xFFFF;
  sum += IPPROTO_UDP;  // protocol is single byte, pad with zero
  sum += len;           // length in host byte order

  uint8_t *ptr = (uint8_t *)udph;
  size_t count = len;

  while (count > 1) {
    sum += (ptr[0] << 8) | ptr[1];
    ptr += 2;
    count -= 2;
  }

  if (count > 0) {
    sum += ptr[0] << 8;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return ~sum;
}

int udp_rcv(sk_buff_t *skb) {
  ASSERT(skb != NULL);
  if (skb->len < sizeof(struct udphdr)) {
    EPRINTF("received packet too small for UDP header\n");
    skb_free(&skb);
    return -EINVAL;
  }

  struct udphdr *udph = (struct udphdr *)skb->data;
  struct iphdr *iph = skb_network_header(skb);

  uint16_t len = ntohs(udph->len);
  if (len < sizeof(struct udphdr) || len > skb->len) {
    EPRINTF("invalid UDP length: %u (packet len: %zu)\n", len, skb->len);
    skb_free(&skb);
    return -EINVAL;
  }

  // TODO: Fix UDP checksum verification
  // verify UDP checksum if present (optional for IPv4)
  if (0 && udph->check != 0) {
    uint32_t saddr_host = ntohl(iph->saddr);
    uint32_t daddr_host = ntohl(iph->daddr);
    uint16_t expected_csum = udp_checksum(saddr_host, daddr_host, udph, len);
    if (expected_csum != 0) {
      EPRINTF("bad checksum: saddr={:ip} daddr={:ip} len=%u stored=0x%04x computed=0x%04x\n",
              saddr_host, daddr_host, len, ntohs(udph->check), expected_csum);
      skb_free(&skb);
      return -EINVAL;
    }
  }

  uint16_t sport = ntohs(udph->source);
  uint16_t dport = ntohs(udph->dest);
  DPRINTF("received UDP packet: {:ip}:%u -> {:ip}:%u, len %u\n",
          ntohl(iph->saddr), sport, ntohl(iph->daddr), dport, len);

  mtx_lock(&socket_lock);
  udp_sock_t *udp_sk = LIST_FIND(_sk, &udp_sockets, link, (ntohs(_sk->sport) == dport &&
    (_sk->saddr == INADDR_ANY || _sk->saddr == iph->daddr)));
  mtx_unlock(&socket_lock);

  if (!udp_sk) {
    EPRINTF("no socket listening on port %u (dest IP {:ip})\n", dport, ntohl(iph->daddr));
    skb_free(&skb);
    return -ECONNREFUSED;
  }

  DPRINTF("found socket for port %u, queuing packet\n", dport);

  // Don't pull the UDP header - we need it in udp_recvmsg to get source addr/port
  skb_set_transport_header(skb, 0);

  mtx_lock(&udp_sk->rx_lock);
  LIST_ADD(&udp_sk->rx_queue, skb, list);
  udp_sk->rx_queue_len++;

  cond_broadcast(&udp_sk->rx_cond);
  knlist_activate_notes(&udp_sk->knlist, 0);

  mtx_unlock(&udp_sk->rx_lock);
  DPRINTF("queued UDP packet for socket (queue len: %zu)\n", udp_sk->rx_queue_len);
  return 0;
}

int udp_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  ASSERT(sock != NULL);
  ASSERT(msg != NULL);

  udp_sock_t *udp_sk = sock->sk;
  uint32_t daddr;
  uint16_t dport;
  uint32_t saddr;
  uint16_t sport;
  bool bound;

  // determine destination
  if (msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_in)) {
    struct sockaddr_in *sin = msg->msg_name;
    if (sin->sin_family != AF_INET) {
      return -EAFNOSUPPORT;
    }
    daddr = ntohl(sin->sin_addr.s_addr);
    dport = ntohs(sin->sin_port);
  } else {
    mtx_lock(&udp_sk->lock);
    if (!udp_sk->connected) {
      mtx_unlock(&udp_sk->lock);
      return -EDESTADDRREQ;
    }
    daddr = ntohl(udp_sk->daddr);
    dport = ntohs(udp_sk->dport);
    mtx_unlock(&udp_sk->lock);
  }

  mtx_lock(&udp_sk->lock);
  bound = udp_sk->bound;
  if (bound) {
    sport = ntohs(udp_sk->sport);
    saddr = ntohl(udp_sk->saddr);
  }
  mtx_unlock(&udp_sk->lock);

  if (!bound) {
    int ret = udp_bind(udp_sk, INADDR_ANY, 0);
    if (ret < 0) {
      return ret;
    }
    mtx_lock(&udp_sk->lock);
    sport = ntohs(udp_sk->sport);
    saddr = ntohl(udp_sk->saddr);
    mtx_unlock(&udp_sk->lock);
  }

  size_t total_len = 0;
  for (size_t i = 0; i < msg->msg_iovlen; i++) {
    total_len += msg->msg_iov[i].iov_len;
  }
  total_len = min(len, total_len);

  sk_buff_t *skb = skb_alloc(total_len + sizeof(struct udphdr));
  if (!skb) {
    return -ENOMEM;
  }

  // copy data from iovec
  uint8_t *data = skb_put_data(skb, total_len);
  size_t copied = 0;
  for (size_t i = 0; i < msg->msg_iovlen && copied < total_len; i++) {
    size_t to_copy = min(msg->msg_iov[i].iov_len, total_len - copied);
    memcpy(data + copied, msg->msg_iov[i].iov_base, to_copy);
    copied += to_copy;
  }

  // add UDP header
  struct udphdr *udph = skb_push(skb, sizeof(struct udphdr));
  udph->source = htons(sport);
  udph->dest = htons(dport);
  udph->len = htons(skb->len);
  udph->check = 0;

  route_t *route = ip_route_lookup(daddr);
  if (!route) {
    EPRINTF("no route to destination {:ip}\n", daddr);
    skb_free(&skb);
    return -EHOSTUNREACH;
  }

  uint32_t src_addr = saddr;
  if (src_addr == INADDR_ANY) {
    // use the IP address of the output device
    in_ifaddr_t *ifa = LIST_FIRST(&route->dev->ip_addrs);
    if (ifa && ifa->ifa_address != INADDR_ANY) {
      src_addr = ifa->ifa_address;
    } else {
      src_addr = INADDR_LOOPBACK;  // fallback for loopback
    }
  }

  udph->check = 0;

  DPRINTF("sending UDP: {:ip}:%u -> {:ip}:%u, len=%u\n",
          src_addr, sport, daddr, dport, ntohs(udph->len));

  int ret = ip_output(skb, src_addr, daddr, IPPROTO_UDP, route->dev);
  if (ret < 0) {
    return ret;
  }

  return (int)total_len;
}

int udp_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  ASSERT(sock != NULL);
  ASSERT(msg != NULL);

  udp_sock_t *udp_sk = sock->sk;
  int is_nonblock = (sock->flags & SOCK_NONBLOCK) || (flags & MSG_DONTWAIT);

  mtx_lock(&udp_sk->rx_lock);
  while (udp_sk->rx_queue_len == 0) {
    if (is_nonblock) {
      mtx_unlock(&udp_sk->rx_lock);
      return -EAGAIN;
    }

    cond_wait(&udp_sk->rx_cond, &udp_sk->rx_lock);
  }

  sk_buff_t *skb = LIST_FIRST(&udp_sk->rx_queue);
  LIST_REMOVE(&udp_sk->rx_queue, skb, list);
  udp_sk->rx_queue_len--;
  mtx_unlock(&udp_sk->rx_lock);

  // Skip UDP header - payload starts after it
  uint8_t *payload = skb->data + sizeof(struct udphdr);
  size_t payload_len = skb->len - sizeof(struct udphdr);
  size_t to_copy = min(payload_len, len);
  size_t copied = 0;

  for (size_t i = 0; i < msg->msg_iovlen && copied < to_copy; i++) {
    size_t copy_len = min(msg->msg_iov[i].iov_len, to_copy - copied);
    memcpy(msg->msg_iov[i].iov_base, payload + copied, copy_len);
    copied += copy_len;
  }

  // Set MSG_TRUNC if message was truncated
  if (payload_len > len) {
    msg->msg_flags |= MSG_TRUNC;
  }

  if (msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_in)) {
    // fill in source address
    struct iphdr *iph = skb_network_header(skb);
    struct udphdr *udph = skb_transport_header(skb);
    struct sockaddr_in *sin = msg->msg_name;

    sin->sin_family = AF_INET;
    sin->sin_port = udph->source;
    sin->sin_addr.s_addr = iph->saddr;
    memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
    msg->msg_namelen = sizeof(struct sockaddr_in);
  }

  skb_free(&skb);

  // If MSG_TRUNC was in flags, return actual message size; otherwise return copied
  if (flags & MSG_TRUNC) {
    return (int)payload_len;
  }
  return (int)copied;
}

//
// MARK: UDP Socket Operations
//

static int udp_create(sock_t *sock, int protocol) {
  if (protocol != 0 && protocol != IPPROTO_UDP) {
    return -EPROTONOSUPPORT;
  }

  udp_sock_t *udp_sk = udp_sock_alloc();
  if (!udp_sk) {
    return -ENOMEM;
  }

  mtx_lock(&socket_lock);
  // the global udp socket list does not hold references
  LIST_ADD(&udp_sockets, udp_sk, link);
  mtx_unlock(&socket_lock);
  sock->sk = moveref(udp_sk);
  sock->knlist = &((udp_sock_t *)sock->sk)->knlist;

  DPRINTF("created UDP socket\n");
  return 0;
}

static int udp_release(sock_t *sock) {
  if (!sock->sk) {
    return 0;
  }

  udp_sock_t *udp_sk = moveref(sock->sk);
  mtx_lock(&socket_lock);
  LIST_REMOVE(&udp_sockets, udp_sk, link);
  mtx_unlock(&socket_lock);

  udp_sock_putref(&udp_sk);
  return 0;
}

static int udp_sock_bind(sock_t *sock, struct sockaddr *addr, int addrlen) {
  if (!sock->sk || addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  if (sin->sin_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  udp_sock_t *udp_sk = sock->sk;
  return udp_bind(udp_sk, ntohl(sin->sin_addr.s_addr), ntohs(sin->sin_port));
}

static int udp_sock_connect(sock_t *sock, struct sockaddr *addr, int addrlen, int flags) {
  if (!sock->sk || addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  if (sin->sin_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  udp_sock_t *udp_sk = (udp_sock_t *)sock->sk;
  return udp_connect(udp_sk, ntohl(sin->sin_addr.s_addr), ntohs(sin->sin_port));
}

static int udp_listen(sock_t *sock, int backlog) {
  return -EOPNOTSUPP;
}

static int udp_accept(sock_t *sock, sock_t *newsock, int flags) {
  return -EOPNOTSUPP;
}

static int udp_shutdown(sock_t *sock, int how) {
  return -EOPNOTSUPP;
}

const struct proto_ops udp_dgram_ops = {
  .family = AF_INET,
  .create = udp_create,
  .release = udp_release,
  .bind = udp_sock_bind,
  .connect = udp_sock_connect,
  .listen = udp_listen,
  .accept = udp_accept,
  .sendmsg = udp_sendmsg,
  .recvmsg = udp_recvmsg,
  .shutdown = udp_shutdown,
};

//
// MARK: UDP Module Registration
//

void udp_init() {
  ip_register_protocol(IPPROTO_UDP, udp_rcv);
  inet_register_protocol(SOCK_DGRAM, 0, &udp_dgram_ops);

  DPRINTF("UDP protocol initialized\n");
}
MODULE_INIT(udp_init);
