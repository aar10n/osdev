//
// Created by Aaron Gill-Braun on 2025-09-16.
//

#include <kernel/types.h>
#include <kernel/net/socket.h>
#include <kernel/net/netdev.h>
#include <kernel/net/ip.h>
#include <kernel/net/in_dev.h>
#include <kernel/mm.h>
#include <kernel/mutex.h>
#include <kernel/proc.h>
#include <kernel/errno.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/cond.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_addr.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG netlink
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("netlink: %s: " fmt, __func__, ##__VA_ARGS__)

#define NLMSG_DEFAULT_SIZE 1024


typedef struct nl_message {
  LIST_ENTRY(struct nl_message) link;
  size_t len;
  char data[];
} nl_message_t;

typedef struct nl_socket {
  uint32_t pid;          // port id (usually process pid)
  uint32_t groups;       // multicast groups
  int protocol;          // netlink protocol (NETLINK_ROUTE, etc.)

  // message queue for received messages
  LIST_HEAD(struct nl_message) msg_queue;
  mtx_t msg_lock;
  cond_t msg_cond;
  size_t msg_count;
} nl_socket_t;

//
// MARK: Socket Allocation and Cleanup
//

static nl_socket_t *nl_sock_alloc(int protocol) {
  nl_socket_t *nlsk = kmallocz(sizeof(nl_socket_t));
  if (!nlsk) {
    return NULL;
  }

  nlsk->protocol = protocol;
  nlsk->pid = 0;
  nlsk->groups = 0;
  nlsk->msg_count = 0;

  LIST_INIT(&nlsk->msg_queue);
  mtx_init(&nlsk->msg_lock, 0, "netlink_msg");
  cond_init(&nlsk->msg_cond, "netlink_cond");

  return nlsk;
}

static void nl_sock_cleanup(nl_socket_t *nlsk) {
  if (!nlsk) {
    return;
  }

  mtx_lock(&nlsk->msg_lock);
  LIST_FOR_IN_SAFE(msg, &nlsk->msg_queue, link) {
    LIST_REMOVE(&nlsk->msg_queue, msg, link);
    kfree(msg);
  }
  mtx_unlock(&nlsk->msg_lock);

  cond_destroy(&nlsk->msg_cond);
  mtx_destroy(&nlsk->msg_lock);
  kfree(nlsk);
}

static int netlink_route_process(nl_socket_t *nlsk, struct nlmsghdr *nlh, size_t len);

//
// MARK: Helper Functions
//

static inline int nla_put(struct sk_buff *skb, int attrtype, const void *data, size_t len) {
  ASSERT(skb != NULL);
  ASSERT(data != NULL);
  size_t total_len = RTA_SPACE(len);
  uint8_t *buf = skb_put_data(skb, total_len);
  struct rtattr *rta = (struct rtattr *)buf;

  memset(buf, 0, total_len);
  rta->rta_type = attrtype;
  rta->rta_len = RTA_LENGTH(len);
  memcpy(RTA_DATA(rta), data, len);
  return 0;
}

static inline int nla_put_u32(struct sk_buff *skb, int attrtype, uint32_t value) {
  ASSERT(skb != NULL);
  size_t total_len = RTA_SPACE(sizeof(uint32_t));
  uint8_t *buf = skb_put_data(skb, total_len);
  struct rtattr *rta = (struct rtattr *)buf;

  memset(buf, 0, total_len);
  rta->rta_type = attrtype;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  memcpy(RTA_DATA(rta), &value, sizeof(uint32_t));
  return 0;
}

static inline int nla_put_string(struct sk_buff *skb, int attrtype, const char *str) {
  ASSERT(skb != NULL);
  ASSERT(str != NULL);

  size_t len = strlen(str) + 1;
  size_t total_len = RTA_SPACE(len);
  uint8_t *buf = skb_put_data(skb, total_len);
  struct rtattr *rta = (struct rtattr *)buf;

  memset(buf, 0, total_len);
  rta->rta_type = attrtype;
  rta->rta_len = RTA_LENGTH(len);
  memcpy(RTA_DATA(rta), str, len);
  return 0;
}

static inline int nla_put_u8(struct sk_buff *skb, int attrtype, uint8_t value) {
  ASSERT(skb != NULL);

  size_t total_len = RTA_SPACE(sizeof(uint8_t));
  uint8_t *buf = skb_put_data(skb, total_len);
  struct rtattr *rta = (struct rtattr *)buf;

  memset(buf, 0, total_len);
  rta->rta_type = attrtype;
  rta->rta_len = RTA_LENGTH(sizeof(uint8_t));
  memcpy(RTA_DATA(rta), &value, sizeof(uint8_t));
  return 0;
}

static inline unsigned int dev_get_flags(netdev_t *dev) {
  ASSERT(dev != NULL);

  mtx_lock(&dev->lock);
  unsigned int flags = 0;
  if (dev->flags & NETDEV_UP)
    flags |= IFF_UP;
  if (dev->flags & NETDEV_RUNNING)
    flags |= IFF_RUNNING;
  if (dev->flags & NETDEV_LOOPBACK)
    flags |= IFF_LOOPBACK;
  mtx_unlock(&dev->lock);
  return flags;
}

//
// MARK: Netlink Protocol Operations
//

static int netlink_create(sock_t *sock, int protocol);
static int netlink_release(sock_t *sock);
static int netlink_bind(sock_t *sock, struct sockaddr *addr, int addrlen);
static int netlink_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags);
static int netlink_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags);

