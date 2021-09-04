//
// Created by Aaron Gill-Braun on 2021-08-31.
//

#include <ipc.h>
#include <process.h>
#include <mutex.h>
#include <scheduler.h>


// send - sends message to target process
//        blocks process until message is received
int ipc_send(pid_t pid, message_t *message) {
  process_t *process = scheduler_get_process(pid);
  if (process == NULL) {
    ERRNO = ESRCH;
    return -1;
  } else if (!is_kheap_ptr(message)) {
    ERRNO = EINVAL;
    return -1;
  }

  mutex_lock(&process->ipc_mutex);
  cond_clear_signal(&process->ipc_cond_recvd);
  process->ipc_msg = message;
  cond_signal(&process->ipc_cond_avail);
  cond_wait(&process->ipc_cond_recvd);
  mutex_unlock(&process->ipc_mutex);
  return 0;
}

// receive - receives a message
//           blocks process until message is available
message_t *ipc_receive() {
  process_t *process = current_process;
  cond_wait(&process->ipc_cond_avail);
  message_t *ptr = process->ipc_msg;
  cond_signal(&process->ipc_cond_recvd);
  return ptr;
}

// receive - receives a message (non-blocking)
//           returns a recieved message if it exists
message_t *ipc_receive_nb() {
  process_t *process = current_process;
  if (cond_signaled(&process->ipc_cond_avail)) {
    message_t *ptr = process->ipc_msg;
    cond_signal(&process->ipc_cond_recvd);
    return ptr;
  }
  return NULL;
}
