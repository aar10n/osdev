//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#include <kernel/net/socket.h>

#include <kernel/mm.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>
#include <kernel/queue.h>
#include <kernel/kevent.h>
#include <kernel/string.h>
#include <kernel/printf.h>

#include <abi/fcntl.h>
#include <sys/un.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("unix: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("unix: %s: " fmt, __func__, ##__VA_ARGS__)

#define UNIX_BUFFER_SIZE (64 * 1024UL)  // 64KB for stream sockets
#define UNIX_MAX_DGRAM_SIZE (16 * 1024UL)  // 16KB max datagram size
#define UNIX_BACKLOG_MAX 128

typedef struct unix_dgram_msg {
  struct sockaddr_un addr;
  socklen_t addrlen;
  size_t len;
  LIST_ENTRY(struct unix_dgram_msg) link;
  uint8_t data[];
} unix_dgram_msg_t;

typedef struct unix_dgram {
  LIST_HEAD(unix_dgram_msg_t) rx_queue;
  size_t rx_queue_len;
  size_t rx_queue_bytes;
} unix_dgram_t;

// stream buffer structure
typedef struct unix_stream {
  void *buffer;
  size_t buffer_size;
  size_t read_pos;
  size_t write_pos;
  size_t count;

  struct unix_socket *peer;
  int shutdown_flags;  // SHUT_RD, SHUT_WR

  // for listening sockets
  LIST_HEAD(struct unix_socket) accept_queue;
  size_t accept_queue_len;
  int backlog;
} unix_stream_t;

typedef struct unix_socket {
  int type;  // SOCK_DGRAM or SOCK_STREAM
  struct sockaddr_un addr;
  socklen_t addrlen;
  bool bound;

  union {
    unix_dgram_t dgram;
    unix_stream_t stream;
  };

  // socket options
  int rcvbuf;
  int sndbuf;

  mtx_t lock;
  cond_t rx_cond;
  cond_t tx_cond;
  struct knlist knlist;

  _refcount;

  LIST_ENTRY(struct unix_socket) link;     // global unix_sockets list
  LIST_ENTRY(struct unix_socket) aq_link;  // accept queue entry
} unix_socket_t;

static LIST_HEAD(unix_socket_t) unix_sockets;
static mtx_t unix_sockets_lock;

#define unix_sock_getref(sock) ({ \
  ASSERT_IS_TYPE(unix_socket_t *, sock); \
  unix_socket_t *__sock = (sock); \
  __sock ? ref_get(&__sock->refcount) : NULL; \
  __sock; \
})

#define unix_sock_putref(sockref) ({ \
  ASSERT_IS_TYPE(unix_socket_t **, sockref); \
  unix_socket_t *__sock = *(sockref); \
  *(sockref) = NULL; \
  if (__sock) { \
    if (ref_put(&__sock->refcount)) { \
      _unix_sock_cleanup(&__sock); \
    } \
  } \
})

//
// MARK: Data Copying
//

static size_t copy_from_iovec(const struct iovec *iov, size_t iovcnt, void *dst, size_t len) {
  size_t offset = 0;
  for (size_t i = 0; i < iovcnt && offset < len; i++) {
    size_t to_copy = min(iov[i].iov_len, len - offset);
    memcpy((uint8_t *)dst + offset, iov[i].iov_base, to_copy);
    offset += to_copy;
  }
  return offset;
}

static size_t copy_to_iovec(const struct iovec *iov, size_t iovcnt, const void *src, size_t len) {
  size_t offset = 0;
  for (size_t i = 0; i < iovcnt && offset < len; i++) {
    size_t to_copy = min(iov[i].iov_len, len - offset);
    memcpy(iov[i].iov_base, (const uint8_t *)src + offset, to_copy);
    offset += to_copy;
  }
  return offset;
}

static size_t stream_buffer_write(unix_stream_t *stream, const void *data, size_t len) {
  size_t write_pos = stream->write_pos;
  size_t buffer_size = stream->buffer_size;
  const uint8_t *src = (const uint8_t *)data;

  if (write_pos + len > buffer_size) {
    size_t first_part = buffer_size - write_pos;
    size_t second_part = len - first_part;

    memcpy((uint8_t *)stream->buffer + write_pos, src, first_part);
    memcpy(stream->buffer, src + first_part, second_part);

    stream->write_pos = second_part;
  } else {
    memcpy((uint8_t *)stream->buffer + write_pos, src, len);
    stream->write_pos = write_pos + len;

    if (stream->write_pos == buffer_size) {
      stream->write_pos = 0;
    }
  }

  stream->count += len;
  return len;
}