static int netlink_getsockname(sock_t *sock, struct sockaddr *addr, socklen_t *addr_len) {
  ASSERT(sock != NULL);
  ASSERT(addr != NULL);
  ASSERT(addr_len != NULL);

  nl_socket_t *nlsk = (nl_socket_t *)sock->sk;
  ASSERT(nlsk != NULL);

  if (*addr_len < sizeof(struct sockaddr_nl)) {
    return -EINVAL;
  }

  struct sockaddr_nl nladdr;
  memset(&nladdr, 0, sizeof(nladdr));
  nladdr.nl_family = AF_NETLINK;
  nladdr.nl_pid = nlsk->pid;
  nladdr.nl_groups = nlsk->groups;

  memcpy(addr, &nladdr, sizeof(struct sockaddr_nl));
  *addr_len = sizeof(struct sockaddr_nl);
  return 0;
}

static const struct proto_ops netlink_proto_ops = {
  .family = AF_NETLINK,
  .create = netlink_create,
  .release = netlink_release,
  .bind = netlink_bind,
  .sendmsg = netlink_sendmsg,
  .recvmsg = netlink_recvmsg,
  .getsockname = netlink_getsockname,
};

//
// MARK: Socket Operations
//

static int netlink_create(sock_t *sock, int protocol) {
  ASSERT(sock != NULL);
  if (protocol != NETLINK_ROUTE && protocol != NETLINK_GENERIC) {
    DPRINTF("unsupported protocol %d\n", protocol);
    return -EPROTONOSUPPORT;
  }

  if (sock->type != SOCK_DGRAM && sock->type != SOCK_RAW) {
    DPRINTF("unsupported socket type %d\n", sock->type);
    return -ESOCKTNOSUPPORT;
  }

  nl_socket_t *nlsk = nl_sock_alloc(protocol);
  if (!nlsk) {
    return -ENOMEM;
  }

  sock->sk = nlsk;
  sock->state = SS_UNCONNECTED;

  DPRINTF("created netlink socket protocol=%d\n", protocol);
  return 0;
}

static int netlink_release(sock_t *sock) {
  ASSERT(sock != NULL);

  nl_socket_t *nlsk = (nl_socket_t *)sock->sk;
  if (!nlsk) {
    return 0;
  }

  DPRINTF("releasing netlink socket pid=%u\n", nlsk->pid);

  nl_sock_cleanup(nlsk);
  sock->sk = NULL;
  return 0;
}

static int netlink_bind(sock_t *sock, struct sockaddr *addr, int addrlen) {
  ASSERT(sock != NULL);
  ASSERT(addr != NULL);

  if (addrlen < sizeof(struct sockaddr_nl)) {
    return -EINVAL;
  }

  struct sockaddr_nl *nl_addr = (struct sockaddr_nl *)addr;
  if (nl_addr->nl_family != AF_NETLINK) {
    return -EINVAL;
  }

  nl_socket_t *nlsk = (nl_socket_t *)sock->sk;
  ASSERT(nlsk != NULL);

  if (nl_addr->nl_pid == 0) {
    nlsk->pid = curproc->pid;
  } else {
    nlsk->pid = nl_addr->nl_pid;
  }

  nlsk->groups = nl_addr->nl_groups;

  DPRINTF("bound netlink socket pid=%u groups=0x%x\n", nlsk->pid, nlsk->groups);
  return 0;
}

static int netlink_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  ASSERT(sock != NULL);
  ASSERT(msg != NULL);

  nl_socket_t *nlsk = (nl_socket_t *)sock->sk;
  ASSERT(nlsk != NULL);

  char *buffer = kmalloc(len);
  if (!buffer) {
    return -ENOMEM;
  }

  size_t copied = 0;
  for (int i = 0; i < msg->msg_iovlen && copied < len; i++) {
    size_t to_copy = min(msg->msg_iov[i].iov_len, len - copied);
    memcpy(buffer + copied, msg->msg_iov[i].iov_base, to_copy);
    copied += to_copy;
  }

  // process netlink messages
  struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;
  int ret = 0;

  while (NLMSG_OK(nlh, copied)) {
    if (nlh->nlmsg_len < sizeof(struct nlmsghdr)) {
      ret = -EINVAL;
      break;
    }

    switch (nlsk->protocol) {
      case NETLINK_ROUTE:
        ret = netlink_route_process(nlsk, nlh, nlh->nlmsg_len);
        break;
      default:
        ret = -EOPNOTSUPP;
        break;
    }

    if (ret < 0) {
      break;
    }

    nlh = NLMSG_NEXT(nlh, copied);
  }

  kfree(buffer);
  return ret < 0 ? ret : (int)len;
}

