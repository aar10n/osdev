//
// Created by Aaron Gill-Braun on 2025-10-03.
//

#include <kernel/net/tcp.h>
#include <kernel/net/inet.h>
#include <kernel/net/in_dev.h>
#include <kernel/net/netdev.h>

#include <kernel/mm.h>
#include <kernel/mm/pool.h>
#include <kernel/alarm.h>
#include <kernel/clock.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <linux/in.h>

#include <bitmap.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("tcp: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("tcp: %s: " fmt, __func__, ##__VA_ARGS__)

static bitmap_t *port_bitmap;
static mtx_t port_lock;
static uint16_t next_ephemeral_port = TCP_EPHEMERAL_MIN;

static LIST_HEAD(tcp_sock_t) tcp_sockets;
static mtx_t socket_lock;
static pool_t *tcp_pool;

static void tcp_pool_init() {
  tcp_pool = pool_create("tcp", pool_sizes(sizeof(tcp_sock_t)), 0);
}
STATIC_INIT(tcp_pool_init);

static uint32_t tcp_isn_counter = 1;

static void tcp_stop_retrans_timer(tcp_sock_t *tcp_sk);
static uint16_t tcp_get_port();
static int tcp_check_port(uint16_t port);
static tcp_sock_t *tcp_lookup_sock(uint32_t saddr, uint16_t sport, uint32_t daddr, uint16_t dport);
static uint16_t tcp_checksum(uint32_t saddr, uint32_t daddr, struct tcphdr *tcph, size_t len);

static void tcp_static_init() {
  port_bitmap = create_bitmap(65536);
  mtx_init(&port_lock, 0, "tcp_ports");
  mtx_init(&socket_lock, 0, "tcp_sockets");
}
STATIC_INIT(tcp_static_init);

//
// MARK: Helper Functions
//

static const char *tcp_state_str(int state) {
  switch (state) {
    case TCP_CLOSED: return "CLOSED";
    case TCP_LISTEN: return "LISTEN";
    case TCP_SYN_SENT: return "SYN_SENT";
    case TCP_SYN_RECEIVED: return "SYN_RECEIVED";
    case TCP_ESTABLISHED: return "ESTABLISHED";
    case TCP_FIN_WAIT_1: return "FIN_WAIT_1";
    case TCP_FIN_WAIT_2: return "FIN_WAIT_2";
    case TCP_CLOSE_WAIT: return "CLOSE_WAIT";
    case TCP_CLOSING: return "CLOSING";
    case TCP_LAST_ACK: return "LAST_ACK";
    case TCP_TIME_WAIT: return "TIME_WAIT";
    default: return "UNKNOWN";
  }
}

static void tcp_set_state(tcp_sock_t *tcp_sk, int new_state) {
  DPRINTF("state transition: %s -> %s\n", tcp_state_str(tcp_sk->state), tcp_state_str(new_state));
  tcp_sk->state = new_state;

  switch (new_state) {
    case TCP_ESTABLISHED:
      // wake up connect() waiters
      cond_signal(&tcp_sk->connect_cond);
      break;
    case TCP_CLOSED:
      // stop retransmission timer when entering CLOSED state
      tcp_stop_retrans_timer(tcp_sk);
      // wake up all waiters on connection close
      cond_broadcast(&tcp_sk->connect_cond);
      cond_broadcast(&tcp_sk->accept_cond);
      cond_broadcast(&tcp_sk->recv_cond);
      cond_broadcast(&tcp_sk->send_cond);
      // remove from socket list when fully closed
      mtx_lock(&socket_lock);
      LIST_REMOVE(&tcp_sockets, tcp_sk, link);
      mtx_unlock(&socket_lock);
      break;
    case TCP_CLOSE_WAIT:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
      // wake up recv waiters on connection closing
      cond_broadcast(&tcp_sk->recv_cond);
      break;
    default:
      break;
  }
}

static uint32_t tcp_new_isn() {
  return __atomic_add_fetch(&tcp_isn_counter, 64000, __ATOMIC_SEQ_CST);
}

// MARK: RTO Calculation (RFC 6298)

static void tcp_update_rto(tcp_sock_t *tcp_sk, uint32_t measured_rtt) {
  // measured_rtt is in milliseconds
  if (tcp_sk->srtt == 0) {
    // first measurement
    tcp_sk->srtt = measured_rtt << 3;     // srtt = r
    tcp_sk->rttvar = measured_rtt << 1;   // rttvar = r/2
  } else {
    // subsequent measurements
    int32_t delta = (int32_t)(measured_rtt - (tcp_sk->srtt >> 3));
    tcp_sk->srtt += delta;                // srtt = 7/8 * srtt + 1/8 * r
    if (delta < 0) {
      delta = -delta;
    }
    tcp_sk->rttvar += (delta - (tcp_sk->rttvar >> 2));  // rttvar = 3/4 * rttvar + 1/4 * |delta|
  }

  // calculate RTO = srtt + 4 * rttvar
  uint32_t rto = (tcp_sk->srtt >> 3) + tcp_sk->rttvar;

  // apply bounds
  if (rto < TCP_MIN_RTO) {
    rto = TCP_MIN_RTO;
  } else if (rto > TCP_MAX_RTO) {
    rto = TCP_MAX_RTO;
  }

  tcp_sk->rto = rto;
  DPRINTF("updated RTO: srtt=%u rttvar=%u rto=%u ms\n",
          tcp_sk->srtt >> 3, tcp_sk->rttvar >> 2, tcp_sk->rto);
}

static void tcp_start_rtt_measurement(tcp_sock_t *tcp_sk, uint32_t seq) {
  if (tcp_sk->rtt_seq == 0) {
    tcp_sk->rtt_seq = seq;
    tcp_sk->rtt_time = clock_get_nanos();
  }
}

static void tcp_stop_rtt_measurement(tcp_sock_t *tcp_sk, uint32_t ack) {
  if (tcp_sk->rtt_seq && tcp_seq_leq(tcp_sk->rtt_seq, ack)) {
    uint64_t now = clock_get_nanos();
    uint64_t rtt_ns = now - tcp_sk->rtt_time;
    uint32_t rtt_ms = rtt_ns / 1000000;

    if (rtt_ms > 0) {
      tcp_update_rto(tcp_sk, rtt_ms);
    }

    tcp_sk->rtt_seq = 0;
    tcp_sk->rtt_time = 0;
  }
}

// MARK: Checksum Calculation

static uint16_t tcp_checksum(uint32_t saddr, uint32_t daddr, struct tcphdr *tcph, size_t len) {
  uint32_t sum = 0;

  // pseudo-header: add all values as they appear in network byte order
  sum += (saddr >> 16) & 0xFFFF;
  sum += saddr & 0xFFFF;
  sum += (daddr >> 16) & 0xFFFF;
  sum += daddr & 0xFFFF;
  sum += IPPROTO_TCP;  // protocol is single byte, pad with zero: 0x0006
  sum += len;           // length in host byte order equals network byte order for this value

  // packet data: read bytes in network byte order (same as UDP)
  uint8_t *ptr = (uint8_t *)tcph;
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

  uint16_t result = ~sum;

  kprintf("TCP_CSUM: addrs=%08x->%08x len=%zu cksum=%04x\n", saddr, daddr, len, result);

  return result;
}

//
// MARK: Timer Management
//