static size_t stream_buffer_read(unix_stream_t *stream, void *data, size_t len) {
  size_t read_pos = stream->read_pos;
  size_t buffer_size = stream->buffer_size;
  uint8_t *dst = (uint8_t *)data;

  if (read_pos + len > buffer_size) {
    size_t first_part = buffer_size - read_pos;
    size_t second_part = len - first_part;

    memcpy(dst, (uint8_t *)stream->buffer + read_pos, first_part);
    memcpy(dst + first_part, stream->buffer, second_part);

    stream->read_pos = second_part;
  } else {
    memcpy(dst, (uint8_t *)stream->buffer + read_pos, len);
    stream->read_pos = read_pos + len;

    if (stream->read_pos == buffer_size) {
      stream->read_pos = 0;
    }
  }

  stream->count -= len;
  return len;
}

static size_t stream_buffer_write_iovec(unix_stream_t *stream, const struct iovec *iov, size_t iovcnt, size_t total_len) {
  size_t written = 0;
  for (size_t i = 0; i < iovcnt && written < total_len; i++) {
    size_t to_write = min(iov[i].iov_len, total_len - written);
    stream_buffer_write(stream, iov[i].iov_base, to_write);
    written += to_write;
  }
  return written;
}

static size_t stream_buffer_read_iovec(unix_stream_t *stream, const struct iovec *iov, size_t iovcnt, size_t total_len) {
  size_t read = 0;
  for (size_t i = 0; i < iovcnt && read < total_len; i++) {
    size_t to_read = min(iov[i].iov_len, total_len - read);
    stream_buffer_read(stream, iov[i].iov_base, to_read);
    read += to_read;
  }
  return read;
}

//
// MARK: Socket Management
//

static void _unix_sock_cleanup(unix_socket_t **usockp) {
  unix_socket_t *usock = moveref(*usockp);
  if (!usock) {
    return;
  }

  ASSERT(read_refcount(usock) == 0);

  if (usock->type == SOCK_DGRAM) {
    // free all pending datagrams
    LIST_FOR_IN_SAFE(msg, &usock->dgram.rx_queue, link) {
      LIST_REMOVE(&usock->dgram.rx_queue, msg, link);
      kfree(msg);
    }
  } else if (usock->type == SOCK_STREAM) {
    if (usock->stream.buffer) {
      vmap_free((uintptr_t)usock->stream.buffer, usock->stream.buffer_size);
    }

    if (usock->stream.peer) {
      unix_sock_putref(&usock->stream.peer);
    }

    // free pending accept queue
    LIST_FOR_IN_SAFE(pending, &usock->stream.accept_queue, aq_link) {
      LIST_REMOVE(&usock->stream.accept_queue, pending, aq_link);
      unix_sock_putref(&pending);
    }
  }

  knlist_destroy(&usock->knlist);
  mtx_destroy(&usock->lock);
  cond_destroy(&usock->rx_cond);
  cond_destroy(&usock->tx_cond);
  kfree(usock);
}

static unix_socket_t *unix_find_bound_socket(const struct sockaddr_un *addr, socklen_t addrlen) {
  size_t path_offset = offsetof(struct sockaddr_un, sun_path);
  bool abstract = addrlen > path_offset && addr->sun_path[0] == '\0';
  size_t name_len = addrlen - path_offset;

  mtx_lock(&unix_sockets_lock);
  LIST_FOR_IN(usock, &unix_sockets, link) {
    if (!usock->bound)
      continue;

    if (abstract) {
      // abstract sockets: compare by exact abstract name bytes
      size_t bound_len = usock->addrlen - path_offset;
      if (bound_len == name_len && memcmp(usock->addr.sun_path, addr->sun_path, name_len) == 0) {
        usock = unix_sock_getref(usock);
        mtx_unlock(&unix_sockets_lock);
        return usock;
      }
    } else {
      // pathname sockets: compare by string
      if (usock->addr.sun_path[0] != '\0' && strcmp(usock->addr.sun_path, addr->sun_path) == 0) {
        usock = unix_sock_getref(usock);
        mtx_unlock(&unix_sockets_lock);
        return usock;
      }
    }
  }

  mtx_unlock(&unix_sockets_lock);
  return NULL;
}

