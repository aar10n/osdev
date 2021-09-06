//
// Created by Aaron Gill-Braun on 2021-08-31.
//

#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#ifdef __KERNEL__
#include <base.h>
#else
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define static_assert(expr) _Static_assert(expr, "")
#endif



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

#define IPC_SUCCESS 0
typedef struct ipc_msg_ok {
  uint32_t origin;      // message sender
  uint32_t type;        // IPC_SUCCESS
  uint64_t result;      // result (optional)
  uint8_t reserved[48]; // unused
} ipc_msg_ok_t;
static_assert(sizeof(ipc_msg_ok_t) == 64);

#define IPC_FAILURE 1
typedef struct ipc_msg_fail {
  uint32_t origin;      // message sender
  uint32_t type;        // IPC_FAILURE
  uint32_t code;        // error code
  uint8_t reserved[52]; // unused
} ipc_msg_fail_t;
static_assert(sizeof(ipc_msg_fail_t) == 64);

#define IPC_MEMORY_MAP 2
typedef struct ipc_msg_mmap {
  uint32_t origin;      // message sender
  uint32_t type;        // IPC_MEMORY_MAP
  uint64_t phys_addr;   // physical address to map
  uint64_t length;      // length of region to map
  uint8_t reserved[40]; // unused
} ipc_msg_mmap_t;
static_assert(sizeof(ipc_msg_mmap_t) == 64);

#define IPC_REMOTE_CALL 3
typedef struct ipc_msg_rpc {
  uint32_t origin;      // message sender
  uint32_t type;        // IPC_REMOTE_CALL
  char call[16];        // remote procedure name
  uint64_t args[5];     // procedure arguments
} ipc_msg_rpc_t;
static_assert(sizeof(ipc_msg_rpc_t) == 64);

#define IPC_REMOTE_CALL_LONG 4
typedef struct ipc_msg_rpc_long {
  uint32_t origin;      // message sender
  uint32_t type;        // IPC_REMOTE_CALL
  const char *call;     // remote procedure
  uint64_t args[6];     // procedure arguments
} ipc_msg_rpc_long_t;
static_assert(sizeof(ipc_msg_rpc_long_t) == 64);

//
// API
//

int ipc_send(pid_t pid, message_t *message);
message_t *ipc_receive();
message_t *ipc_receive_nb();


#endif