static int netlink_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  ASSERT(sock != NULL);
  ASSERT(msg != NULL);

  nl_socket_t *nlsk = (nl_socket_t *)sock->sk;
  ASSERT(nlsk != NULL);

  if (msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_nl)) {
    struct sockaddr_nl *nladdr = (struct sockaddr_nl *)msg->msg_name;
    memset(nladdr, 0, sizeof(*nladdr));
    nladdr->nl_family = AF_NETLINK;
    nladdr->nl_pid = 0;
    nladdr->nl_groups = 0;
    msg->msg_namelen = sizeof(struct sockaddr_nl);
  }

  mtx_lock(&nlsk->msg_lock);

  while (LIST_EMPTY(&nlsk->msg_queue)) {
    if (flags & MSG_DONTWAIT) {
      mtx_unlock(&nlsk->msg_lock);
      return -EAGAIN;
    }

    cond_wait(&nlsk->msg_cond, &nlsk->msg_lock);
  }

  size_t total_copied = 0;
  size_t iov_index = 0;
  size_t iov_offset = 0;

  LIST_FOR_IN_SAFE(nl_msg, &nlsk->msg_queue, link) {
    if (nl_msg->len > (len - total_copied)) {
      break;
    }

    LIST_REMOVE(&nlsk->msg_queue, nl_msg, link);
    nlsk->msg_count--;

    size_t msg_copied = 0;
    while (msg_copied < nl_msg->len && iov_index < msg->msg_iovlen) {
      size_t iov_remaining = msg->msg_iov[iov_index].iov_len - iov_offset;
      size_t msg_remaining = nl_msg->len - msg_copied;
      size_t to_copy = min(iov_remaining, msg_remaining);

      memcpy((uint8_t *)msg->msg_iov[iov_index].iov_base + iov_offset,
             nl_msg->data + msg_copied, to_copy);

      msg_copied += to_copy;
      iov_offset += to_copy;

      if (iov_offset >= msg->msg_iov[iov_index].iov_len) {
        iov_index++;
        iov_offset = 0;
      }
    }

    total_copied += msg_copied;

    bool done = ((struct nlmsghdr *)nl_msg->data)->nlmsg_type == NLMSG_DONE;
    kfree(nl_msg);
    if (done) {
      break;
    }
  }

  mtx_unlock(&nlsk->msg_lock);
  return (int)total_copied;
}

//
// MARK: Message Queue Management
//

static int netlink_queue_message(nl_socket_t *nlsk, const void *data, size_t len) {
  ASSERT(nlsk != NULL);
  ASSERT(data != NULL);

  nl_message_t *msg = kmalloc(sizeof(nl_message_t) + len);
  if (!msg) {
    return -ENOMEM;
  }

  msg->len = len;
  memcpy(msg->data, data, len);

  mtx_lock(&nlsk->msg_lock);
  LIST_ADD(&nlsk->msg_queue, msg, link);
  nlsk->msg_count++;
  cond_signal(&nlsk->msg_cond);
  mtx_unlock(&nlsk->msg_lock);

  return 0;
}

//
// MARK: NETLINK_ROUTE Implementation
//

struct rtnetlink_dump_ctx {
  nl_socket_t *nlsk;
  struct nlmsghdr *req_nlh;
  int error;
};

static int rtnetlink_fill_ifinfo(struct sk_buff *skb, struct netdev *dev,
                                   uint32_t portid, uint32_t seq, int type, unsigned int flags) {
  ASSERT(skb != NULL);
  ASSERT(dev != NULL);

  struct ifinfomsg *ifm;
  struct nlmsghdr *nlh;
  uint8_t *start_pos = skb->tail;

  nlh = skb_put_data(skb, sizeof(struct nlmsghdr));
  nlh->nlmsg_type = type;
  nlh->nlmsg_len = 0;
  nlh->nlmsg_flags = flags;
  nlh->nlmsg_pid = portid;
  nlh->nlmsg_seq = seq;

  mtx_lock(&dev->lock);

  ifm = skb_put_data(skb, sizeof(struct ifinfomsg));
  ifm->ifi_family = AF_UNSPEC;
  ifm->ifi_type = dev->type;
  ifm->ifi_index = dev->ifindex;
  ifm->ifi_flags = dev_get_flags(dev);
  ifm->ifi_change = 0;

  nla_put_string(skb, IFLA_IFNAME, dev->name.str);
  nla_put_u32(skb, IFLA_MTU, dev->mtu);

  if (dev->addr_len > 0 && dev->addr_len <= sizeof(dev->dev_addr)) {
    nla_put(skb, IFLA_ADDRESS, dev->dev_addr, dev->addr_len);

    if (dev->type == ARPHRD_ETHER) {
      uint8_t bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      nla_put(skb, IFLA_BROADCAST, bcast_mac, 6);
    } else if (dev->flags & NETDEV_LOOPBACK) {
      uint8_t zero_mac[6] = {0};
      nla_put(skb, IFLA_BROADCAST, zero_mac, 6);
    }
  }

  nla_put_string(skb, IFLA_QDISC, "noop");
  nla_put_u32(skb, IFLA_TXQLEN, 1000);

  uint8_t operstate = (dev->flags & NETDEV_UP) ? IF_OPER_UP : IF_OPER_DOWN;
  nla_put_u8(skb, IFLA_OPERSTATE, operstate);

  mtx_unlock(&dev->lock);

  nlh->nlmsg_len = skb->tail - start_pos;

  size_t aligned_len = NLMSG_ALIGN(nlh->nlmsg_len);
  if (aligned_len > nlh->nlmsg_len) {
    size_t padding = aligned_len - nlh->nlmsg_len;
    uint8_t *pad_ptr = skb_put_data(skb, padding);
    memset(pad_ptr, 0, padding);
  }

  return (int)skb->len;
}