static void tcp_retransmit_timeout(alarm_t *alarm, tcp_sock_t *tcp_sk);

static void tcp_start_retrans_timer(tcp_sock_t *tcp_sk) {
  // cancel existing timer if any
  if (tcp_sk->retrans_alarm_id) {
    alarm_unregister(tcp_sk->retrans_alarm_id, NULL);
    tcp_sk->retrans_alarm_id = 0;
  }

  // calculate timeout with exponential backoff
  uint32_t timeout_ms = tcp_sk->rto;
  if (tcp_sk->retrans_count > 0) {
    timeout_ms <<= tcp_sk->retrans_count;  // exponential backoff: 2^n
    if (timeout_ms > TCP_MAX_RTO) {
      timeout_ms = TCP_MAX_RTO;
    }
  }


  // create and register new alarm, callback holds reference to tcp_sk
  struct callback alarm_cb = alarm_cb(tcp_retransmit_timeout, tcp_sock_getref(tcp_sk));
  alarm_t *alarm = alarm_alloc_relative(timeout_ms * 1000000ULL, alarm_cb);
  if (alarm) {
    id_t alarm_id = alarm_register(alarm);
    if (alarm_id == 0) {
      EPRINTF("failed to register retransmission alarm\n");
      alarm_free(&alarm);
      tcp_sock_putref(&tcp_sk);
      return;
    }

    tcp_sk->retrans_alarm_id = alarm_id;
    DPRINTF("started retrans timer: timeout=%u ms\n", timeout_ms);
  } else {
    EPRINTF("failed to allocate retransmission alarm\n");
    tcp_sock_putref(&tcp_sk);
  }
}

static void tcp_stop_retrans_timer(tcp_sock_t *tcp_sk) {
  // atomically read and clear the alarm ID
  id_t alarm_id = atomic_xchg(&tcp_sk->retrans_alarm_id, 0);
  if (alarm_id != 0) {
    struct callback cb;
    if (alarm_unregister(alarm_id, &cb) == 0) {
      tcp_sock_t *alarm_tcp_sk = (tcp_sock_t *)cb.args[0];
      tcp_sock_putref(&alarm_tcp_sk);
    }
    DPRINTF("stopped retrans timer\n");
  }
}

// retransmission timeout callback
static void tcp_retransmit_timeout(alarm_t *alarm, __ref tcp_sock_t *tcp_sk) {
  mtx_lock(&tcp_sk->lock);

  // check if alarm was already cancelled (another alarm fired first)
  if (tcp_sk->retrans_alarm_id == 0) {
    mtx_unlock(&tcp_sk->lock);
    tcp_sock_putref(&tcp_sk);
    return;
  }

  // clear the alarm id now that we're handling it
  tcp_sk->retrans_alarm_id = 0;

  // don't retransmit if socket is already closed
  if (tcp_sk->state == TCP_CLOSED) {
    mtx_unlock(&tcp_sk->lock);
    tcp_sock_putref(&tcp_sk);
    return;
  }

  DPRINTF("retransmission timeout (count=%u)\n", tcp_sk->retrans_count);

  // check if we've exceeded max retransmissions
  if (tcp_sk->retrans_count >= TCP_MAX_RETRANS) {
    DPRINTF("max retransmissions exceeded, closing connection\n");
    tcp_set_state(tcp_sk, TCP_CLOSED);
    // TODO: notify application of connection failure
    mtx_unlock(&tcp_sk->lock);
    tcp_sock_putref(&tcp_sk);
    return;
  }

  tcp_sk->retrans_count++;

  // retransmit the oldest unacknowledged segment
  sk_buff_t *skb = LIST_FIRST(&tcp_sk->retrans_queue);
  if (skb) {
    sk_buff_t *retrans_skb = skb_clone(skb);
    if (retrans_skb) {
      struct tcphdr *tcph = (struct tcphdr *)retrans_skb->data;
      uint32_t seq = ntohl(tcph->seq);
      DPRINTF("retransmitting segment (seq=%u)\n", seq);

      tcph->check = 0;
      size_t tcp_len = retrans_skb->len;
      // tcp_checksum expects addresses in HOST byte order (like UDP)
      tcph->check = htons(tcp_checksum(ntohl(tcp_sk->saddr), ntohl(tcp_sk->daddr), tcph, tcp_len));

      // ip_output expects host byte order
      uint32_t saddr = ntohl(tcp_sk->saddr);
      uint32_t daddr = ntohl(tcp_sk->daddr);
      route_t *route = ip_route_lookup(daddr);
      if (route) {
        ip_output(retrans_skb, saddr, daddr, IPPROTO_TCP, route->dev);
      } else {
        skb_free(&retrans_skb);
      }
    }
  }

  tcp_start_retrans_timer(tcp_sk);
  mtx_unlock(&tcp_sk->lock);

  tcp_sock_putref(&tcp_sk);
}

//
// MARK: Retransmission Queue Management
//

static void tcp_add_to_retrans_queue(tcp_sock_t *tcp_sk, sk_buff_t *skb, uint32_t seq, uint32_t ack, uint16_t flags) {
  sk_buff_t *clone = skb_clone(skb);
  ASSERT(clone != NULL);

  // add TCP header to the clone so we can retransmit it later
  struct tcphdr *tcph = skb_push(clone, sizeof(struct tcphdr));
  tcph->source = tcp_sk->sport;
  tcph->dest = tcp_sk->dport;
  tcph->seq = htonl(seq);
  tcph->ack_seq = htonl(ack);
  tcph->flags = htons(TCP_DOFF_SET(5) | flags);
  tcph->window = htons(tcp_sk->rcv_wnd);
  tcph->check = 0;
  tcph->urg_ptr = 0;

  LIST_ADD(&tcp_sk->retrans_queue, clone, list);
  tcp_sk->retrans_queue_len++;

  if (tcp_sk->retrans_alarm_id == 0) {
    tcp_start_retrans_timer(tcp_sk);
  }
}

static void tcp_clean_retrans_queue(tcp_sock_t *tcp_sk, uint32_t ack) {
  bool freed_any = false;

  // remove acknowledged packets from retransmission queue
  LIST_FOR_IN_SAFE(skb, &tcp_sk->retrans_queue, list) {
    struct tcphdr *tcph = (struct tcphdr *)skb->data;
    uint32_t seq_end = ntohl(tcph->seq) + (skb->len - sizeof(struct tcphdr));

    uint16_t flags = TCP_FLAGS_GET(ntohs(tcph->flags));
    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
      seq_end++;
    }

    if (tcp_seq_leq(seq_end, ack)) {
      // packet acknowledged, remove from queue
      LIST_REMOVE(&tcp_sk->retrans_queue, skb, list);
      tcp_sk->retrans_queue_len--;
      skb_free(&skb);
      freed_any = true;
    } else {
      // not yet acknowledged
      break;
    }
  }

  if (freed_any) {
    cond_signal(&tcp_sk->send_cond);
  }

  // stop timer if queue is empty
  if (LIST_EMPTY(&tcp_sk->retrans_queue)) {
    tcp_sk->retrans_count = 0;
    tcp_stop_retrans_timer(tcp_sk);
  }
}

//
// MARK: Port Management
//