//
// MARK: Protocol Operations
//

static int unix_create(sock_t *sock, int protocol) {
  if (protocol != 0) {
    return -EPROTONOSUPPORT;
  }

  if (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM) {
    return -ESOCKTNOSUPPORT;
  }

  unix_socket_t *usock = kmallocz(sizeof(unix_socket_t));
  if (!usock) {
    return -ENOMEM;
  }

  usock->type = sock->type;
  usock->rcvbuf = 64 * 1024;
  usock->sndbuf = 64 * 1024;

  if (sock->type == SOCK_STREAM) {
    // allocate stream buffer
    uintptr_t buffer = vmap_anon(UNIX_BUFFER_SIZE, 0, UNIX_BUFFER_SIZE, VM_RDWR, "unix_stream");
    if (!buffer) {
      kfree(usock);
      return -ENOMEM;
    }
    usock->stream.buffer = (void *)buffer;
    usock->stream.buffer_size = UNIX_BUFFER_SIZE;
  }

  initref(usock);
  mtx_init(&usock->lock, 0, "unix_lock");
  cond_init(&usock->rx_cond, "unix_rx");
  cond_init(&usock->tx_cond, "unix_tx");
  knlist_init(&usock->knlist, &usock->lock.lo);

  sock->sk = usock;
  sock->knlist = &usock->knlist;

  mtx_lock(&unix_sockets_lock);
  LIST_ADD(&unix_sockets, usock, link);
  mtx_unlock(&unix_sockets_lock);

  DPRINTF("created UNIX socket type=%d\n", sock->type);
  return 0;
}

static int unix_release(sock_t *sock) {
  unix_socket_t *usock = moveref(sock->sk);
  ASSERT(usock != NULL);

  DPRINTF("releasing UNIX socket\n");

  mtx_lock(&unix_sockets_lock);
  LIST_REMOVE(&unix_sockets, usock, link);
  mtx_unlock(&unix_sockets_lock);

  mtx_lock(&usock->lock);
  cond_broadcast(&usock->rx_cond);
  cond_broadcast(&usock->tx_cond);

  // notify peer if connected
  if (usock->type == SOCK_STREAM && usock->stream.peer) {
    unix_socket_t *peer = usock->stream.peer;
    mtx_lock(&peer->lock);
    peer->stream.shutdown_flags |= SHUT_WR;
    cond_broadcast(&peer->rx_cond);
    knlist_activate_notes(&peer->knlist, 0);
    mtx_unlock(&peer->lock);
  }
  mtx_unlock(&usock->lock);

  unix_sock_putref(&usock);
  return 0;
}

static int unix_bind(sock_t *sock, struct sockaddr *addr, int addrlen) {
  unix_socket_t *usock = sock->sk;
  ASSERT(usock != NULL);

  if (addrlen < sizeof(sa_family_t) || addrlen > sizeof(struct sockaddr_un)) {
    return -EINVAL;
  }

  const struct sockaddr_un *sun = (const struct sockaddr_un *)addr;
  if (sun->sun_family != AF_UNIX) {
    return -EAFNOSUPPORT;
  }

  mtx_lock(&usock->lock);
  if (usock->bound) {
    mtx_unlock(&usock->lock);
    return -EINVAL;
  }

  unix_socket_t *existing = unix_find_bound_socket(sun, addrlen);
  if (existing) {
    unix_sock_putref(&existing);
    mtx_unlock(&usock->lock);
    return -EADDRINUSE;
  }

  memcpy(&usock->addr, sun, addrlen);
  usock->addrlen = addrlen;
  usock->bound = true;
  mtx_unlock(&usock->lock);

  DPRINTF("bound to path: %s\n", sun->sun_path[0] ? sun->sun_path : "(abstract)");
  return 0;
}