static int rtnetlink_send_device_info(netdev_t *dev, void *data) {
  ASSERT(dev != NULL);
  ASSERT(data != NULL);

  struct rtnetlink_dump_ctx *ctx = (struct rtnetlink_dump_ctx *)data;
  struct sk_buff *skb;

  skb = skb_alloc(NLMSG_DEFAULT_SIZE);
  if (!skb) {
    ctx->error = -ENOMEM;
    return -ENOMEM;
  }

  int ret = rtnetlink_fill_ifinfo(skb, dev, ctx->nlsk->pid, ctx->req_nlh->nlmsg_seq, RTM_NEWLINK, NLM_F_MULTI);
  if (ret < 0) {
    ctx->error = ret;
    skb_free(&skb);
    return ret;
  }

  ret = netlink_queue_message(ctx->nlsk, skb->data, skb->len);
  skb_free(&skb);

  if (ret < 0) {
    ctx->error = ret;
    return ret;
  }

  return 0;
}

static int rtnetlink_send_addr_info(nl_socket_t *nlsk, netdev_t *dev, in_ifaddr_t *ifa, uint32_t seq, uint16_t flags) {
  ASSERT(nlsk != NULL);
  ASSERT(dev != NULL);
  ASSERT(ifa != NULL);

  struct sk_buff *skb = skb_alloc(512);
  if (!skb) {
    return -ENOMEM;
  }

  // build the message header
  struct nlmsghdr *nlh = (struct nlmsghdr *)skb_put_data(skb, sizeof(struct nlmsghdr));
  nlh->nlmsg_type = RTM_NEWADDR;
  nlh->nlmsg_flags = flags;
  nlh->nlmsg_seq = seq;
  nlh->nlmsg_pid = nlsk->pid;

  // add the ifaddrmsg
  struct ifaddrmsg *ifm = (struct ifaddrmsg *)skb_put_data(skb, sizeof(struct ifaddrmsg));
  ifm->ifa_family = AF_INET;
  ifm->ifa_prefixlen = ifa->ifa_prefixlen;
  ifm->ifa_flags = ifa->ifa_flags;
  ifm->ifa_scope = ifa->ifa_scope;
  ifm->ifa_index = dev->ifindex;

  if (ifa->ifa_address) {
    nla_put_u32(skb, IFA_ADDRESS, htonl(ifa->ifa_address));
  }
  if (ifa->ifa_local) {
    nla_put_u32(skb, IFA_LOCAL, htonl(ifa->ifa_local));
  }
  if (ifa->ifa_broadcast && !(dev->flags & NETDEV_LOOPBACK)) {
    nla_put_u32(skb, IFA_BROADCAST, htonl(ifa->ifa_broadcast));
  }
  if (!str_isnull(ifa->ifa_label) && str_len(ifa->ifa_label) > 0) {
    nla_put_string(skb, IFA_LABEL, str_cptr(ifa->ifa_label));
  }

  nlh->nlmsg_len = skb->len;

  int ret = netlink_queue_message(nlsk, skb->data, skb->len);
  skb_free(&skb);
  return ret;
}

struct rtnetlink_addr_ctx {
  nl_socket_t *nlsk;
  uint32_t seq;
  int count;
  int error;
};

static int rtnetlink_addr_iter(netdev_t *dev, in_ifaddr_t *ifa, void *data) {
  ASSERT(dev != NULL);
  ASSERT(ifa != NULL);
  ASSERT(data != NULL);

  struct rtnetlink_addr_ctx *ctx = data;

  int ret = rtnetlink_send_addr_info(ctx->nlsk, dev, ifa, ctx->seq, NLM_F_MULTI);
  if (ret < 0) {
    ctx->error = ret;
    return ret;
  }

  ctx->count++;
  return 0;
}