static uint16_t tcp_get_port() {
  mtx_lock(&port_lock);

  index_t port = bitmap_get_set_free_range(port_bitmap, next_ephemeral_port, TCP_EPHEMERAL_MIN, TCP_EPHEMERAL_MAX);
  if (port >= 0) {
    next_ephemeral_port = (uint16_t)(port + 1);
    if (next_ephemeral_port > TCP_EPHEMERAL_MAX) {
      next_ephemeral_port = TCP_EPHEMERAL_MIN;
    }
  }

  mtx_unlock(&port_lock);
  return (port >= 0) ? (uint16_t)port : 0;
}

static int tcp_check_port(uint16_t port) {
  mtx_lock(&port_lock);
  int result = bitmap_get(port_bitmap, port);
  mtx_unlock(&port_lock);
  return result;
}

//
// MARK: Packet Transmission
//

static int tcp_transmit_skb(tcp_sock_t *tcp_sk, sk_buff_t *skb, uint32_t seq, uint32_t ack, uint16_t flags) {
  sk_buff_t *tx_skb = skb_clone(skb);
  if (!tx_skb) {
    return -ENOMEM;
  }

  struct tcphdr *tcph = skb_push(tx_skb, sizeof(struct tcphdr));

  tcph->source = tcp_sk->sport;
  tcph->dest = tcp_sk->dport;
  tcph->seq = htonl(seq);
  tcph->ack_seq = htonl(ack);
  tcph->flags = htons(TCP_DOFF_SET(5) | flags);
  tcph->window = htons(tcp_sk->rcv_wnd);
  tcph->check = 0;
  tcph->urg_ptr = 0;

  // tcp_checksum expects addresses in HOST byte order (like UDP)
  tcph->check = htons(tcp_checksum(ntohl(tcp_sk->saddr), ntohl(tcp_sk->daddr), tcph, tx_skb->len));

  uint32_t daddr = ntohl(tcp_sk->daddr);
  uint32_t saddr = ntohl(tcp_sk->saddr);
  route_t *route = ip_route_lookup(daddr);
  if (!route) {
    skb_free(&tx_skb);
    return -EHOSTUNREACH;
  }

  return ip_output(tx_skb, saddr, daddr, IPPROTO_TCP, route->dev);
}

static int tcp_send_syn(tcp_sock_t *tcp_sk) {
  sk_buff_t *skb = skb_alloc(0);
  if (!skb) {
    return -ENOMEM;
  }

  int ret = tcp_transmit_skb(tcp_sk, skb, tcp_sk->iss, 0, TCP_FLAG_SYN);
  if (ret == 0) {
    tcp_add_to_retrans_queue(tcp_sk, skb, tcp_sk->iss, 0, TCP_FLAG_SYN);
    tcp_start_rtt_measurement(tcp_sk, tcp_sk->iss);
  }
  skb_free(&skb);
  return ret;
}

static int tcp_send_synack(tcp_sock_t *tcp_sk) {
  sk_buff_t *skb = skb_alloc(0);
  if (!skb) {
    return -ENOMEM;
  }

  int ret = tcp_transmit_skb(tcp_sk, skb, tcp_sk->iss, tcp_sk->rcv_nxt, TCP_FLAG_SYN | TCP_FLAG_ACK);
  if (ret == 0) {
    tcp_add_to_retrans_queue(tcp_sk, skb, tcp_sk->iss, tcp_sk->rcv_nxt, TCP_FLAG_SYN | TCP_FLAG_ACK);
    tcp_start_rtt_measurement(tcp_sk, tcp_sk->iss);
  }
  skb_free(&skb);
  return ret;
}

static int tcp_send_ack(tcp_sock_t *tcp_sk) {
  sk_buff_t *skb = skb_alloc(0);
  if (!skb) {
    return -ENOMEM;
  }

  int ret = tcp_transmit_skb(tcp_sk, skb, tcp_sk->snd_nxt, tcp_sk->rcv_nxt, TCP_FLAG_ACK);
  skb_free(&skb);
  return ret;
}

static int tcp_send_rst(tcp_sock_t *tcp_sk) {
  sk_buff_t *skb = skb_alloc(0);
  if (!skb) {
    return -ENOMEM;
  }

  int ret = tcp_transmit_skb(tcp_sk, skb, tcp_sk->snd_nxt, tcp_sk->rcv_nxt, TCP_FLAG_RST | TCP_FLAG_ACK);
  skb_free(&skb);
  return ret;
}

static int tcp_send_fin(tcp_sock_t *tcp_sk) {
  sk_buff_t *skb = skb_alloc(0);
  if (!skb) {
    return -ENOMEM;
  }

  int ret = tcp_transmit_skb(tcp_sk, skb, tcp_sk->snd_nxt, tcp_sk->rcv_nxt, TCP_FLAG_FIN | TCP_FLAG_ACK);
  if (ret == 0) {
    tcp_add_to_retrans_queue(tcp_sk, skb, tcp_sk->snd_nxt, tcp_sk->rcv_nxt, TCP_FLAG_FIN | TCP_FLAG_ACK);
    tcp_sk->snd_nxt++;
  }
  skb_free(&skb);
  return ret;
}

//
// MARK: Socket Lookup
//

static tcp_sock_t *tcp_lookup_sock(uint32_t saddr, uint16_t sport, uint32_t daddr, uint16_t dport) {
  mtx_lock(&socket_lock);

  // try exact match on full 4-tuple for established connections
  tcp_sock_t *tcp_sk = LIST_FIND(_sk, &tcp_sockets, link,
    (_sk->state != TCP_LISTEN && _sk->sport == sport && _sk->dport == dport &&
     _sk->saddr == saddr && _sk->daddr == daddr));
  if (tcp_sk) {
    mtx_unlock(&socket_lock);
    return tcp_sk;
  }

  // try partial match allowing wildcard addresses for connected sockets
  tcp_sk = LIST_FIND(_sk, &tcp_sockets, link,
    (_sk->state != TCP_LISTEN && _sk->sport == sport && _sk->dport == dport &&
     (_sk->saddr == INADDR_ANY || _sk->saddr == saddr) &&
     (_sk->daddr == INADDR_ANY || _sk->daddr == daddr)));
  if (tcp_sk) {
    mtx_unlock(&socket_lock);
    return tcp_sk;
  }

  // try listening socket on matching port
  tcp_sk = LIST_FIND(_sk, &tcp_sockets, link,
    (_sk->state == TCP_LISTEN && _sk->sport == sport &&
     (_sk->saddr == INADDR_ANY || _sk->saddr == saddr)));
  if (tcp_sk) {
    mtx_unlock(&socket_lock);
    return tcp_sk;
  }

  mtx_unlock(&socket_lock);
  return NULL;
}

//
// MARK: TCP Socket Management
//

