//
// Created by Aaron Gill-Braun on 2025-10-20.
//

#include <kernel/net/inet.h>
#include <kernel/net/socket.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <linux/in.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG inet
#include <kernel/log.h>
#define EPRINTF(fmt, ...) kprintf("inet: %s: " fmt, __func__, ##__VA_ARGS__)

typedef struct inet_protocol {
  int type;                       // socket type (SOCK_STREAM, SOCK_DGRAM, etc.)
  int protocol;                   // IP protocol number (IPPROTO_TCP, etc.)
  const struct proto_ops *ops;    // protocol operations
  LIST_ENTRY(struct inet_protocol) link;
} inet_protocol_t;

static LIST_HEAD(inet_protocol_t) protocols;
static mtx_t protocols_lock;

void inet_static_init() {
  mtx_init(&protocols_lock, 0, "inet_protocols");
}
STATIC_INIT(inet_static_init);

//
// MARK: Protocol Registration
//

int inet_register_protocol(int type, int protocol, const struct proto_ops *ops) {
  if (!ops || !ops->create) {
    return -EINVAL;
  }

  inet_protocol_t *proto = kmalloc(sizeof(inet_protocol_t));
  if (!proto) {
    return -ENOMEM;
  }

  proto->type = type;
  proto->protocol = protocol;
  proto->ops = ops;

  mtx_lock(&protocols_lock);
  LIST_ADD(&protocols, proto, link);
  mtx_unlock(&protocols_lock);

  DPRINTF("registered protocol: type=%d, proto=%d\n", type, protocol);
  return 0;
}

void inet_unregister_protocol(int type, int protocol) {
  mtx_lock(&protocols_lock);

  inet_protocol_t *proto = LIST_FIND(_proto, &protocols, link,
    _proto->type == type && _proto->protocol == protocol);

  if (proto) {
    LIST_REMOVE(&protocols, proto, link);
    kfree(proto);
  }

  mtx_unlock(&protocols_lock);
  DPRINTF("unregistered protocol: type=%d, proto=%d\n", type, protocol);
}

static const struct proto_ops *inet_lookup_protocol(int type, int protocol) {
  const struct proto_ops *ops = NULL;

  mtx_lock(&protocols_lock);

  inet_protocol_t *proto;
  LIST_FOREACH(proto, &protocols, link) {
    if (proto->type == type && (proto->protocol == protocol || proto->protocol == 0)) {
      ops = proto->ops;
      break;
    }
  }

  mtx_unlock(&protocols_lock);
  return ops;
}

//
// MARK: Socket Operations
//

static int inet_create(sock_t *sock, int protocol) {
  const struct proto_ops *ops = inet_lookup_protocol(sock->type, protocol);
  if (!ops) {
    return -EPROTONOSUPPORT;
  }

  sock->ops = ops;
  return ops->create(sock, protocol);
}

static int inet_release(sock_t *sock) {
  if (!sock->sk || !sock->ops || !sock->ops->release) {
    return 0;
  }
  return sock->ops->release(sock);
}

static int inet_bind(sock_t *sock, struct sockaddr *addr, int addrlen) {
  if (!sock->sk || addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  if (sin->sin_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  if (!sock->ops || !sock->ops->bind) {
    return -EOPNOTSUPP;
  }

  return sock->ops->bind(sock, addr, addrlen);
}

static int inet_connect(sock_t *sock, struct sockaddr *addr, int addrlen, int flags) {
  if (!sock->sk || addrlen < sizeof(struct sockaddr_in)) {
    return -EINVAL;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  if (sin->sin_family != AF_INET) {
    return -EAFNOSUPPORT;
  }

  if (!sock->ops || !sock->ops->connect) {
    return -EOPNOTSUPP;
  }

  return sock->ops->connect(sock, addr, addrlen, flags);
}

static int inet_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  if (!sock->ops || !sock->ops->sendmsg) {
    return -EOPNOTSUPP;
  }
  return sock->ops->sendmsg(sock, msg, len, flags);
}

static int inet_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  if (!sock->ops || !sock->ops->recvmsg) {
    return -EOPNOTSUPP;
  }
  return sock->ops->recvmsg(sock, msg, len, flags);
}

static int inet_listen(sock_t *sock, int backlog) {
  if (!sock->ops || !sock->ops->listen) {
    return -EOPNOTSUPP;
  }
  return sock->ops->listen(sock, backlog);
}

static int inet_accept(sock_t *sock, sock_t *newsock, int flags) {
  if (!sock->ops || !sock->ops->accept) {
    return -EOPNOTSUPP;
  }
  return sock->ops->accept(sock, newsock, flags);
}

static int inet_shutdown(sock_t *sock, int how) {
  if (!sock->ops || !sock->ops->shutdown) {
    return -EOPNOTSUPP;
  }
  return sock->ops->shutdown(sock, how);
}

const struct proto_ops inet_ops = {
  .family = AF_INET,
  .create = inet_create,
  .release = inet_release,
  .bind = inet_bind,
  .connect = inet_connect,
  .listen = inet_listen,
  .accept = inet_accept,
  .sendmsg = inet_sendmsg,
  .recvmsg = inet_recvmsg,
  .shutdown = inet_shutdown,
};

//
// MARK: Inet Protocol Registration
//

void inet_init() {
  proto_register(&inet_ops);
  DPRINTF("IPv4 protocol family initialized\n");
}
MODULE_INIT(inet_init);