static int unix_connect(sock_t *sock, struct sockaddr *addr, int addrlen, int flags) {
  unix_socket_t *usock = sock->sk;
  ASSERT(usock != NULL);

  if (addrlen < sizeof(sa_family_t) || addrlen > sizeof(struct sockaddr_un)) {
    return -EINVAL;
  }

  const struct sockaddr_un *sun = (const struct sockaddr_un *)addr;
  if (sun->sun_family != AF_UNIX) {
    return -EAFNOSUPPORT;
  }

  if (usock->type == SOCK_DGRAM) {
    // for DGRAM, connect just sets the default destination
    mtx_lock(&usock->lock);
    memcpy(&usock->addr, sun, addrlen);
    usock->addrlen = addrlen;
    mtx_unlock(&usock->lock);

    DPRINTF("connected DGRAM socket to default destination\n");
    return 0;
  }

  // SOCK_STREAM connection
  unix_socket_t *peer = unix_find_bound_socket(sun, addrlen);
  if (!peer) {
    return -ECONNREFUSED;
  }

  mtx_lock(&peer->lock);
  if (peer->stream.backlog == 0) {
    mtx_unlock(&peer->lock);
    unix_sock_putref(&peer);
    return -ECONNREFUSED;
  }

  if (peer->stream.accept_queue_len >= (size_t)peer->stream.backlog) {
    mtx_unlock(&peer->lock);
    unix_sock_putref(&peer);
    return -ECONNREFUSED;
  }

  unix_sock_getref(usock);
  LIST_ADD(&peer->stream.accept_queue, usock, aq_link);
  peer->stream.accept_queue_len++;

  cond_broadcast(&peer->rx_cond);
  knlist_activate_notes(&peer->knlist, 0);

  mtx_unlock(&peer->lock);

  // wait for the server to accept and wire up usock->stream.peer
  mtx_lock(&usock->lock);
  while (!usock->stream.peer) {
    cond_wait(&usock->rx_cond, &usock->lock);
  }
  mtx_unlock(&usock->lock);

  sock->state = SS_CONNECTED;
  DPRINTF("connected STREAM socket\n");
  return 0;
}

static int unix_listen(sock_t *sock, int backlog) {
  unix_socket_t *usock = sock->sk;
  ASSERT(usock != NULL);
  if (usock->type != SOCK_STREAM) {
    return -EOPNOTSUPP;
  }

  mtx_lock(&usock->lock);
  if (!usock->bound) {
    mtx_unlock(&usock->lock);
    return -EINVAL;
  }

  if (backlog < 0) {
    backlog = 0;
  } else if (backlog > UNIX_BACKLOG_MAX) {
    backlog = UNIX_BACKLOG_MAX;
  }

  usock->stream.backlog = backlog;
  mtx_unlock(&usock->lock);

  DPRINTF("listening with backlog=%d\n", backlog);
  return 0;
}

static int unix_accept(sock_t *sock, sock_t *newsock, int flags) {
  unix_socket_t *usock = sock->sk;
  ASSERT(usock != NULL);
  if (usock->type != SOCK_STREAM) {
    return -EOPNOTSUPP;
  }

  mtx_lock(&usock->lock);
  while (usock->stream.accept_queue_len == 0) {
    if (flags & O_NONBLOCK) {
      mtx_unlock(&usock->lock);
      return -EAGAIN;
    }

    cond_wait(&usock->rx_cond, &usock->lock);
  }

  unix_socket_t *client = LIST_REMOVE_FIRST(&usock->stream.accept_queue, aq_link);
  usock->stream.accept_queue_len--;

  mtx_unlock(&usock->lock);

  // create new connected socket
  unix_socket_t *accepted = kmallocz(sizeof(unix_socket_t));
  if (!accepted) {
    unix_sock_putref(&client);
    return -ENOMEM;
  }

  accepted->type = SOCK_STREAM;
  accepted->rcvbuf = UNIX_BUFFER_SIZE;
  accepted->sndbuf = UNIX_BUFFER_SIZE;

  // allocate stream buffer
  uintptr_t buffer = vmap_anon(UNIX_BUFFER_SIZE, 0, UNIX_BUFFER_SIZE, VM_RDWR, "unix_stream");
  if (!buffer) {
    kfree(accepted);
    unix_sock_putref(&client);
    return -ENOMEM;
  }
  accepted->stream.buffer = (void *)buffer;
  accepted->stream.buffer_size = UNIX_BUFFER_SIZE;

  initref(accepted);
  mtx_init(&accepted->lock, 0, "unix_lock");
  cond_init(&accepted->rx_cond, "unix_rx");
  cond_init(&accepted->tx_cond, "unix_tx");
  knlist_init(&accepted->knlist, &accepted->lock.lo);

  accepted->stream.peer = unix_sock_getref(client);

  mtx_lock(&client->lock);
  client->stream.peer = unix_sock_getref(accepted);
  cond_broadcast(&client->rx_cond);
  mtx_unlock(&client->lock);

  newsock->sk = accepted;
  newsock->knlist = &accepted->knlist;
  newsock->state = SS_CONNECTED;
  unix_sock_putref(&client);

  mtx_lock(&unix_sockets_lock);
  LIST_ADD(&unix_sockets, accepted, link);
  mtx_unlock(&unix_sockets_lock);

  DPRINTF("accepted connection\n");
  return 0;
}