__ref tcp_sock_t *tcp_sock_alloc() {
  tcp_sock_t *tcp_sk = pool_alloc(tcp_pool, sizeof(tcp_sock_t));
  if (!tcp_sk) {
    return NULL;
  }

  tcp_sk->saddr = INADDR_ANY;
  tcp_sk->sport = 0;
  tcp_sk->daddr = INADDR_ANY;
  tcp_sk->dport = 0;

  tcp_sk->state = TCP_CLOSED;
  tcp_sk->rcv_wnd = TCP_DEFAULT_WINDOW;
  tcp_sk->snd_wnd = TCP_DEFAULT_WINDOW;
  tcp_sk->rto = TCP_INITIAL_RTO;
  tcp_sk->cwnd = TCP_MSS;
  tcp_sk->ssthresh = TCP_MAX_WINDOW;
  tcp_sk->mss = TCP_MSS;

  mtx_init(&tcp_sk->lock, MTX_RECURSIVE, "tcp_sock");
  cond_init(&tcp_sk->connect_cond, "tcp_connect");
  cond_init(&tcp_sk->accept_cond, "tcp_accept");
  cond_init(&tcp_sk->recv_cond, "tcp_recv");
  cond_init(&tcp_sk->send_cond, "tcp_send");
  knlist_init(&tcp_sk->knlist, &tcp_sk->lock.lo);
  initref(tcp_sk);

  return tcp_sk;
}

void _tcp_sock_cleanup(__move tcp_sock_t **tcp_skp) {
  tcp_sock_t *tcp_sk = moveref(*tcp_skp);
  if (!tcp_sk) {
    return;
  }

  ASSERT(read_refcount(tcp_sk) == 0);

  if (tcp_sk->sport) {
    mtx_lock(&port_lock);
    bitmap_clear(port_bitmap, ntohs(tcp_sk->sport));
    mtx_unlock(&port_lock);
  }

  tcp_stop_retrans_timer(tcp_sk);
  if (tcp_sk->time_wait_alarm_id) {
    alarm_unregister(tcp_sk->time_wait_alarm_id, NULL);
  }

  // clean up queues
  mtx_lock(&tcp_sk->lock);
  LIST_FOR_IN_SAFE(skb, &tcp_sk->send_queue, list) {
    LIST_REMOVE(&tcp_sk->send_queue, skb, list);
    skb_free(&skb);
  }
  LIST_FOR_IN_SAFE(skb, &tcp_sk->retrans_queue, list) {
    LIST_REMOVE(&tcp_sk->retrans_queue, skb, list);
    skb_free(&skb);
  }
  LIST_FOR_IN_SAFE(skb, &tcp_sk->recv_queue, list) {
    LIST_REMOVE(&tcp_sk->recv_queue, skb, list);
    skb_free(&skb);
  }
  LIST_FOR_IN_SAFE(skb, &tcp_sk->ofo_queue, list) {
    LIST_REMOVE(&tcp_sk->ofo_queue, skb, list);
    skb_free(&skb);
  }
  LIST_FOR_IN_SAFE(conn, &tcp_sk->accept_queue, accept_link) {
    LIST_REMOVE(&tcp_sk->accept_queue, conn, accept_link);
    tcp_sock_putref(&conn);
  }
  mtx_unlock(&tcp_sk->lock);

  cond_destroy(&tcp_sk->connect_cond);
  cond_destroy(&tcp_sk->accept_cond);
  cond_destroy(&tcp_sk->recv_cond);
  cond_destroy(&tcp_sk->send_cond);

  pool_free(tcp_pool, tcp_sk);
}

static int tcp_queue_sendmsg(tcp_sock_t *tcp_sk, struct msghdr *msg, size_t len) {
  size_t total_queued = 0;
  size_t remaining = len;

  for (size_t i = 0; i < msg->msg_iovlen && remaining > 0; i++) {
    struct iovec *iov = &msg->msg_iov[i];
    size_t iov_remaining = iov->iov_len < remaining ? iov->iov_len : remaining;

    if (iov_remaining == 0) {
      continue;
    }

    size_t iov_offset = 0;
    while (iov_offset < iov_remaining) {
      size_t chunk_size = iov_remaining - iov_offset;
      if (chunk_size > tcp_sk->mss) {
        chunk_size = tcp_sk->mss;
      }

      sk_buff_t *skb = skb_alloc(chunk_size);
      if (!skb) {
        return total_queued > 0 ? (int)total_queued : -ENOMEM;
      }

      size_t copied = skb_copy_from_iovec(skb, iov, iov_offset, chunk_size);
      if (copied != chunk_size) {
        skb_free(&skb);
        return total_queued > 0 ? (int)total_queued : -EFAULT;
      }

      LIST_ADD(&tcp_sk->send_queue, skb, list);
      tcp_sk->send_queue_len++;

      total_queued += copied;
      iov_offset += copied;
    }

    remaining -= iov_remaining;
  }

  return (int)total_queued;
}

static size_t tcp_drain_recvmsg(tcp_sock_t *tcp_sk, struct msghdr *msg, size_t len) {
  size_t total_read = 0;
  size_t remaining = len;

  for (size_t i = 0; i < msg->msg_iovlen && remaining > 0 && !LIST_EMPTY(&tcp_sk->recv_queue); i++) {
    struct iovec *iov = &msg->msg_iov[i];
    size_t iov_offset = 0;

    while (iov_offset < iov->iov_len && remaining > 0 && !LIST_EMPTY(&tcp_sk->recv_queue)) {
      sk_buff_t *skb = LIST_FIRST(&tcp_sk->recv_queue);
      size_t to_copy = remaining;
      if (to_copy > iov->iov_len - iov_offset) {
        to_copy = iov->iov_len - iov_offset;
      }

      size_t copied = skb_copy_to_iovec(skb, iov, iov_offset, to_copy, true);

      total_read += copied;
      iov_offset += copied;
      remaining -= copied;

      if (skb->len == 0) {
        LIST_REMOVE(&tcp_sk->recv_queue, skb, list);
        tcp_sk->recv_queue_len--;
        skb_free(&skb);
      }
    }
  }

  return total_read;
}

//
// MARK: TCP Protocol Operations
//

static int tcp_bind(tcp_sock_t *tcp_sk, uint32_t addr, uint16_t port) {
  ASSERT(tcp_sk != NULL);
  mtx_lock(&tcp_sk->lock);

  if (tcp_sk->bound) {
    mtx_unlock(&tcp_sk->lock);
    return -EINVAL;
  }

  // check if port is available
  if (port != 0 && tcp_check_port(port)) {
    mtx_unlock(&tcp_sk->lock);
    return -EADDRINUSE;
  }

  // allocate ephemeral port if needed
  if (port == 0) {
    port = tcp_get_port();
    if (port == 0) {
      mtx_unlock(&tcp_sk->lock);
      return -EADDRNOTAVAIL;
    }
  } else {
    mtx_lock(&port_lock);
    bitmap_set(port_bitmap, port);
    mtx_unlock(&port_lock);
  }

  tcp_sk->saddr = htonl(addr);
  tcp_sk->sport = htons(port);
  tcp_sk->bound = 1;

  mtx_unlock(&tcp_sk->lock);

  DPRINTF("bound socket to {:ip}:%u\n", addr, port);

  return 0;
}

//
// MARK: Socket Protocol Operations
//

static int inet_stream_create(sock_t *sock, int protocol) {
  if (protocol != 0 && protocol != IPPROTO_TCP) {
    return -EPROTONOSUPPORT;
  }

  tcp_sock_t *tcp_sk = tcp_sock_alloc();
  if (!tcp_sk) {
    return -ENOMEM;
  }

  sock->sk = tcp_sk;
  sock->knlist = &tcp_sk->knlist;

  // add to socket list
  mtx_lock(&socket_lock);
  LIST_ADD(&tcp_sockets, tcp_sk, link);
  mtx_unlock(&socket_lock);

  DPRINTF("created TCP socket\n");
  return 0;
}

