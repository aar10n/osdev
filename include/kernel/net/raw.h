//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#ifndef KERNEL_NET_RAW_H
#define KERNEL_NET_RAW_H

#include <kernel/base.h>

typedef struct sock sock_t;
typedef struct sk_buff sk_buff_t;
struct sockaddr;
struct msghdr;

//
// MARK: Raw Socket Protocol
//

int raw_create(sock_t *sock, int protocol);
int raw_release(sock_t *sock);
int raw_bind(sock_t *sock, struct sockaddr *addr, int addrlen);
int raw_connect(sock_t *sock, struct sockaddr *addr, int addrlen, int flags);
int raw_sendmsg(sock_t *sock, struct msghdr *msg, size_t len);
int raw_recvmsg(sock_t *sock, struct msghdr *msg, size_t len, int flags);

int raw_rcv(sk_buff_t *skb, uint8_t protocol);

#endif