static int unix_sendmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  unix_socket_t *usock = sock->sk;
  ASSERT(usock != NULL);

  if (usock->type == SOCK_DGRAM) {
    if (len > UNIX_MAX_DGRAM_SIZE) {
      return -EMSGSIZE;
    }

    // determine destination
    struct sockaddr_un *dest = NULL;
    socklen_t destlen = 0;

    if (msg->msg_name) {
      dest = (struct sockaddr_un *)msg->msg_name;
      destlen = msg->msg_namelen;

      if (destlen < sizeof(sa_family_t) || dest->sun_family != AF_UNIX) {
        return -EINVAL;
      }
    } else if (usock->addrlen > 0) {
      dest = &usock->addr;
      destlen = usock->addrlen;
    } else {
      return -EDESTADDRREQ;
    }

    unix_socket_t *peer = unix_find_bound_socket(dest, destlen);
    if (!peer) {
      return -ECONNREFUSED;
    }

    if (peer->type != SOCK_DGRAM) {
      unix_sock_putref(&peer);
      return -EPROTOTYPE;
    }

    // allocate message
    unix_dgram_msg_t *dgram = kmallocz(sizeof(unix_dgram_msg_t) + len);
    if (!dgram) {
      unix_sock_putref(&peer);
      return -ENOMEM;
    }

    if (usock->bound) {
      memcpy(&dgram->addr, &usock->addr, usock->addrlen);
      dgram->addrlen = usock->addrlen;
    } else {
      dgram->addr.sun_family = AF_UNIX;
      dgram->addrlen = sizeof(sa_family_t);
    }

    dgram->len = len;
    copy_from_iovec(msg->msg_iov, msg->msg_iovlen, dgram->data, len);

    // queue message
    mtx_lock(&peer->lock);
    if (peer->dgram.rx_queue_bytes + len > (size_t)peer->rcvbuf) {
      mtx_unlock(&peer->lock);
      unix_sock_putref(&peer);
      kfree(dgram);
      return -ENOBUFS;
    }

    LIST_ADD(&peer->dgram.rx_queue, dgram, link);
    peer->dgram.rx_queue_len++;
    peer->dgram.rx_queue_bytes += len;

    cond_broadcast(&peer->rx_cond);
    knlist_activate_notes(&peer->knlist, 0);

    mtx_unlock(&peer->lock);
    unix_sock_putref(&peer);

    return (int)len;
  }

  // SOCK_STREAM
  if (!usock->stream.peer) {
    return -ENOTCONN;
  }

  unix_socket_t *peer = usock->stream.peer;

  mtx_lock(&usock->lock);
  if (usock->stream.shutdown_flags & SHUT_WR) {
    // shut down for writing
    mtx_unlock(&usock->lock);
    return -EPIPE;
  }
  mtx_unlock(&usock->lock);

  size_t total_written = 0;
  size_t to_write = len;

  mtx_lock(&peer->lock);

  if (peer->stream.shutdown_flags & SHUT_RD) {
    // peer shut down for reading
    mtx_unlock(&peer->lock);
    return -EPIPE;
  }

  // write data to peer's buffer
  while (to_write > 0) {
    size_t available = peer->stream.buffer_size - peer->stream.count;

    while (available == 0) {
      // buffer full, wait
      if (flags & MSG_DONTWAIT) {
        if (total_written > 0) {
          mtx_unlock(&peer->lock);
          return (int)total_written;
        }
        mtx_unlock(&peer->lock);
        return -EAGAIN;
      }

      cond_wait(&peer->tx_cond, &peer->lock);

      if (peer->stream.shutdown_flags & SHUT_RD) {
        mtx_unlock(&peer->lock);
        return total_written > 0 ? (int)total_written : -EPIPE;
      }

      available = peer->stream.buffer_size - peer->stream.count;
    }

    size_t chunk = min(to_write, available);
    size_t written = stream_buffer_write_iovec(&peer->stream, msg->msg_iov, msg->msg_iovlen, chunk);
    total_written += written;
    to_write = len - total_written;

    // wake up reader
    cond_broadcast(&peer->rx_cond);
    knlist_activate_notes(&peer->knlist, 0);
  }

  mtx_unlock(&peer->lock);
  return (int)total_written;
}