static int inet_stream_release(sock_t *sock) {
  if (!sock->sk) {
    return 0;
  }

  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;

  mtx_lock(&tcp_sk->lock);
  tcp_sk->closing = true;

  // if socket was already in CLOSED state, it won't transition TO closed,
  // so we need to remove it from the list after unlocking
  bool was_already_closed = (tcp_sk->state == TCP_CLOSED);

  // wake up any blocked operations
  cond_broadcast(&tcp_sk->connect_cond);
  cond_broadcast(&tcp_sk->accept_cond);
  cond_broadcast(&tcp_sk->recv_cond);
  cond_broadcast(&tcp_sk->send_cond);

  switch (tcp_sk->state) {
    case TCP_CLOSED:
      break;
    case TCP_LISTEN:
      // close listening socket immediately
      tcp_set_state(tcp_sk, TCP_CLOSED);

      LIST_FOR_IN_SAFE(queued, &tcp_sk->accept_queue, accept_link) {
        LIST_REMOVE(&tcp_sk->accept_queue, queued, accept_link);
        tcp_sock_putref(&queued);
      }
      tcp_sk->accept_queue_len = 0;
      break;
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED:
      // abort connection attempt - send RST to avoid retransmissions
      tcp_send_rst(tcp_sk);
      tcp_set_state(tcp_sk, TCP_CLOSED);
      break;
    case TCP_ESTABLISHED: {
      // initiate graceful close
      int fin_ret = tcp_send_fin(tcp_sk);
      if (fin_ret == 0) {
        tcp_set_state(tcp_sk, TCP_FIN_WAIT_1);
      } else {
        // if FIN send fails, abort with RST
        tcp_send_rst(tcp_sk);
        tcp_set_state(tcp_sk, TCP_CLOSED);
      }
      break;
    }
    case TCP_CLOSE_WAIT: {
      // peer already closed, send our FIN
      int fin_ret2 = tcp_send_fin(tcp_sk);
      if (fin_ret2 == 0) {
        tcp_set_state(tcp_sk, TCP_LAST_ACK);
      } else {
        tcp_set_state(tcp_sk, TCP_CLOSED);
      }
      break;
    }
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
      // already closing, let it continue
      break;
    case TCP_TIME_WAIT:
      // in TIME_WAIT, can close immediately
      tcp_set_state(tcp_sk, TCP_CLOSED);
      break;
    default:
      EPRINTF("unknown TCP state %d during release\n", tcp_sk->state);
      tcp_set_state(tcp_sk, TCP_CLOSED);
      break;
  }

  // cancel any active timers before cleanup
  tcp_stop_retrans_timer(tcp_sk);
  if (tcp_sk->time_wait_alarm_id) {
    alarm_unregister(tcp_sk->time_wait_alarm_id, NULL);
    tcp_sk->time_wait_alarm_id = 0;
  }

  if (tcp_sk->bound) {
    uint16_t port = ntohs(tcp_sk->sport);
    mtx_lock(&port_lock);
    bitmap_clear(port_bitmap, port);
    mtx_unlock(&port_lock);
    tcp_sk->bound = 0;
  }

  // clean up queues
  LIST_FOR_IN_SAFE(skb, &tcp_sk->send_queue, list) {
    LIST_REMOVE(&tcp_sk->send_queue, skb, list);
    skb_free(&skb);
  }
  tcp_sk->send_queue_len = 0;

  LIST_FOR_IN_SAFE(skb, &tcp_sk->retrans_queue, list) {
    LIST_REMOVE(&tcp_sk->retrans_queue, skb, list);
    skb_free(&skb);
  }
  tcp_sk->retrans_queue_len = 0;

  LIST_FOR_IN_SAFE(skb, &tcp_sk->recv_queue, list) {
    LIST_REMOVE(&tcp_sk->recv_queue, skb, list);
    skb_free(&skb);
  }
  tcp_sk->recv_queue_len = 0;

  LIST_FOR_IN_SAFE(skb, &tcp_sk->ofo_queue, list) {
    LIST_REMOVE(&tcp_sk->ofo_queue, skb, list);
    skb_free(&skb);
  }
  tcp_sk->ofo_queue_len = 0;

  LIST_FOR_IN_SAFE(conn, &tcp_sk->accept_queue, accept_link) {
    LIST_REMOVE(&tcp_sk->accept_queue, conn, accept_link);
    tcp_sock_putref(&conn);
  }
  tcp_sk->accept_queue_len = 0;

  mtx_unlock(&tcp_sk->lock);

  // socket will be removed from tcp_sockets list when it transitions to CLOSED
  // in tcp_set_state, but if it was already CLOSED before we entered, remove it now
  if (was_already_closed) {
    mtx_lock(&socket_lock);
    LIST_REMOVE(&tcp_sockets, tcp_sk, link);
    mtx_unlock(&socket_lock);
  }

  tcp_sock_putref(&tcp_sk);
  sock->sk = NULL;

  return 0;
}

