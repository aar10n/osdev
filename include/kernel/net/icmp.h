//
// Created by Aaron Gill-Braun on 2025-09-20.
//

#ifndef KERNEL_NET_ICMP_H
#define KERNEL_NET_ICMP_H

#include <kernel/base.h>
#include <linux/icmp.h>

typedef struct sk_buff sk_buff_t;

int icmp_rcv(sk_buff_t *skb);
int icmp_send_echo_reply(sk_buff_t *skb);
int icmp_send_dest_unreach(uint32_t dest, uint8_t code, sk_buff_t *orig_skb);

#endif