static int unix_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags) {
  unix_socket_t *usock = sock->sk;
  if (!usock) {
    return -EINVAL;
  }

  if (usock->type == SOCK_DGRAM) {
    // datagram socket
    mtx_lock(&usock->lock);

    while (usock->dgram.rx_queue_len == 0) {
      if (flags & MSG_DONTWAIT) {
        mtx_unlock(&usock->lock);
        return -EAGAIN;
      }

      cond_wait(&usock->rx_cond, &usock->lock);
    }

    unix_dgram_msg_t *dgram = LIST_REMOVE_FIRST(&usock->dgram.rx_queue, link);
    usock->dgram.rx_queue_len--;
    usock->dgram.rx_queue_bytes -= dgram->len;

    mtx_unlock(&usock->lock);

    // copy source address
    if (msg->msg_name && msg->msg_namelen > 0) {
      socklen_t to_copy = min(msg->msg_namelen, dgram->addrlen);
      memcpy(msg->msg_name, &dgram->addr, to_copy);
      msg->msg_namelen = to_copy;
    }

    // copy data to iovec
    size_t to_copy = min(len, dgram->len);
    copy_to_iovec(msg->msg_iov, msg->msg_iovlen, dgram->data, to_copy);

    int result = (int)to_copy;
    if (to_copy < dgram->len) {
      msg->msg_flags |= MSG_TRUNC;
    }

    kfree(dgram);
    return result;
  }

  // SOCK_STREAM
  if (!usock->stream.peer) {
    return -ENOTCONN;
  }

  mtx_lock(&usock->lock);

  // check if shut down for reading
  if (usock->stream.shutdown_flags & SHUT_RD) {
    mtx_unlock(&usock->lock);
    return 0;
  }

  size_t total_read = 0;
  size_t to_read = len;

  while (to_read > 0) {
    while (usock->stream.count == 0) {
      if (total_read > 0) {
        // return partial read instead of blocking for more
        goto out;
      }

      // check if peer closed
      if (usock->stream.shutdown_flags & SHUT_WR) {
        mtx_unlock(&usock->lock);
        return (int)total_read;
      }

      if (flags & MSG_DONTWAIT) {
        mtx_unlock(&usock->lock);
        return -EAGAIN;
      }

      cond_wait(&usock->rx_cond, &usock->lock);
    }

    size_t available = usock->stream.count;
    size_t chunk = min(to_read, available);

    size_t read = stream_buffer_read_iovec(&usock->stream, msg->msg_iov, msg->msg_iovlen, chunk);
    total_read += read;
    to_read = len - total_read;

    // wake up writer
    if (usock->stream.peer) {
      unix_socket_t *peer = usock->stream.peer;
      cond_broadcast(&peer->tx_cond);
    }
  }

out:

  mtx_unlock(&usock->lock);
  return (int)total_read;
}