static int rtnetlink_del_addr(nl_socket_t *nlsk, struct nlmsghdr *nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifaddrmsg))) {
    return -EINVAL;
  }

  struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
  netdev_t *dev = NULL;
  int ret = 0;

  if (ifa->ifa_index > 0) {
    dev = netdev_get_by_index((int)ifa->ifa_index);
    if (!dev) {
      ret = -ENODEV;
      goto send_ack;
    }
  }

  uint32_t ip_addr = 0;
  bool have_addr = false;

  struct rtattr *rta = IFA_RTA(ifa);
  int rtl = (int)(nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa)));

  for (; RTA_OK(rta, rtl); rta = RTA_NEXT(rta, rtl)) {
    switch (rta->rta_type) {
      case IFA_ADDRESS:
      case IFA_LOCAL:
        if (RTA_PAYLOAD(rta) == sizeof(uint32_t)) {
          memcpy(&ip_addr, RTA_DATA(rta), sizeof(uint32_t));
          have_addr = true;
        }
        break;
      default:
        break;
    }
  }

  if (!have_addr) {
    ret = -EINVAL;
    goto cleanup;
  }

  ret = inet_addr_del(dev, ip_addr, ifa->ifa_prefixlen);

cleanup:
  if (dev) {
    netdev_putref(&dev);
  }

send_ack: {
    struct {
      struct nlmsghdr nlh;
      struct nlmsgerr err;
    } ack = {
      .nlh = {
        .nlmsg_len = sizeof(ack),
        .nlmsg_type = NLMSG_ERROR,
        .nlmsg_flags = 0,
        .nlmsg_seq = nlh->nlmsg_seq,
        .nlmsg_pid = nlsk->pid,
      },
      .err = {
        .error = (ret < 0) ? ret : 0,
        .msg = *nlh,
      }
    };

    return netlink_queue_message(nlsk, &ack, sizeof(ack));
  }
}

static int rtnetlink_get_addr(nl_socket_t *nlsk, struct nlmsghdr *nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  struct rtnetlink_addr_ctx ctx = {
    .nlsk = nlsk,
    .seq = nlh->nlmsg_seq,
    .count = 0,
    .error = 0
  };

  inet_addr_iterate_all(rtnetlink_addr_iter, &ctx);

  if (ctx.error < 0) {
    return ctx.error;
  }

  struct nlmsghdr done_nlh = {
    .nlmsg_len = sizeof(struct nlmsghdr),
    .nlmsg_type = NLMSG_DONE,
    .nlmsg_flags = 0,
    .nlmsg_seq = nlh->nlmsg_seq,
    .nlmsg_pid = nlsk->pid,
  };

  return netlink_queue_message(nlsk, &done_nlh, sizeof(done_nlh));
}

struct rtnetlink_route_ctx {
  nl_socket_t *nlsk;
  uint32_t seq;
  int count;
  int error;
};

static int mask_to_prefix_len(uint32_t mask) {
  int len = 0;
  while (mask & 0x80000000) {
    len++;
    mask <<= 1;
  }
  return len;
}

static int rtnetlink_send_route_info(route_t *route, void *data) {
  ASSERT(route != NULL);
  ASSERT(data != NULL);

  struct rtnetlink_route_ctx *ctx = data;
  struct sk_buff *skb;

  skb = skb_alloc(512);
  if (!skb) {
    ctx->error = -ENOMEM;
    return -ENOMEM;
  }

  struct nlmsghdr *nlh = skb_put_data(skb, sizeof(struct nlmsghdr));
  nlh->nlmsg_type = RTM_NEWROUTE;
  nlh->nlmsg_flags = NLM_F_MULTI;
  nlh->nlmsg_seq = ctx->seq;
  nlh->nlmsg_pid = ctx->nlsk->pid;

  struct rtmsg *rtm = skb_put_data(skb, sizeof(struct rtmsg));
  memset(rtm, 0, sizeof(*rtm));
  rtm->rtm_family = AF_INET;
  rtm->rtm_dst_len = mask_to_prefix_len(route->mask);
  rtm->rtm_src_len = 0;
  rtm->rtm_tos = 0;
  rtm->rtm_table = RT_TABLE_MAIN;
  rtm->rtm_protocol = RTPROT_STATIC;
  rtm->rtm_scope = RT_SCOPE_UNIVERSE;
  rtm->rtm_type = RTN_UNICAST;
  rtm->rtm_flags = 0;

  if (route->dest != 0) {
    nla_put_u32(skb, RTA_DST, htonl(route->dest));
  }

  if (route->gateway != 0) {
    nla_put_u32(skb, RTA_GATEWAY, htonl(route->gateway));
  }

  if (route->dev) {
    nla_put_u32(skb, RTA_OIF, route->dev->ifindex);
  }

  if (route->metric > 0) {
    nla_put_u32(skb, RTA_PRIORITY, route->metric);
  }

  nlh->nlmsg_len = skb->len;

  int ret = netlink_queue_message(ctx->nlsk, skb->data, skb->len);
  skb_free(&skb);

  if (ret < 0) {
    ctx->error = ret;
    return ret;
  }

  ctx->count++;
  return 0;
}

