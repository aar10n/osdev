//
// Created by Aaron Gill-Braun on 2021-08-31.
//

#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <base.h>


// Generic message struct serving as the base for all
// other message formats. All messages are 64-bytes
// large with a usable payload size of 56-bytes.
typedef struct message {
  uint32_t origin;
  uint32_t type;
  uint8_t data[56];
} message_t;
static_assert(sizeof(message_t) == 64);

//
// Message Subtypes
//

#define IPC_ACKNOWLEDGE 0
typedef struct ipc_msg_ack {
  uint32_t origin;      // message sender
  uint32_t type;        // IPC_ACKNOWLEDGE
  uint8_t reserved[56]; // unused
} ipc_msg_ack_t;
static_assert(sizeof(ipc_msg_ack_t) == 64);

// API

int ipc_send(pid_t pid, message_t *message);
message_t *ipc_receive();
message_t *ipc_receive_nb();


#endif
