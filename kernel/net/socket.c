//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#include <kernel/net/netdev.h>
#include <kernel/net/tcp.h>
#include <kernel/net/udp.h>
#include <kernel/vfs/file.h>

#include <kernel/mm.h>
#include <kernel/proc.h>
#include <kernel/fs.h>
#include <kernel/kevent.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("socket: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("socket: %s: " fmt, __func__, ##__VA_ARGS__)

// protocol family registry
static const struct proto_ops *proto_families[AF_MAX] = { NULL };
static mtx_t proto_lock;

// socket file operations
static int socket_open(file_t *file, int flags);
static ssize_t socket_read(file_t *file, kio_t *kio);
static ssize_t socket_write(file_t *file, kio_t *kio);
static int socket_ioctl(file_t *file, unsigned int request, void *arg);
static int socket_kqevent(file_t *file, knote_t *kn);
static int socket_close(file_t *file);
static void socket_cleanup(file_t *file);

static const struct file_ops socket_file_ops = {
  .f_open = socket_open,
  .f_read = socket_read,
  .f_write = socket_write,
  .f_ioctl = socket_ioctl,
  .f_kqevent = socket_kqevent,
  .f_close = socket_close,
  .f_cleanup = socket_cleanup,
};

void socket_static_init() {
  mtx_init(&proto_lock, 0, "proto_families");
}
STATIC_INIT(socket_static_init);

//
// MARK: Protocol Registration
//

int proto_register(struct proto_ops *ops) {
  if (!ops || ops->family >= AF_MAX) {
    return -EINVAL;
  }

  mtx_lock(&proto_lock);
  if (proto_families[ops->family]) {
    mtx_unlock(&proto_lock);
    return -EEXIST;
  }

  proto_families[ops->family] = ops;
  mtx_unlock(&proto_lock);

  DPRINTF("registered protocol family %d\n", ops->family);
  return 0;
}

void proto_unregister(struct proto_ops *ops) {
  if (!ops || ops->family >= AF_MAX) {
    return;
  }

  mtx_lock(&proto_lock);
  proto_families[ops->family] = NULL;
  mtx_unlock(&proto_lock);

  DPRINTF("unregistered protocol family %d\n", ops->family);
}

//
// MARK: Socket API
//

sock_t *socket_alloc() {
  sock_t *sock = kmallocz(sizeof(sock_t));
  if (!sock) {
    return NULL;
  }

  sock->state = SS_UNCONNECTED;
  sock->type = 0;
  sock->flags = 0;
  sock->ops = NULL;
  sock->sk = NULL;
  initref(sock);

  return sock;
}

void socket_free(sock_t *sock) {
  if (!sock) {
    return;
  }

  ASSERT(read_refcount(sock) == 0);
  if (sock->ops && sock->ops->release) {
    sock->ops->release(sock);
  }

  kfree(sock);
}

//
// MARK: File Operations
//

static int socket_open(file_t *file, int flags) {
  return 0;
}

static ssize_t socket_read(file_t *file, kio_t *kio) {
  sock_t *sock = (sock_t *)file->data;
  if (!sock) {
    return -EBADF;
  }

  if (!sock->ops || !sock->ops->recvmsg) {
    return -EOPNOTSUPP;
  }

  // implement read as recvfrom with no address
  struct msghdr msg = { 0 };

  if (kio->kind == KIO_IOV) {
    // kio already has iovec array, use it directly
    msg.msg_iov = (struct iovec *)kio->iov.arr;
    msg.msg_iovlen = (int)kio->iov.cnt;
  } else {
    // kio has a single buffer, create iovec
    struct iovec iov = {
      .iov_base = kio->buf.base + kio->buf.off,
      .iov_len = kio_remaining(kio)
    };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
  }

  ssize_t ret = sock->ops->recvmsg(sock, &msg, kio_remaining(kio), 0);
  if (ret > 0) {
    // update kio to reflect data received
    if (kio->kind == KIO_BUF) {
      kio->buf.off += ret;
    } else {
      // for IOV, we'd need to update the indices properly
      // simplified approach for now
      kio->iov.t_off += ret;
    }
  }

  return ret;
}

static ssize_t socket_write(file_t *file, kio_t *kio) {
  sock_t *sock = file->data;
  if (!sock) {
    return -EBADF;
  }

  if (!sock->ops || !sock->ops->sendmsg) {
    return -EOPNOTSUPP;
  }

  // implement write as sendto with no address
  struct msghdr msg = { 0 };

  if (kio->kind == KIO_IOV) {
    // kio already has iovec array, use it directly
    msg.msg_iov = (struct iovec *)kio->iov.arr;
    msg.msg_iovlen = (int)kio->iov.cnt;
  } else {
    // kio has a single buffer, create iovec
    struct iovec iov = {
      .iov_base = kio->buf.base + kio->buf.off,
      .iov_len = kio_remaining(kio)
    };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
  }

  ssize_t ret = sock->ops->sendmsg(sock, &msg, kio_remaining(kio));
  if (ret > 0) {
    // update kio to reflect data sent
    if (kio->kind == KIO_BUF) {
      kio->buf.off += ret;
    } else {
      // for IOV, we'd need to update the indices properly
      // simplified approach for now
      kio->iov.t_off += ret;
    }
  }

  return ret;
}