static int inet_stream_bind(sock_t *sock, struct sockaddr *addr, int addrlen) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  ASSERT(addr != NULL);
  if (addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  if (sin->sin_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  tcp_sock_t *tcp_sk = sock->sk;
  return tcp_bind(tcp_sk, ntohl(sin->sin_addr.s_addr), ntohs(sin->sin_port));
}

static int inet_stream_connect(sock_t *sock, struct sockaddr *addr, int addrlen, int flags) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  ASSERT(addr != NULL);
  if (addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  if (sin->sin_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;
  mtx_lock(&tcp_sk->lock);

  if (tcp_sk->state != TCP_CLOSED) {
    mtx_unlock(&tcp_sk->lock);
    return -EISCONN;
  }

  // bind to ephemeral port if not already bound
  if (!tcp_sk->bound) {
    uint16_t port = tcp_get_port();
    if (port == 0) {
      mtx_unlock(&tcp_sk->lock);
      return -EADDRNOTAVAIL;
    }
    tcp_sk->sport = htons(port);

    route_t *route = ip_route_lookup(ntohl(sin->sin_addr.s_addr));
    if (route && route->dev && !LIST_EMPTY(&route->dev->ip_addrs)) {
      in_ifaddr_t *ifa = LIST_FIRST(&route->dev->ip_addrs);
      tcp_sk->saddr = htonl(ifa->ifa_address);
    } else {
      tcp_sk->saddr = htonl(0x0a000215); // default to 10.0.2.15
    }
    tcp_sk->bound = 1;

    mtx_lock(&port_lock);
    bitmap_set(port_bitmap, port);
    mtx_unlock(&port_lock);
  }

  // set destination
  tcp_sk->daddr = sin->sin_addr.s_addr;
  tcp_sk->dport = sin->sin_port;

  // initialize sequence numbers
  tcp_sk->iss = tcp_new_isn();
  tcp_sk->snd_una = tcp_sk->iss;
  tcp_sk->snd_nxt = tcp_sk->iss + 1;

  tcp_set_state(tcp_sk, TCP_SYN_SENT);
  tcp_sk->connected = 1;

  int ret = tcp_send_syn(tcp_sk);
  if (ret < 0) {
    return ret;
  }

  DPRINTF("connecting to {:ip}:%u\n", tcp_sk->daddr, ntohs(tcp_sk->dport));

  // wait for connection to establish unless non-blocking
  if (!(flags & MSG_DONTWAIT)) {
    while (tcp_sk->state == TCP_SYN_SENT && !tcp_sk->closing) {
      DPRINTF("waiting for connection to establish...\n");
      ret = cond_wait_sig(&tcp_sk->connect_cond, &tcp_sk->lock);
      if (ret != 0) {
        mtx_unlock(&tcp_sk->lock);
        return -EINTR;
      }
    }

    if (tcp_sk->closing || tcp_sk->state != TCP_ESTABLISHED) {
      mtx_unlock(&tcp_sk->lock);
      return -ECONNREFUSED;
    }
  }

  mtx_unlock(&tcp_sk->lock);
  return 0;
}

static int inet_stream_listen(sock_t *sock, int backlog) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;
  mtx_lock(&tcp_sk->lock);

  if (!tcp_sk->bound || (tcp_sk->state != TCP_CLOSED && tcp_sk->state != TCP_LISTEN)) {
    mtx_unlock(&tcp_sk->lock);
    return -EINVAL;
  }

  tcp_sk->accept_queue_max = backlog > 0 ? backlog : 8;
  tcp_set_state(tcp_sk, TCP_LISTEN);
  mtx_unlock(&tcp_sk->lock);

  DPRINTF("socket now listening (backlog=%zu)\n", tcp_sk->accept_queue_max);
  return 0;
}

static int inet_stream_accept(sock_t *sock, sock_t *newsock, int flags) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  ASSERT(newsock != NULL);
  int res = 0;
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;
  mtx_lock(&tcp_sk->lock);

  if (tcp_sk->state != TCP_LISTEN) {
    mtx_unlock(&tcp_sk->lock);
    return -EINVAL;
  }

  // wait for connections if accept queue is empty
  while (LIST_EMPTY(&tcp_sk->accept_queue) && !tcp_sk->closing) {
    if (flags & MSG_DONTWAIT) {
      goto_res(ret_unlock, -EAGAIN);
    }

    DPRINTF("no connections in accept queue, blocking...\n");
    int ret = cond_wait_sig(&tcp_sk->accept_cond, &tcp_sk->lock);
    if (ret != 0) {
      goto_res(ret_unlock, -EINTR);
    }
  }

  if (tcp_sk->closing) {
    goto_res(ret_unlock, -ECONNABORTED);
  } else if (LIST_EMPTY(&tcp_sk->accept_queue)) {
    goto_res(ret_unlock, -EAGAIN);
  }

  // dequeue the first connection
  tcp_sock_t *new_sk = LIST_REMOVE_FIRST(&tcp_sk->accept_queue, accept_link);
  tcp_sk->accept_queue_len--;

  newsock->sk = moveref(new_sk);
  newsock->knlist = &((tcp_sock_t *)newsock->sk)->knlist;

  DPRINTF("accepted connection\n");
  res = 0; // success
LABEL(ret_unlock);
  mtx_unlock(&tcp_sk->lock);
  return res;
}

static int inet_stream_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  ASSERT(msg != NULL);
  int res = 0;
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;
  mtx_lock(&tcp_sk->lock);
  DPRINTF("sendmsg: state=%s\n", tcp_state_str(tcp_sk->state));

  if (tcp_sk->state != TCP_ESTABLISHED) {
    EPRINTF("sendmsg: returning ENOTCONN for state=%s\n", tcp_state_str(tcp_sk->state));
    goto_res(ret_unlock, -ENOTCONN);
  }

  if (len == 0) {
    goto_res(ret_unlock, 0);
  }

  // copy data from iovec into send buffer
  int queued = tcp_queue_sendmsg(tcp_sk, msg, len);
  if (queued < 0) {
    goto_res(ret_unlock, queued);
  }

  size_t total_sent = (size_t)queued;

  // transmit queued segments
  sk_buff_t *skb;
  while ((skb = LIST_FIRST(&tcp_sk->send_queue)) != NULL) {
    LIST_REMOVE(&tcp_sk->send_queue, skb, list);
    tcp_sk->send_queue_len--;

    size_t data_len = skb->len;
    uint32_t seq = tcp_sk->snd_nxt;

    int ret = tcp_transmit_skb(tcp_sk, skb, seq, tcp_sk->rcv_nxt, TCP_FLAG_ACK | TCP_FLAG_PSH);

    if (ret < 0) {
      skb_free(&skb);
      DPRINTF("failed to transmit segment: %d\n", ret);
      res = total_sent > 0 ? (int)total_sent : ret;
      goto ret_unlock;
    }

    // add to retransmission queue and start RTT measurement if needed
    tcp_add_to_retrans_queue(tcp_sk, skb, seq, tcp_sk->rcv_nxt, TCP_FLAG_ACK | TCP_FLAG_PSH);
    if (tcp_sk->rtt_seq == 0) {
      tcp_start_rtt_measurement(tcp_sk, seq);
    }

    // advance send sequence number
    tcp_sk->snd_nxt += data_len;
    DPRINTF("sent %zu bytes (seq=%u, next=%u)\n", data_len, seq, tcp_sk->snd_nxt);
  }

  res = (int)total_sent; // success
LABEL(ret_unlock);
  mtx_unlock(&tcp_sk->lock);
  return res;
}

static int inet_stream_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  ASSERT(msg != NULL);
  int res = 0;
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;
  mtx_lock(&tcp_sk->lock);

  if (tcp_sk->state != TCP_ESTABLISHED && tcp_sk->state != TCP_CLOSE_WAIT) {
    EPRINTF("recvmsg: returning ENOTCONN for state=%s\n", tcp_state_str(tcp_sk->state));
    goto_res(ret_unlock, -ENOTCONN);
  }

  if (len == 0) {
    goto_res(ret_unlock, 0);
  }

  // wait for data if receive queue is empty
  while (LIST_EMPTY(&tcp_sk->recv_queue) && !tcp_sk->closing) {
    // check for EOF (FIN received)
    if (tcp_sk->state == TCP_CLOSE_WAIT || tcp_sk->state == TCP_LAST_ACK ||
        tcp_sk->state == TCP_TIME_WAIT || tcp_sk->state == TCP_CLOSED) {
      goto_res(ret_unlock, 0);
    }

    if (flags & MSG_DONTWAIT) {
      goto_res(ret_unlock, -EAGAIN);
    }

    DPRINTF("no data available, blocking...\n");
    int ret = cond_wait_sig(&tcp_sk->recv_cond, &tcp_sk->lock);
    if (ret != 0) {
      goto_res(ret_unlock, -EINTR);
    }
  }

  if (tcp_sk->closing) {
    // EOF on closing
    goto_res(ret_unlock, -EAGAIN);
  }

  if (LIST_EMPTY(&tcp_sk->recv_queue)) {
    // still no data after waking up (e.g., spurious wakeup or EOF)
    if (tcp_sk->state == TCP_CLOSE_WAIT || tcp_sk->state == TCP_LAST_ACK ||
        tcp_sk->state == TCP_TIME_WAIT || tcp_sk->state == TCP_CLOSED) {
      goto_res(ret_unlock, 0); // EOF
    }
    goto_res(ret_unlock, -EAGAIN);
  }

  // copy data from receive queue to user buffer
  size_t total_read = tcp_drain_recvmsg(tcp_sk, msg, len);

  DPRINTF("received %zu bytes\n", total_read);
  res = (int)total_read; // success
