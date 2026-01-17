//
// Created by Aaron Gill-Braun on 2025-10-20.
//

#ifndef KERNEL_NET_INET_H
#define KERNEL_NET_INET_H

#include <kernel/base.h>

struct proto_ops;

int inet_register_protocol(int type, int protocol, const struct proto_ops *ops);
void inet_unregister_protocol(int type, int protocol);

#endif
