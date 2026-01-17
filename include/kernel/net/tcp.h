//
// Created by Aaron Gill-Braun on 2025-10-03.
//

#ifndef KERNEL_NET_TCP_H
#define KERNEL_NET_TCP_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>
#include <kernel/queue.h>
#include <kernel/kevent.h>
#include <kernel/net/skbuff.h>
#include <kernel/net/socket.h>
#include <kernel/net/ip.h>

#include <linux/socket.h>

//
// TCP Protocol Implementation
//

// TCP header structure
struct tcphdr {
  uint16_t source;      // source port
  uint16_t dest;        // destination port
  uint32_t seq;         // sequence number
  uint32_t ack_seq;     // acknowledgment number
  uint16_t flags;       // data offset (4 bits) + reserved (3 bits) + flags (9 bits)
  uint16_t window;      // window size
  uint16_t check;       // checksum
  uint16_t urg_ptr;     // urgent pointer
} packed;

// TCP flags (in flags field, after data offset)
#define TCP_FLAG_FIN    0x0001
#define TCP_FLAG_SYN    0x0002
#define TCP_FLAG_RST    0x0004
#define TCP_FLAG_PSH    0x0008
#define TCP_FLAG_ACK    0x0010
#define TCP_FLAG_URG    0x0020
#define TCP_FLAG_ECE    0x0040
#define TCP_FLAG_CWR    0x0080
#define TCP_FLAG_NS     0x0100

// TCP header data offset macros (in 32-bit words)
#define TCP_DOFF_GET(flags) (((flags) >> 12) & 0xF)
#define TCP_DOFF_SET(words) (((words) & 0xF) << 12)
#define TCP_FLAGS_GET(flags) ((flags) & 0x1FF)

// TCP states
#define TCP_CLOSED          0
#define TCP_LISTEN          1
#define TCP_SYN_SENT        2
#define TCP_SYN_RECEIVED    3
#define TCP_ESTABLISHED     4
#define TCP_FIN_WAIT_1      5
#define TCP_FIN_WAIT_2      6
#define TCP_CLOSE_WAIT      7
#define TCP_CLOSING         8
#define TCP_LAST_ACK        9
#define TCP_TIME_WAIT       10

// TCP options
#define TCPOPT_EOL          0   // end of option list
#define TCPOPT_NOP          1   // no operation
#define TCPOPT_MSS          2   // maximum segment size
#define TCPOPT_WINDOW       3   // window scale
#define TCPOPT_SACK_PERM    4   // SACK permitted
#define TCPOPT_SACK         5   // SACK block
#define TCPOPT_TIMESTAMP    8   // timestamps

// TCP constants
#define TCP_MSS             1460    // default maximum segment size
#define TCP_MIN_MSS         536     // minimum MSS
#define TCP_MAX_WINDOW      65535   // maximum window without scaling
#define TCP_DEFAULT_WINDOW  8192    // default receive window
#define TCP_INITIAL_RTO     1000    // initial RTO in ms
#define TCP_MIN_RTO         200     // minimum RTO in ms
#define TCP_MAX_RTO         60000   // maximum RTO in ms
#define TCP_MSL             60000   // maximum segment lifetime in ms
#define TCP_TIMEWAIT_LEN    (2*TCP_MSL)
#define TCP_MAX_RETRANS     12      // maximum retransmissions

#define TCP_EPHEMERAL_MIN   32768
#define TCP_EPHEMERAL_MAX   65535