LABEL(ret_unlock);
  mtx_unlock(&tcp_sk->lock);
  return res;
}

static int inet_stream_shutdown(sock_t *sock, int how) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  int res = 0;
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;
  mtx_lock(&tcp_sk->lock);

  if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
    mtx_unlock(&tcp_sk->lock);
    return -EINVAL;
  }

  switch (tcp_sk->state) {
    case TCP_CLOSED:
      break;
    case TCP_LISTEN:
      tcp_set_state(tcp_sk, TCP_CLOSED);
      break;
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED:
      tcp_send_rst(tcp_sk);
      tcp_set_state(tcp_sk, TCP_CLOSED);
      break;
    case TCP_ESTABLISHED:
      if (how == SHUT_WR || how == SHUT_RDWR) {
        res = tcp_send_fin(tcp_sk);
        if (res == 0) {
          tcp_set_state(tcp_sk, TCP_FIN_WAIT_1);
        }
      }
      break;
    case TCP_CLOSE_WAIT:
      if (how == SHUT_WR || how == SHUT_RDWR) {
        res = tcp_send_fin(tcp_sk);
        if (res == 0) {
          tcp_set_state(tcp_sk, TCP_LAST_ACK);
        }
      }
      break;
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
      break;
    default:
      EPRINTF("unknown TCP state %d during shutdown\n", tcp_sk->state);
      break;
  }

  mtx_unlock(&tcp_sk->lock);
  DPRINTF("shutdown(%d) completed in state %s\n", how, tcp_state_str(tcp_sk->state));
  return res;
}

static int inet_stream_getsockname(sock_t *sock, struct sockaddr *addr, socklen_t *addrlen) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;

  if (*addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  sin->sin_family = AF_INET;
  sin->sin_port = tcp_sk->sport;
  sin->sin_addr.s_addr = tcp_sk->saddr;
  *addrlen = sizeof(struct sockaddr_in);
  return 0;
}

static int inet_stream_getpeername(sock_t *sock, struct sockaddr *addr, socklen_t *addrlen) {
  ASSERT(sock != NULL);
  ASSERT(sock->sk != NULL);
  tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;

  if (!tcp_sk->connected) {
    return -ENOTCONN;
  }

  if (*addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  sin->sin_family = AF_INET;
  sin->sin_port = tcp_sk->dport;
  sin->sin_addr.s_addr = tcp_sk->daddr;
  *addrlen = sizeof(struct sockaddr_in);
  return 0;
}

const struct proto_ops tcp_stream_ops = {
  .family = AF_INET,
  .create = inet_stream_create,
  .release = inet_stream_release,
  .bind = inet_stream_bind,
  .connect = inet_stream_connect,
  .listen = inet_stream_listen,
  .accept = inet_stream_accept,
  .sendmsg = inet_stream_sendmsg,
  .recvmsg = inet_stream_recvmsg,
  .shutdown = inet_stream_shutdown,
  .getsockname = inet_stream_getsockname,
  .getpeername = inet_stream_getpeername,
};

//
// MARK: TCP Packet Processing
//

static int tcp_rcv_synsent(tcp_sock_t *tcp_sk, sk_buff_t *skb, struct tcphdr *tcph) {
  uint32_t seq = ntohl(tcph->seq);
  uint32_t ack = ntohl(tcph->ack_seq);
  uint16_t flags = TCP_FLAGS_GET(ntohs(tcph->flags));

  if (flags & TCP_FLAG_RST) {
    tcp_set_state(tcp_sk, TCP_CLOSED);
    cond_broadcast(&tcp_sk->connect_cond);
    knlist_activate_notes(&tcp_sk->knlist, 0);
    DPRINTF("connection refused (RST received)\n");
    return 0;
  }

  if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
    if (ack != tcp_sk->snd_nxt) {
      EPRINTF("received SYN-ACK with bad ack (expected %u, got %u)\n", tcp_sk->snd_nxt, ack);
      return -EINVAL;
    }

    // save peer's ISN
    tcp_sk->irs = seq;
    tcp_sk->rcv_nxt = seq + 1;
    tcp_sk->snd_una = ack;

    tcp_stop_rtt_measurement(tcp_sk, ack);
    tcp_clean_retrans_queue(tcp_sk, ack);
    tcp_send_ack(tcp_sk);

    tcp_set_state(tcp_sk, TCP_ESTABLISHED);
    DPRINTF("connection established\n");
    return 0;
  }

  EPRINTF("unexpected flags in SYN_SENT state: 0x%x\n", flags);
  return -EINVAL;
}

static int tcp_rcv_listen(tcp_sock_t *tcp_sk, sk_buff_t *skb, struct tcphdr *tcph, struct iphdr *iph) {
  uint32_t seq = ntohl(tcph->seq);
  uint16_t flags = TCP_FLAGS_GET(ntohs(tcph->flags));

  if (!(flags & TCP_FLAG_SYN)) {
    EPRINTF("received non-SYN packet in LISTEN state\n");
    return -EINVAL;
  }

  if (tcp_sk->accept_queue_len >= tcp_sk->accept_queue_max) {
    EPRINTF("accept queue full, dropping SYN\n");
    return -ENOBUFS;
  }

  tcp_sock_t *new_sk = tcp_sock_alloc();
  if (!new_sk) {
    return -ENOMEM;
  }

  new_sk->saddr = iph->daddr;
  new_sk->sport = tcp_sk->sport;
  new_sk->daddr = iph->saddr;
  new_sk->dport = tcph->source;
  new_sk->bound = 1;
  new_sk->connected = 1;

  DPRINTF("new socket: saddr=%x sport=%u, daddr=%x dport=%u\n",
          ntohl(new_sk->saddr), ntohs(new_sk->sport),
          ntohl(new_sk->daddr), ntohs(new_sk->dport));

  // save peer's ISN
  new_sk->irs = seq;
  new_sk->rcv_nxt = seq + 1;
  new_sk->iss = tcp_new_isn();
  new_sk->snd_una = new_sk->iss;
  new_sk->snd_nxt = new_sk->iss + 1;
  new_sk->parent = tcp_sk;

  mtx_lock(&socket_lock);
  LIST_ADD(&tcp_sockets, new_sk, link);
  mtx_unlock(&socket_lock);

  tcp_set_state(new_sk, TCP_SYN_RECEIVED);
  tcp_send_synack(new_sk);
  return 0;
}

static int tcp_rcv_synrecv(tcp_sock_t *tcp_sk, sk_buff_t *skb, struct tcphdr *tcph) {
  uint32_t ack = ntohl(tcph->ack_seq);
  uint16_t flags = TCP_FLAGS_GET(ntohs(tcph->flags));

  if (!(flags & TCP_FLAG_ACK)) {
    EPRINTF("received non-ACK packet in SYN_RECEIVED state\n");
    return -EINVAL;
  }

  if (ack != tcp_sk->snd_nxt) {
    EPRINTF("received ACK with bad ack (expected %u, got %u)\n", tcp_sk->snd_nxt, ack);
    return -EINVAL;
  }

  tcp_sk->snd_una = ack;
  tcp_stop_rtt_measurement(tcp_sk, ack);
  tcp_clean_retrans_queue(tcp_sk, ack);
  tcp_set_state(tcp_sk, TCP_ESTABLISHED);

  if (tcp_sk->parent) {
    tcp_sock_t *parent = tcp_sk->parent;
    mtx_lock(&parent->lock);
    LIST_ADD(&parent->accept_queue, tcp_sk, accept_link);
    parent->accept_queue_len++;
    cond_signal(&parent->accept_cond);
    mtx_unlock(&parent->lock);
  }

  return 0;
}

