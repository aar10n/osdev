//
// Created by Aaron Gill-Braun on 2025-09-14.
//

#ifndef KERNEL_NET_SOCKET_H
#define KERNEL_NET_SOCKET_H

#include <kernel/base.h>
#include <kernel/ref.h>
#include <kernel/kevent.h>

#include <sys/socket.h>

#define SS_FREE         0   // not allocated
#define SS_UNCONNECTED  1   // unconnected to any socket
#define SS_CONNECTING   2   // in process of connecting
#define SS_CONNECTED    3   // connected to socket
#define SS_DISCONNECTING 4  // in process of disconnecting

struct sock;
struct proto_ops;


/**
 * A network socket.
 */
typedef struct sock {
  int state;                    // socket state (SS_*)
  int type;                     // socket type (SOCK_*)
  int flags;                    // socket flags

  struct proto_ops *ops;        // protocol operations
  void *sk;                     // protocol specific socket

  _refcount;                    // reference counting
} sock_t;

struct proto_ops {
  int family;
  int (*create)(struct sock *sock, int protocol);
  int (*release)(struct sock *sock);
  int (*bind)(struct sock *sock, struct sockaddr *addr, int addrlen);
  int (*connect)(struct sock *sock, struct sockaddr *addr, int addrlen, int flags);
  int (*listen)(struct sock *sock, int backlog);
  int (*accept)(struct sock *sock, struct sock *newsock, int flags);
  int (*sendmsg)(struct sock *sock, struct msghdr *msg, size_t len);
  int (*recvmsg)(struct sock *sock, struct msghdr *msg, size_t len, int flags);
  int (*shutdown)(struct sock *sock, int how);
  int (*setsockopt)(struct sock *sock, int level, int optname, const void *optval, socklen_t optlen);
  int (*getsockopt)(struct sock *sock, int level, int optname, void *optval, socklen_t *optlen);
  int (*getsockname)(struct sock *sock, struct sockaddr *addr, socklen_t *addrlen);
  int (*getpeername)(struct sock *sock, struct sockaddr *addr, socklen_t *addrlen);
};

#define sock_getref(sock) ({ \
  ASSERT_IS_TYPE(sock_t *, sock); \
  socket_t *__sock = (sock); \
  __sock ? ref_get(&__sock->refcount) : NULL; \
  __sock; \
})

#define sock_putref(sockref) ({ \
  ASSERT_IS_TYPE(sock_t **, sockref); \
  sock_t *__sock = *(sockref); \
  *(sockref) = NULL; \
  if (__sock) { \
    if (ref_put(&__sock->refcount)) { \
      socket_free(__sock); \
    } \
  } \
})

//
// Socket API
//

int proto_register(struct proto_ops *ops);
void proto_unregister(struct proto_ops *ops);

sock_t *socket_alloc(void);
void socket_free(sock_t *sock);

// socket system calls
int net_socket(int domain, int type, int protocol);
int net_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int net_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int net_listen(int sockfd, int backlog);
int net_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t net_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t net_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t net_sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t net_recvmsg(int sockfd, struct msghdr *msg, int flags);
int net_shutdown(int sockfd, int how);
int net_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int net_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int net_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int net_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int net_socketpair(int domain, int type, int protocol, int sv[2]);

#endif