static int rtnetlink_get_route(nl_socket_t *nlsk, struct nlmsghdr *nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  struct rtnetlink_route_ctx ctx = {
    .nlsk = nlsk,
    .seq = nlh->nlmsg_seq,
    .count = 0,
    .error = 0
  };

  int ret = ip_route_iterate(rtnetlink_send_route_info, &ctx);
  if (ret < 0) {
    return ret;
  }

  struct nlmsghdr done_nlh = {
    .nlmsg_len = sizeof(struct nlmsghdr),
    .nlmsg_type = NLMSG_DONE,
    .nlmsg_flags = 0,
    .nlmsg_seq = nlh->nlmsg_seq,
    .nlmsg_pid = nlsk->pid,
  };

  return netlink_queue_message(nlsk, &done_nlh, sizeof(done_nlh));
}

static int rtnetlink_del_route(nl_socket_t *nlsk, struct nlmsghdr *nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtmsg))) {
    return -EINVAL;
  }

  struct rtmsg *rtm = NLMSG_DATA(nlh);
  struct rtattr *rta = RTM_RTA(rtm);
  int remaining = RTM_PAYLOAD(nlh);

  uint32_t dest = 0;
  uint32_t mask = 0;

  if (rtm->rtm_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  if (rtm->rtm_dst_len > 0) {
    mask = htonl(~((1U << (32 - rtm->rtm_dst_len)) - 1));
  } else {
    mask = 0;
  }

  while (RTA_OK(rta, remaining)) {
    switch (rta->rta_type) {
      case RTA_DST:
        if (RTA_PAYLOAD(rta) == sizeof(uint32_t)) {
          uint32_t tmp;
          memcpy(&tmp, RTA_DATA(rta), sizeof(uint32_t));
          dest = ntohl(tmp);
        }
        break;
      default:
        break;
    }
    rta = RTA_NEXT(rta, remaining);
  }

  int ret = ip_route_del(dest, mask);

  struct {
    struct nlmsghdr nlh;
    struct nlmsgerr err;
  } ack = {
    .nlh = {
      .nlmsg_len = sizeof(ack),
      .nlmsg_type = NLMSG_ERROR,
      .nlmsg_flags = 0,
      .nlmsg_seq = nlh->nlmsg_seq,
      .nlmsg_pid = nlsk->pid,
    },
    .err = {
      .error = (ret < 0) ? ret : 0,
      .msg = *nlh,
    }
  };

  return netlink_queue_message(nlsk, &ack, sizeof(ack));
}

static int rtnetlink_add_route(nl_socket_t *nlsk, struct nlmsghdr *nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtmsg))) {
    return -EINVAL;
  }

  struct rtmsg *rtm = NLMSG_DATA(nlh);
  struct rtattr *rta = RTM_RTA(rtm);
  int remaining = RTM_PAYLOAD(nlh);

  // route parameters
  uint32_t dest = 0;
  uint32_t gateway = 0;
  uint32_t mask = 0;
  int oif_index = -1;
  int metric = 0;

  if (rtm->rtm_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  if (rtm->rtm_dst_len > 0) {
    mask = htonl(~((1U << (32 - rtm->rtm_dst_len)) - 1));
  } else {
    mask = 0;
  }

  while (RTA_OK(rta, remaining)) {
    switch (rta->rta_type) {
      case RTA_DST:
        if (RTA_PAYLOAD(rta) == sizeof(uint32_t)) {
          uint32_t tmp;
          memcpy(&tmp, RTA_DATA(rta), sizeof(uint32_t));
          dest = ntohl(tmp);
        }
        break;
      case RTA_GATEWAY:
        if (RTA_PAYLOAD(rta) == sizeof(uint32_t)) {
          uint32_t tmp;
          memcpy(&tmp, RTA_DATA(rta), sizeof(uint32_t));
          gateway = ntohl(tmp);
        }
        break;
      case RTA_OIF:
        if (RTA_PAYLOAD(rta) == sizeof(int)) {
          memcpy(&oif_index, RTA_DATA(rta), sizeof(int));
        }
        break;
      case RTA_PRIORITY:
        if (RTA_PAYLOAD(rta) == sizeof(int)) {
          memcpy(&metric, RTA_DATA(rta), sizeof(int));
        }
        break;
      default:
        break;
    }
    rta = RTA_NEXT(rta, remaining);
  }

  netdev_t *dev = NULL;
  if (oif_index > 0) {
    dev = netdev_get_by_index(oif_index);
    if (!dev) {
      return -ENODEV;
    }
  } else {
    return -EINVAL;
  }

  int ret = ip_route_add(dest, mask, gateway, dev, metric);
  netdev_putref(&dev);

  struct {
    struct nlmsghdr nlh;
    struct nlmsgerr err;
  } ack = {
    .nlh = {
      .nlmsg_len = sizeof(ack),
      .nlmsg_type = NLMSG_ERROR,
      .nlmsg_flags = 0,
      .nlmsg_seq = nlh->nlmsg_seq,
      .nlmsg_pid = nlsk->pid,
    },
    .err = {
      .error = (ret < 0 && ret != -EEXIST) ? ret : 0,
      .msg = *nlh,
    }
  };

  return netlink_queue_message(nlsk, &ack, sizeof(ack));
}