static int unix_shutdown(sock_t *sock, int how) {
  unix_socket_t *usock = sock->sk;
  if (!usock) {
    return -EINVAL;
  }

  if (usock->type != SOCK_STREAM) {
    return -ENOTCONN;
  }

  if (!usock->stream.peer) {
    return -ENOTCONN;
  }

  mtx_lock(&usock->lock);

  if (how == SHUT_RD || how == SHUT_RDWR) {
    usock->stream.shutdown_flags |= SHUT_RD;
    cond_broadcast(&usock->rx_cond);
  }

  if (how == SHUT_WR || how == SHUT_RDWR) {
    usock->stream.shutdown_flags |= SHUT_WR;

    // notify peer
    if (usock->stream.peer) {
      unix_socket_t *peer = usock->stream.peer;
      mtx_lock(&peer->lock);
      peer->stream.shutdown_flags |= SHUT_WR;
      cond_broadcast(&peer->rx_cond);
      knlist_activate_notes(&peer->knlist, 0);
      mtx_unlock(&peer->lock);
    }
  }

  mtx_unlock(&usock->lock);

  DPRINTF("shutdown how=%d\n", how);
  return 0;
}

static int unix_setsockopt(sock_t *sock, int level, int optname, const void *optval, socklen_t optlen) {
  unix_socket_t *usock = sock->sk;
  if (!usock) {
    return -EINVAL;
  }

  if (level != SOL_SOCKET) {
    return -ENOPROTOOPT;
  }

  if (optlen < sizeof(int)) {
    return -EINVAL;
  }

  int value = *(const int *)optval;

  mtx_lock(&usock->lock);

  switch (optname) {
    case SO_RCVBUF:
      usock->rcvbuf = value;
      break;
    case SO_SNDBUF:
      usock->sndbuf = value;
      break;
    default:
      mtx_unlock(&usock->lock);
      return -ENOPROTOOPT;
  }

  mtx_unlock(&usock->lock);
  return 0;
}

static int unix_getsockopt(sock_t *sock, int level, int optname, void *optval, socklen_t *optlen) {
  unix_socket_t *usock = sock->sk;
  if (!usock) {
    return -EINVAL;
  }

  if (level != SOL_SOCKET) {
    return -ENOPROTOOPT;
  }

  if (*optlen < sizeof(int)) {
    return -EINVAL;
  }

  int value = 0;

  mtx_lock(&usock->lock);

  switch (optname) {
    case SO_RCVBUF:
      value = usock->rcvbuf;
      break;
    case SO_SNDBUF:
      value = usock->sndbuf;
      break;
    case SO_TYPE:
      value = usock->type;
      break;
    default:
      mtx_unlock(&usock->lock);
      return -ENOPROTOOPT;
  }

  mtx_unlock(&usock->lock);

  *(int *)optval = value;
  *optlen = sizeof(int);
  return 0;
}

static int unix_getsockname(sock_t *sock, struct sockaddr *addr, socklen_t *addrlen) {
  unix_socket_t *usock = sock->sk;
  if (!usock) {
    return -EINVAL;
  }

  mtx_lock(&usock->lock);

  if (!usock->bound) {
    // return anonymous address
    struct sockaddr_un sun = { .sun_family = AF_UNIX };
    socklen_t len = sizeof(sa_family_t);
    if (*addrlen < len) {
      mtx_unlock(&usock->lock);
      return -EINVAL;
    }
    memcpy(addr, &sun, len);
    *addrlen = len;
    mtx_unlock(&usock->lock);
    return 0;
  }

  socklen_t len = usock->addrlen;
  if (*addrlen < len) {
    mtx_unlock(&usock->lock);
    return -EINVAL;
  }

  memcpy(addr, &usock->addr, len);
  *addrlen = len;

  mtx_unlock(&usock->lock);
  return 0;
}