static int tcp_rcv_established(tcp_sock_t *tcp_sk, sk_buff_t *skb, struct tcphdr *tcph) {
  uint32_t seq = ntohl(tcph->seq);
  uint32_t ack = ntohl(tcph->ack_seq);
  uint16_t flags = TCP_FLAGS_GET(ntohs(tcph->flags));

  size_t tcp_hdr_len = TCP_DOFF_GET(ntohs(tcph->flags)) * 4UL;
  size_t data_len = skb->len - tcp_hdr_len;

  if (flags & TCP_FLAG_ACK) {
    if (tcp_seq_gt(ack, tcp_sk->snd_una) && tcp_seq_leq(ack, tcp_sk->snd_nxt)) {
      tcp_sk->snd_una = ack;
      tcp_stop_rtt_measurement(tcp_sk, ack);
      tcp_clean_retrans_queue(tcp_sk, ack);

      if (tcp_sk->state == TCP_FIN_WAIT_1) {
        tcp_set_state(tcp_sk, TCP_FIN_WAIT_2);
      } else if (tcp_sk->state == TCP_CLOSING) {
        tcp_set_state(tcp_sk, TCP_TIME_WAIT);
      } else if (tcp_sk->state == TCP_LAST_ACK) {
        tcp_set_state(tcp_sk, TCP_CLOSED);
      }
    }
  }

  if (data_len > 0) {
    if (seq == tcp_sk->rcv_nxt) {
      sk_buff_t *recv_skb = skb_alloc(data_len);
      if (!recv_skb) {
        return -ENOMEM;
      }

      memcpy(recv_skb->data, (uint8_t *)tcph + tcp_hdr_len, data_len);
      recv_skb->len = data_len;

      LIST_ADD(&tcp_sk->recv_queue, recv_skb, list);
      tcp_sk->recv_queue_len++;
      cond_signal(&tcp_sk->recv_cond);
      tcp_sk->rcv_nxt += data_len;
      tcp_send_ack(tcp_sk);
    } else if (tcp_seq_gt(seq, tcp_sk->rcv_nxt)) {
      sk_buff_t *ofo_skb = skb_alloc(data_len);
      if (!ofo_skb) {
        return -ENOMEM;
      }

      memcpy(ofo_skb->data, (uint8_t *)tcph + tcp_hdr_len, data_len);
      ofo_skb->len = data_len;
      *(uint32_t *)(ofo_skb->head) = seq;

      LIST_ADD(&tcp_sk->ofo_queue, ofo_skb, list);
      tcp_sk->ofo_queue_len++;
      tcp_send_ack(tcp_sk);
    } else {
      tcp_send_ack(tcp_sk);
    }

    sk_buff_t *ofo_skb;
    while ((ofo_skb = LIST_FIRST(&tcp_sk->ofo_queue)) != NULL) {
      uint32_t ofo_seq = *(uint32_t *)(ofo_skb->head);
      if (ofo_seq == tcp_sk->rcv_nxt) {
        LIST_REMOVE(&tcp_sk->ofo_queue, ofo_skb, list);
        tcp_sk->ofo_queue_len--;
        LIST_ADD(&tcp_sk->recv_queue, ofo_skb, list);
        tcp_sk->recv_queue_len++;
        cond_signal(&tcp_sk->recv_cond);
        tcp_sk->rcv_nxt += ofo_skb->len;
        tcp_send_ack(tcp_sk);
      } else {
        break;
      }
    }
  }

  if (flags & TCP_FLAG_FIN) {
    tcp_sk->rcv_nxt++;
    tcp_send_ack(tcp_sk);

    if (tcp_sk->state == TCP_ESTABLISHED) {
      tcp_set_state(tcp_sk, TCP_CLOSE_WAIT);
    } else if (tcp_sk->state == TCP_FIN_WAIT_1) {
      tcp_set_state(tcp_sk, TCP_CLOSING);
    } else if (tcp_sk->state == TCP_FIN_WAIT_2) {
      tcp_set_state(tcp_sk, TCP_TIME_WAIT);
    }
  }

  return 0;
}

int tcp_rcv(sk_buff_t *skb) {
  ASSERT(skb != NULL);
  if (skb->len < sizeof(struct tcphdr)) {
    skb_free(&skb);
    return -EINVAL;
  }

  struct tcphdr *tcph = (struct tcphdr *)skb->data;
  struct iphdr *iph = (struct iphdr *)skb_network_header(skb);

  // tcp_checksum expects addresses in HOST byte order (like UDP)
  uint16_t expected_csum = tcp_checksum(ntohl(iph->saddr), ntohl(iph->daddr), tcph, skb->len);
  if (expected_csum != 0) {
    DPRINTF("BAD CHECKSUM: expected=0 got=0x%04x len=%zu stored_csum=0x%04x\n",
            expected_csum, skb->len, ntohs(tcph->check));
    DPRINTF("  saddr=%08x daddr=%08x sport=%u dport=%u\n",
            iph->saddr, iph->daddr, ntohs(tcph->source), ntohs(tcph->dest));
    skb_free(&skb);
    return -EINVAL;
  }

  tcp_sock_t *tcp_sk = tcp_lookup_sock(iph->daddr, tcph->dest, iph->saddr, tcph->source);
  if (!tcp_sk) {
    uint16_t flags = TCP_FLAGS_GET(ntohs(tcph->flags));
    DPRINTF("no socket found for packet (dest={:ip}:%u, src={:ip}:%u, flags=0x%x)\n",
            ntohl(iph->daddr), ntohs(tcph->dest),
            ntohl(iph->saddr), ntohs(tcph->source), flags);
    skb_free(&skb);
    return -ENOENT;
  }

  mtx_lock(&tcp_sk->lock);
  int ret = 0;
  switch (tcp_sk->state) {
    case TCP_LISTEN:
      ret = tcp_rcv_listen(tcp_sk, skb, tcph, iph);
      break;
    case TCP_SYN_SENT:
      ret = tcp_rcv_synsent(tcp_sk, skb, tcph);
      break;
    case TCP_SYN_RECEIVED:
      ret = tcp_rcv_synrecv(tcp_sk, skb, tcph);
      break;
    case TCP_ESTABLISHED:
    case TCP_CLOSE_WAIT:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
      ret = tcp_rcv_established(tcp_sk, skb, tcph);
      break;
    case TCP_TIME_WAIT:
      if (TCP_FLAGS_GET(ntohs(tcph->flags)) & TCP_FLAG_RST) {
        tcp_set_state(tcp_sk, TCP_CLOSED);
      }
      break;
    default:
      break;
  }

  mtx_unlock(&tcp_sk->lock);
  skb_free(&skb);
  return ret;
}

//
// MARK: Module Initialization
//

void tcp_init() {
  ip_register_protocol(IPPROTO_TCP, tcp_rcv);
  inet_register_protocol(SOCK_STREAM, 0, &tcp_stream_ops);
  DPRINTF("TCP protocol initialized\n");
}
MODULE_INIT(tcp_init);