static int rtnetlink_add_addr(nl_socket_t *nlsk, struct nlmsghdr *nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
  struct rtattr *rta = IFA_RTA(ifa);
  int remaining = IFA_PAYLOAD(nlh);
  int ret = 0;

  uint32_t addr = 0;
  uint32_t local_addr = 0;
  char *label = NULL;
  int has_local = 0;

  while (RTA_OK(rta, remaining)) {
    switch (rta->rta_type) {
      case IFA_ADDRESS:
        if (ifa->ifa_family == AF_INET && rta->rta_len >= sizeof(uint32_t)) {
          uint32_t tmp;
          memcpy(&tmp, RTA_DATA(rta), sizeof(uint32_t));
          addr = ntohl(tmp);
        }
        break;
      case IFA_LOCAL:
        if (ifa->ifa_family == AF_INET && rta->rta_len >= sizeof(uint32_t)) {
          uint32_t tmp;
          memcpy(&tmp, RTA_DATA(rta), sizeof(uint32_t));
          local_addr = ntohl(tmp);
          has_local = 1;
        }
        break;
      case IFA_LABEL:
        label = RTA_DATA(rta);
        break;
      case IFA_BROADCAST:
      default:
        break;
    }
    rta = RTA_NEXT(rta, remaining);
  }

  netdev_t *dev = netdev_get_by_index((int)ifa->ifa_index);
  if (!dev) {
    ret = -ENODEV;
    goto send_ack;
  }

  uint32_t ip_addr = has_local ? local_addr : addr;
  uint32_t broadcast = 0;

  rta = IFA_RTA(ifa);
  remaining = IFA_PAYLOAD(nlh);
  while (RTA_OK(rta, remaining)) {
    if (rta->rta_type == IFA_BROADCAST && ifa->ifa_family == AF_INET && rta->rta_len >= sizeof(uint32_t)) {
      uint32_t tmp;
      memcpy(&tmp, RTA_DATA(rta), sizeof(uint32_t));
      broadcast = ntohl(tmp);
      break;
    }
    rta = RTA_NEXT(rta, remaining);
  }

  if (ip_addr != 0) {
    ret = inet_addr_add(dev, ip_addr, ifa->ifa_prefixlen, broadcast, ifa->ifa_scope, cstr_make(label));
  }

  netdev_putref(&dev);

send_ack:
  {
    struct {
      struct nlmsghdr nlh;
      struct nlmsgerr err;
    } ack = {
      .nlh = {
        .nlmsg_len = sizeof(ack),
        .nlmsg_type = NLMSG_ERROR,
        .nlmsg_flags = 0,
        .nlmsg_seq = nlh->nlmsg_seq,
        .nlmsg_pid = nlsk->pid,
      },
      .err = {
        .error = (ret < 0 && ret != -EEXIST) ? ret : 0,
        .msg = *nlh,
      }
    };

    return netlink_queue_message(nlsk, &ack, sizeof(ack));
  }
}

static int rtnetlink_send_link_info(nl_socket_t *nlsk, struct nlmsghdr *req_nlh) {
  ASSERT(nlsk != NULL);
  ASSERT(req_nlh != NULL);

  struct rtnetlink_dump_ctx ctx = {
    .nlsk = nlsk,
    .req_nlh = req_nlh,
    .error = 0
  };

  int device_count = netdev_iterate_all(rtnetlink_send_device_info, &ctx);

  if (ctx.error < 0) {
    return ctx.error;
  }

  if (device_count < 0) {
    return device_count;
  }

  struct nlmsghdr done_nlh = {
    .nlmsg_len = sizeof(struct nlmsghdr),
    .nlmsg_type = NLMSG_DONE,
    .nlmsg_flags = 0,
    .nlmsg_seq = req_nlh->nlmsg_seq,
    .nlmsg_pid = nlsk->pid,
  };

  return netlink_queue_message(nlsk, &done_nlh, sizeof(done_nlh));
}