static int socket_ioctl(file_t *file, unsigned int request, void *arg) {
  sock_t *sock = file->data;
  if (!sock) {
    return -EBADF;
  }

  DPRINTF("socket ioctl: request=0x%x\n", request);

  // for now, delegate all socket ioctls to the netdev layer
  // this includes interface configuration commands like SIOCSIFFLAGS
  int res = netdev_ioctl(request, (uintptr_t) arg);
  DPRINTF("socket ioctl: request=0x%x returned %d\n", request, res);
  return res;
}

static int socket_kqevent(file_t *file, knote_t *kn) {
  sock_t *sock = file->data;
  if (!sock) {
    return -EBADF;
  }

  // for TCP sockets, check the protocol socket directly
  if (sock->type == SOCK_STREAM && sock->sk) {
    tcp_sock_t *tcp_sk = (tcp_sock_t *)sock->sk;

    mtx_lock(&tcp_sk->lock);
    int ret = 0;

    switch (kn->event.filter) {
      case EVFILT_READ: {
        // check if data is available or accept queue has connections
        if (tcp_sk->state == TCP_LISTEN) {
          // listening socket - check accept queue
          if (tcp_sk->accept_queue_len > 0) {
            kn->event.data = (intptr_t)tcp_sk->accept_queue_len;
            ret = 1;
          }
        } else if (tcp_sk->state == TCP_ESTABLISHED ||
                   tcp_sk->state == TCP_FIN_WAIT_1 ||
                   tcp_sk->state == TCP_FIN_WAIT_2) {
          // connected socket - check receive queue
          if (tcp_sk->recv_queue_len > 0) {
            kn->event.data = (intptr_t)tcp_sk->recv_queue_len;
            ret = 1;
          }
        } else if (tcp_sk->state == TCP_CLOSE_WAIT) {
          // peer closed - data available or EOF
          if (tcp_sk->recv_queue_len > 0) {
            kn->event.data = (intptr_t)tcp_sk->recv_queue_len;
            ret = 1;
          } else {
            kn->flags |= EV_EOF;
            ret = 1;
          }
        } else if (tcp_sk->state == TCP_CLOSED) {
          // socket closed
          kn->flags |= EV_EOF;
          ret = 1;
        }
        break;
      }

      case EVFILT_WRITE: {
        // check if socket is writable
        if (tcp_sk->state == TCP_ESTABLISHED ||
            tcp_sk->state == TCP_CLOSE_WAIT) {
          // connected socket - always writable for now (no send buffering limits)
          kn->event.data = TCP_DEFAULT_WINDOW;
          ret = 1;
        } else if (tcp_sk->state == TCP_SYN_SENT ||
                   tcp_sk->state == TCP_SYN_RECEIVED) {
          // connecting - not yet writable
          ret = 0;
        } else if (tcp_sk->state == TCP_CLOSED ||
                   tcp_sk->state == TCP_FIN_WAIT_1 ||
                   tcp_sk->state == TCP_FIN_WAIT_2 ||
                   tcp_sk->state == TCP_CLOSING ||
                   tcp_sk->state == TCP_LAST_ACK ||
                   tcp_sk->state == TCP_TIME_WAIT) {
          // socket closing or closed
          kn->flags |= EV_EOF;
          ret = 1;
        }
        break;
      }

      default:
        ret = -EINVAL;
        break;
    }

    mtx_unlock(&tcp_sk->lock);
    return ret;
  } else if (sock->type == SOCK_DGRAM && sock->sk) {
    udp_sock_t *udp_sk = sock->sk;
    int ret = 0;
    switch (kn->event.filter) {
      case EVFILT_READ: {
        // check if data is available in receive queue
        if (udp_sk->rx_queue_len > 0) {
          kn->event.data = (intptr_t)udp_sk->rx_queue_len;
          ret = 1;
        }
        break;
      }

      case EVFILT_WRITE: {
        // UDP sockets are always writable (datagrams are not buffered)
        kn->event.data = 65535;  // maximum UDP payload size
        ret = 1;
        break;
      }

      default:
        ret = -EINVAL;
        break;
    }

    return ret;
  }

  // for other socket types, not yet implemented
  return -EOPNOTSUPP;
}