// TCP socket structure
typedef struct tcp_sock {
  // connection identity (network byte order)
  uint32_t saddr;           // source address
  uint16_t sport;           // source port
  uint32_t daddr;           // destination address
  uint16_t dport;           // destination port

  // connection state
  int state;                // TCP_* state constants
  int bound;                // socket is bound
  int connected;            // socket is connected

  // sequence numbers
  uint32_t snd_una;         // send unacknowledged
  uint32_t snd_nxt;         // send next
  uint32_t snd_wnd;         // send window
  uint32_t snd_wl1;         // segment seq for last window update
  uint32_t snd_wl2;         // segment ack for last window update
  uint32_t iss;             // initial send sequence number

  uint32_t rcv_nxt;         // receive next
  uint32_t rcv_wnd;         // receive window
  uint32_t irs;             // initial receive sequence number

  // retransmission
  uint32_t rto;             // retransmission timeout (ms)
  uint32_t srtt;            // smoothed round trip time (ms << 3)
  uint32_t rttvar;          // round trip time variation (ms << 2)
  uint32_t rtt_seq;         // sequence number being timed
  uint64_t rtt_time;        // time when rtt_seq was sent (ns)
  uint32_t retrans_count;   // consecutive retransmission count

  // congestion control
  uint32_t cwnd;            // congestion window
  uint32_t ssthresh;        // slow start threshold
  uint16_t mss;             // maximum segment size

  // timers
  id_t retrans_alarm_id;    // retransmission alarm ID
  id_t time_wait_alarm_id;  // TIME_WAIT alarm ID

  // send queue (data to be sent)
  LIST_HEAD(sk_buff_t) send_queue;
  size_t send_queue_len;

  // retransmission queue (sent but unacknowledged)
  LIST_HEAD(sk_buff_t) retrans_queue;
  size_t retrans_queue_len;

  // receive queue (received in-order data)
  LIST_HEAD(sk_buff_t) recv_queue;
  size_t recv_queue_len;

  // out-of-order queue
  LIST_HEAD(sk_buff_t) ofo_queue;
  size_t ofo_queue_len;

  // listen queue (for LISTEN state only)
  LIST_HEAD(struct tcp_sock) accept_queue;
  size_t accept_queue_len;
  size_t accept_queue_max;  // backlog

  // parent listener (for accepted connections)
  struct tcp_sock *parent;

  // accept queue link (separate from global socket list)
  LIST_ENTRY(struct tcp_sock) accept_link;

  // synchronization
  mtx_t lock;
  cond_t connect_cond;  // for connect() blocking
  cond_t accept_cond;   // for accept() blocking
  cond_t recv_cond;     // for recv() blocking
  cond_t send_cond;     // for send() blocking when buffer full
  bool closing;         // socket is being closed

  // kevent support
  struct knlist knlist; // associated knotes

  // linked list for socket tracking
  LIST_ENTRY(struct tcp_sock) link;

  // reference counting
  _refcount;
} tcp_sock_t;

// tcp_sock reference counting macros
#define tcp_sock_getref(sock) ({ \
  ASSERT_IS_TYPE(tcp_sock_t *, sock); \
  tcp_sock_t *__sock = (sock); \
  __sock ? ref_get(&__sock->refcount) : NULL; \
  __sock; \
})

#define tcp_sock_putref(sockref) ({ \
  ASSERT_IS_TYPE(tcp_sock_t **, sockref); \
  tcp_sock_t *__sock = *(sockref); \
  *(sockref) = NULL; \
  if (__sock) { \
    if (ref_put(&__sock->refcount)) { \
      _tcp_sock_cleanup(&__sock); \
    } \
  } \
})

//
// TCP Socket API
//

// TCP socket operations
tcp_sock_t *tcp_sock_alloc(void);
void _tcp_sock_cleanup(tcp_sock_t **tcp_skp);

// port management (internal)
// uint16_t tcp_get_port(void);
// int tcp_check_port(uint16_t port);

// socket lookup (internal)
// tcp_sock_t *tcp_lookup_sock(uint32_t saddr, uint16_t sport, uint32_t daddr, uint16_t dport);

// sequence number utilities
static inline bool tcp_seq_lt(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) < 0;
}

static inline bool tcp_seq_leq(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) <= 0;
}

static inline bool tcp_seq_gt(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) > 0;
}

static inline bool tcp_seq_geq(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) >= 0;
}

static inline bool tcp_seq_between(uint32_t seq, uint32_t start, uint32_t end) {
  return tcp_seq_geq(seq, start) && tcp_seq_lt(seq, end);
}

// checksum calculation
// uint16_t tcp_checksum(uint32_t saddr, uint32_t daddr, struct tcphdr *tcph, size_t len);

// initialization
void tcp_init(void);

#endif