static int unix_getpeername(sock_t *sock, struct sockaddr *addr, socklen_t *addrlen) {
  unix_socket_t *usock = sock->sk;
  ASSERT(usock != NULL);

  if (usock->type == SOCK_DGRAM) {
    // for DGRAM, return the connected address
    mtx_lock(&usock->lock);

    if (usock->addrlen == 0) {
      mtx_unlock(&usock->lock);
      return -ENOTCONN;
    }

    socklen_t len = usock->addrlen;
    if (*addrlen < len) {
      mtx_unlock(&usock->lock);
      return -EINVAL;
    }

    memcpy(addr, &usock->addr, len);
    *addrlen = len;

    mtx_unlock(&usock->lock);
    return 0;
  }

  // SOCK_STREAM
  mtx_lock(&usock->lock);

  if (!usock->stream.peer) {
    mtx_unlock(&usock->lock);
    return -ENOTCONN;
  }

  unix_socket_t *peer = usock->stream.peer;
  mtx_lock(&peer->lock);

  if (!peer->bound) {
    // return anonymous address
    struct sockaddr_un sun = { .sun_family = AF_UNIX };
    socklen_t len = sizeof(sa_family_t);
    if (*addrlen < len) {
      mtx_unlock(&peer->lock);
      mtx_unlock(&usock->lock);
      return -EINVAL;
    }
    memcpy(addr, &sun, len);
    *addrlen = len;
  } else {
    socklen_t len = peer->addrlen;
    if (*addrlen < len) {
      mtx_unlock(&peer->lock);
      mtx_unlock(&usock->lock);
      return -EINVAL;
    }

    memcpy(addr, &peer->addr, len);
    *addrlen = len;
  }

  mtx_unlock(&peer->lock);
  mtx_unlock(&usock->lock);
  return 0;
}

static int unix_kqevent(sock_t *sock, knote_t *kn) {
  unix_socket_t *usock = sock->sk;
  if (!usock)
    return -EBADF;

  bool locked = mtx_owner(&usock->lock) != curthread;
  if (locked)
    mtx_lock(&usock->lock);
  int ret = 0;

  if (usock->type == SOCK_STREAM) {
    unix_stream_t *st = &usock->stream;
    bool listening = st->backlog > 0;
    switch (kn->event.filter) {
      case EVFILT_READ:
        if (listening) {
          if (st->accept_queue_len > 0) {
            kn->event.data = (intptr_t)st->accept_queue_len;
            ret = 1;
          }
        } else if (st->count > 0) {
          kn->event.data = (intptr_t)st->count;
          ret = 1;
        } else if (!st->peer || (st->shutdown_flags & SHUT_RD)) {
          kn->event.flags |= EV_EOF;
          ret = 1;
        }
        break;
      case EVFILT_WRITE:
        if (listening) {
          break;
        } else if (!st->peer || (st->shutdown_flags & SHUT_WR)) {
          kn->event.flags |= EV_EOF;
          ret = 1;
        } else if (st->buffer_size - st->count > 0) {
          kn->event.data = (intptr_t)(st->buffer_size - st->count);
          ret = 1;
        }
        break;
      default:
        ret = -EINVAL;
        break;
    }
  } else {
    unix_dgram_t *dg = &usock->dgram;
    switch (kn->event.filter) {
      case EVFILT_READ:
        if (dg->rx_queue_len > 0) {
          kn->event.data = (intptr_t)dg->rx_queue_len;
          ret = 1;
        }
        break;
      case EVFILT_WRITE:
        kn->event.data = UNIX_MAX_DGRAM_SIZE;
        ret = 1;
        break;
      default:
        ret = -EINVAL;
        break;
    }
  }

  if (locked)
    mtx_unlock(&usock->lock);
  return ret;
}

static const struct proto_ops unix_proto_ops = {
  .family = AF_UNIX,
  .create = unix_create,
  .release = unix_release,
  .bind = unix_bind,
  .connect = unix_connect,
  .listen = unix_listen,
  .accept = unix_accept,
  .sendmsg = unix_sendmsg,
  .recvmsg = unix_recvmsg,
  .shutdown = unix_shutdown,
  .setsockopt = unix_setsockopt,
  .getsockopt = unix_getsockopt,
  .getsockname = unix_getsockname,
  .getpeername = unix_getpeername,
  .kqevent = unix_kqevent,
};

//
// MARK: Module Initialization
//

static void unix_init() {
  mtx_init(&unix_sockets_lock, 0, "unix_sockets");
  if (proto_register(&unix_proto_ops) < 0) {
    panic("Failed to register AF_UNIX protocol");
  }
  DPRINTF("AF_UNIX socket support initialized\n");
}
MODULE_INIT(unix_init);