static int socket_close(file_t *file) {
  sock_t *sock = moveref(file->data);
  if (!sock) {
    return 0;
  }

  sock_putref(&sock);
  return 0;
}

static void socket_cleanup(file_t *file) {
  // file->data should already be NULL after socket_close
  // just ensure cleanup is complete
  file->data = NULL;
  file->udata = NULL;
}

//
// System Call Implementations
//

int net_socket(int domain, int type, int protocol) {
  DPRINTF("socket: domain=%d, type=%d, protocol=%d\n", domain, type, protocol);

  if (domain >= AF_MAX) {
    DPRINTF("socket: domain %d >= AF_MAX\n", domain);
    return -EAFNOSUPPORT;
  }

  // extract socket flags
  int sock_flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
  type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

  // get protocol family operations
  mtx_lock(&proto_lock);
  const struct proto_ops *ops = proto_families[domain];
  mtx_unlock(&proto_lock);

  if (!ops) {
    DPRINTF("socket: no protocol family registered for domain %d\n", domain);
    return -EAFNOSUPPORT;
  }

  // allocate socket
  sock_t *sock = socket_alloc();
  if (!sock) {
    return -ENOMEM;
  }

  sock->type = type;
  sock->flags = sock_flags;
  sock->ops = ops;

  // create protocol socket
  int ret = 0;
  if (ops->create) {
    ret = ops->create(sock, protocol);
    if (ret < 0) {
      sock_putref(&sock);
      return ret;
    }
  }

  // create file descriptor
  int fflags = (sock_flags & SOCK_NONBLOCK) ? O_NONBLOCK : 0;
  file_t *file = f_alloc(FT_SOCK, fflags, sock, &socket_file_ops);
  if (!file) {
    sock_putref(&sock);
    return -ENOMEM;
  }

  // open the file
  f_lock(file);
  ret = f_open(file, 0);
  f_unlock(file);
  if (ret < 0) {
    f_putref(&file);
    sock_putref(&sock);
    return ret;
  }

  // allocate file descriptor
  proc_t *proc = curproc;
  int fd = fs_proc_alloc_fd(proc);
  if (fd < 0) {
    f_putref(&file);
    return fd;
  }

  // create fd entry
  int fdeflags = (sock_flags & SOCK_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_entry_t *fde = fd_entry_alloc(fd, fdeflags, cstr_make("socket"), file);
  if (!fde) {
    fs_proc_free_fd(proc, fd);
    f_putref(&file);
    return -ENOMEM;
  }

  fs_proc_add_fdentry(proc, fde);
  return fd;
}

int net_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  int ret = sock->ops->bind(sock, (struct sockaddr *)addr, (int)addrlen);
  fde_putref(&fde);
  return ret;
}

int net_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  int ret = sock->ops->connect(sock, (struct sockaddr *)addr, (int)addrlen, 0);
  fde_putref(&fde);
  return ret;
}

int net_listen(int sockfd, int backlog) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  int ret = sock->ops->listen(sock, backlog);
  fde_putref(&fde);
  return ret;
}

int net_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  // allocate a new socket for the accepted connection
  sock_t *sock = file->data;
  sock_t *newsock = socket_alloc();
  if (!newsock) {
    fde_putref(&fde);
    return -ENOMEM;
  }
  newsock->type = sock->type;
  newsock->flags = sock->flags;
  newsock->ops = sock->ops;

  // call the protocol accept function
  int ret = sock->ops->accept(sock, newsock, 0);
  if (ret < 0) {
    sock_putref(&newsock);
    fde_putref(&fde);
    return ret;
  }

  // create a new file for the accepted socket
  file_t *newfile = f_alloc(FT_SOCK, 0, newsock, &socket_file_ops);
  if (!newfile) {
    sock_putref(&newsock);
    fde_putref(&fde);
    return -ENOMEM;
  }

  // open the file
  f_lock(newfile);
  ret = f_open(newfile, 0);
  f_unlock(newfile);
  if (ret < 0) {
    f_putref(&newfile);
    fde_putref(&fde);
    return ret;
  }

  // allocate a file descriptor for it
  int newfd = fs_proc_alloc_fd(proc);
  if (newfd < 0) {
    f_putref(&newfile);
    fde_putref(&fde);
    return newfd;
  }

  // create fd entry
  fd_entry_t *new_fde = fd_entry_alloc(newfd, 0, cstr_make("socket"), newfile);
  if (!new_fde) {
    fs_proc_free_fd(proc, newfd);
    f_putref(&newfile);
    fde_putref(&fde);
    return -ENOMEM;
  }

  fs_proc_add_fdentry(proc, new_fde);

  // optionally fill in the peer address
  if (addr && addrlen) {
    EPRINTF("getpeername not yet implemented\n");
    // todo: implement getpeername functionality
    *addrlen = 0;
  }

  fde_putref(&fde);
  return newfd;
}