static int rtnetlink_change_link(struct nlmsghdr *nlh) {
  ASSERT(nlh != NULL);

  struct ifinfomsg *ifi = NLMSG_DATA(nlh);
  if (ifi->ifi_index == 0) {
    return -ENODEV;
  }

  netdev_t *dev = netdev_get_by_index(ifi->ifi_index);
  if (!dev) {
    return -ENODEV;
  }

  int ret = 0;

  mtx_lock(&dev->lock);
  if ((ifi->ifi_flags & IFF_UP) && !(dev->flags & NETDEV_UP)) {
    mtx_unlock(&dev->lock);
    ret = netdev_open(dev);
  } else if (!(ifi->ifi_flags & IFF_UP) && (dev->flags & NETDEV_UP)) {
    mtx_unlock(&dev->lock);
    ret = netdev_close(dev);
  } else {
    mtx_unlock(&dev->lock);
  }

  netdev_putref(&dev);
  return ret;
}

static int netlink_route_process(nl_socket_t *nlsk, struct nlmsghdr *nlh, size_t len) {
  ASSERT(nlsk != NULL);
  ASSERT(nlh != NULL);

  switch (nlh->nlmsg_type) {
    case RTM_GETLINK:
      if (!(nlh->nlmsg_flags & NLM_F_DUMP)) {
        return -EOPNOTSUPP;
      }
      return rtnetlink_send_link_info(nlsk, nlh);
    case RTM_NEWLINK:
      return rtnetlink_change_link(nlh);
    case RTM_DELLINK:
      todo("implement RTM_DELLINK");
      return -EOPNOTSUPP;
    case RTM_SETLINK:
      todo("implement RTM_SETLINK");
      return -EOPNOTSUPP;

    case RTM_NEWADDR:
      return rtnetlink_add_addr(nlsk, nlh);
    case RTM_DELADDR:
      return rtnetlink_del_addr(nlsk, nlh);
    case RTM_GETADDR:
      return rtnetlink_get_addr(nlsk, nlh);

    case RTM_NEWROUTE:
      return rtnetlink_add_route(nlsk, nlh);
    case RTM_DELROUTE:
      return rtnetlink_del_route(nlsk, nlh);
    case RTM_GETROUTE:
      return rtnetlink_get_route(nlsk, nlh);

    case RTM_NEWNEIGH:
      todo("implement RTM_NEWNEIGH");
      return -EOPNOTSUPP;
    case RTM_DELNEIGH:
      todo("implement RTM_DELNEIGH");
      return -EOPNOTSUPP;
    case RTM_GETNEIGH:
      todo("implement RTM_GETNEIGH");
      return -EOPNOTSUPP;

    case RTM_NEWRULE:
      todo("implement RTM_NEWRULE");
      return -EOPNOTSUPP;
    case RTM_DELRULE:
      todo("implement RTM_DELRULE");
      return -EOPNOTSUPP;
    case RTM_GETRULE:
      todo("implement RTM_GETRULE");
      return -EOPNOTSUPP;

    case RTM_NEWQDISC:
      todo("implement RTM_NEWQDISC");
      return -EOPNOTSUPP;
    case RTM_DELQDISC:
      todo("implement RTM_DELQDISC");
      return -EOPNOTSUPP;
    case RTM_GETQDISC:
      todo("implement RTM_GETQDISC");
      return -EOPNOTSUPP;

    case RTM_NEWTCLASS:
      todo("implement RTM_NEWTCLASS");
      return -EOPNOTSUPP;
    case RTM_DELTCLASS:
      todo("implement RTM_DELTCLASS");
      return -EOPNOTSUPP;
    case RTM_GETTCLASS:
      todo("implement RTM_GETTCLASS");
      return -EOPNOTSUPP;

    case RTM_NEWTFILTER:
      todo("implement RTM_NEWTFILTER");
      return -EOPNOTSUPP;
    case RTM_DELTFILTER:
      todo("implement RTM_DELTFILTER");
      return -EOPNOTSUPP;
    case RTM_GETTFILTER:
      todo("implement RTM_GETTFILTER");
      return -EOPNOTSUPP;

    default: {
      struct nlmsgerr err = {
        .error = -EOPNOTSUPP,
        .msg = *nlh
      };

      struct nlmsghdr err_nlh = {
        .nlmsg_len = NLMSG_LENGTH(sizeof(err)),
        .nlmsg_type = NLMSG_ERROR,
        .nlmsg_flags = 0,
        .nlmsg_seq = nlh->nlmsg_seq,
        .nlmsg_pid = nlsk->pid,
      };

      char err_buf[NLMSG_SPACE(sizeof(err))];
      memcpy(err_buf, &err_nlh, sizeof(err_nlh));
      memcpy(err_buf + sizeof(err_nlh), &err, sizeof(err));

      return netlink_queue_message(nlsk, err_buf, err_nlh.nlmsg_len);
    }
  }
}

//
// MARK: Protocol Registration
//

void netlink_init() {
  int ret = proto_register(&netlink_proto_ops);
  if (ret < 0) {
    kprintf("netlink: failed to register protocol family: %d\n", ret);
    return;
  }

  DPRINTF("netlink protocol family registered\n");
}
MODULE_INIT(netlink_init);