ssize_t net_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  if (!sock->ops->sendmsg) {
    fde_putref(&fde);
    return -EOPNOTSUPP;
  }

  // build message structure
  struct msghdr msg = { 0 };
  struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };

  msg.msg_name = (void *)dest_addr;
  msg.msg_namelen = addrlen;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  int ret = sock->ops->sendmsg(sock, &msg, len);
  fde_putref(&fde);
  return ret;
}

ssize_t net_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  if (!sock->ops->recvmsg) {
    fde_putref(&fde);
    return -EOPNOTSUPP;
  }

  // build message structure
  struct msghdr msg = { 0 };
  struct iovec iov = { .iov_base = buf, .iov_len = len };

  msg.msg_name = src_addr;
  msg.msg_namelen = addrlen ? *addrlen : 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  int ret = sock->ops->recvmsg(sock, &msg, len, flags);
  if (ret >= 0 && addrlen) {
    *addrlen = msg.msg_namelen;
  }

  fde_putref(&fde);
  return ret;
}

ssize_t net_sendmsg(int sockfd, const struct msghdr *msg, int flags) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  ssize_t ret = -EOPNOTSUPP;
  if (sock->ops->sendmsg) {
    // calculate total message size
    size_t len = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
      len += msg->msg_iov[i].iov_len;
    }
    ret = sock->ops->sendmsg(sock, (struct msghdr *)msg, len);
  }

  fde_putref(&fde);
  return ret;
}

ssize_t net_recvmsg(int sockfd, struct msghdr *msg, int flags) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  if (!sock->ops->recvmsg) {
    fde_putref(&fde);
    return -EOPNOTSUPP;
  }

  // for now, assume the message length is the total of all iovec lengths
  size_t total_len = 0;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    total_len += msg->msg_iov[i].iov_len;
  }

  DPRINTF("net_recvmsg: calling socket recvmsg, total_len=%zu\n", total_len);
  int ret = sock->ops->recvmsg(sock, msg, total_len, flags);
  DPRINTF("net_recvmsg: socket recvmsg returned %d\n", ret);
  fde_putref(&fde);
  DPRINTF("net_recvmsg: returning %d\n", ret);
  return ret;
}

int net_shutdown(int sockfd, int how) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  if (!sock->ops->shutdown) {
    fde_putref(&fde);
    return -EOPNOTSUPP;
  }

  int ret = sock->ops->shutdown(sock, how);
  fde_putref(&fde);
  return ret;
}

int net_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
  return -EOPNOTSUPP;  // not implemented for initial version
}

int net_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
  return -EOPNOTSUPP;  // not implemented for initial version
}

int net_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  proc_t *proc = curproc;
  fd_entry_t *fde = fs_proc_get_fdentry(proc, sockfd);
  if (!fde) {
    return -EBADF;
  }

  file_t *file = fde->file;
  if (!F_ISSOCK(file)) {
    fde_putref(&fde);
    return -ENOTSOCK;
  }

  sock_t *sock = file->data;
  if (!sock->ops->getsockname) {
    fde_putref(&fde);
    return -EOPNOTSUPP;
  }

  socklen_t len;
  if (addrlen) {
    len = *addrlen;
  } else {
    len = 0;
  }

  int ret = sock->ops->getsockname(sock, addr, &len);
  if (ret == 0 && addrlen) {
    *addrlen = len;
  }

  fde_putref(&fde);
  return ret;
}

int net_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  EPRINTF("net_getpeername: not implemented\n");
  return -EOPNOTSUPP;
}

int net_socketpair(int domain, int type, int protocol, int sv[2]) {
  EPRINTF("net_socketpair: not implemented\n");
  return -EOPNOTSUPP;
}

//
// MARK: System Calls
//

SYSCALL_ALIAS(socket, net_socket);
SYSCALL_ALIAS(bind, net_bind);
SYSCALL_ALIAS(connect, net_connect);
SYSCALL_ALIAS(listen, net_listen);
SYSCALL_ALIAS(accept, net_accept);
SYSCALL_ALIAS(sendto, net_sendto);
SYSCALL_ALIAS(recvfrom, net_recvfrom);
SYSCALL_ALIAS(sendmsg, net_sendmsg);
SYSCALL_ALIAS(recvmsg, net_recvmsg);
SYSCALL_ALIAS(shutdown, net_shutdown);
SYSCALL_ALIAS(setsockopt, net_setsockopt);
SYSCALL_ALIAS(getsockopt, net_getsockopt);
SYSCALL_ALIAS(getsockname, net_getsockname);
SYSCALL_ALIAS(getpeername, net_getpeername);
SYSCALL_ALIAS(socketpair, net_socketpair);
