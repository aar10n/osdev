//
// Created by Aaron Gill-Braun on 2025-01-12.
//

#include <kernel/futex.h>
#include <kernel/proc.h>
#include <kernel/tqueue.h>
#include <kernel/time.h>
#include <kernel/mm_types.h>
#include <kernel/atomic.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <linux/futex.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("futex: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("futex: %s: " fmt, __func__, ##__VA_ARGS__)

// compute stable wait channel from futex key
// for private futexes: combine address_space pointer and virtual address
// this creates a unique, stable value for each (space, uaddr) pair
static inline const void *futex_wchan(struct address_space *space, uintptr_t uaddr) {
  return (const void *)((uintptr_t)space ^ uaddr);
}

//

static int futex_wait(int *uaddr, int val, const struct timespec *timeout, int flags) {
  if (!is_aligned((uintptr_t)uaddr, sizeof(int))) {
    return -EINVAL;
  }

  const void *wchan = futex_wchan(curspace, (uintptr_t)uaddr);
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_FUTEX, wchan, curthread->own_waitq);
  if (waitq == NULL) {
    return -ENOMEM;
  }

  int current_val = atomic_load(uaddr);
  if (current_val != val) {
    waitq_release(&waitq);
    return -EAGAIN;  // value changed, don't sleep
  }

  // block on waitqueue
  int ret;
  if (timeout != NULL) {
    uint64_t timeout_ns = timespec_to_nanos(timeout);
    if (flags & FUTEX_CLOCK_REALTIME) {
      todo("FUTEX_CLOCK_REALTIME not yet supported");
    }
    ret = waitq_wait_timeout(waitq, "futex", timeout_ns);
  } else {
    waitq_wait(waitq, "futex");
    ret = 0;
  }

  return ret;
}

static int futex_wake(int *uaddr, int n_wake, int flags) {
  if (!is_aligned((uintptr_t)uaddr, sizeof(int))) {
    return -EINVAL;
  } else if (n_wake <= 0) {
    return 0;
  }

  const void *wchan = futex_wchan(curspace, (uintptr_t)uaddr);

  struct waitqueue *waitq = waitq_lookup(wchan);
  if (waitq == NULL) {
    return 0;  // no waiters
  }

  return waitq_broadcast_n(waitq, n_wake);
}

static int futex_requeue(int *uaddr, int n_wake, int n_requeue, int *uaddr2, int flags) {
  todo("FUTEX_REQUEUE not yet implemented");
}

static int futex_cmp_requeue(int *uaddr, int n_wake, int n_requeue, int *uaddr2, int val3, int flags) {
  todo("FUTEX_CMP_REQUEUE not yet implemented");
}

static int futex_wake_op(int *uaddr, int n_wake, int n_wake2, int *uaddr2, int val3, int flags) {
  todo("FUTEX_WAKE_OP not yet implemented");
}

static int futex_lock_pi(int *uaddr, const struct timespec *timeout, int flags) {
  todo("FUTEX_LOCK_PI (priority inheritance) not yet supported");
}

static int futex_unlock_pi(int *uaddr, int flags) {
  todo("FUTEX_UNLOCK_PI (priority inheritance) not yet supported");
}

static int futex_trylock_pi(int *uaddr, int flags) {
  todo("FUTEX_TRYLOCK_PI (priority inheritance) not yet supported");
}

static int futex_wait_bitset(int *uaddr, int val, const struct timespec *timeout, int bitset, int flags) {
  todo("FUTEX_WAIT_BITSET not yet implemented");
}

static int futex_wake_bitset(int *uaddr, int n_wake, int bitset, int flags) {
  todo("FUTEX_WAKE_BITSET not yet implemented");
}

//
// MARK: Futex API
//

void futex_wake_on_exit(int *clear_child_tid) {
  if (clear_child_tid == NULL) {
    return;
  }

  // atomically clear the tid value
  atomic_store(clear_child_tid, 0);

  // wake one waiter (for pthread_join)
  const void *wchan = futex_wchan(curspace, (uintptr_t)clear_child_tid);

  struct waitqueue *waitq = waitq_lookup(wchan);
  if (waitq != NULL) {
    waitq_signal(waitq);
  }
}

//
// MARK: Syscall
//

DEFINE_SYSCALL(futex, int, int *uaddr, int futex_op, int val, const struct timespec *timeout, int *uaddr2, int val3) {
  if (uaddr == NULL) {
    return -EFAULT;
  }

  int cmd = futex_op & FUTEX_CMD_MASK;
  int flags = futex_op & ~FUTEX_CMD_MASK;

  DPRINTF("syscall: futex: uaddr=%p op=%d val=%d timeout=%p uaddr2=%p val3=%d\n",
          uaddr, cmd, val, timeout, uaddr2, val3);

  switch (cmd) {
    case FUTEX_WAIT:
      return futex_wait(uaddr, val, timeout, flags);
    case FUTEX_WAKE:
      return futex_wake(uaddr, val, flags);
    case FUTEX_REQUEUE:
      return futex_requeue(uaddr, val, (int)(uintptr_t)timeout, uaddr2, flags);
    case FUTEX_CMP_REQUEUE:
      return futex_cmp_requeue(uaddr, val, (int)(uintptr_t)timeout, uaddr2, val3, flags);
    case FUTEX_WAKE_OP:
      return futex_wake_op(uaddr, val, (int)(uintptr_t)timeout, uaddr2, val3, flags);
    case FUTEX_LOCK_PI:
      return futex_lock_pi(uaddr, timeout, flags);
    case FUTEX_UNLOCK_PI:
      return futex_unlock_pi(uaddr, flags);
    case FUTEX_TRYLOCK_PI:
      return futex_trylock_pi(uaddr, flags);
    case FUTEX_WAIT_BITSET:
      return futex_wait_bitset(uaddr, val, timeout, val3, flags);
    case FUTEX_WAKE_BITSET:
      return futex_wake_bitset(uaddr, val, val3, flags);
    case FUTEX_WAIT_REQUEUE_PI:
    case FUTEX_CMP_REQUEUE_PI:
    case FUTEX_LOCK_PI2:
      todo("futex operation %d not yet implemented", cmd);
    default:
      EPRINTF("unknown futex operation: %d\n", cmd);
      return -ENOSYS;
  }
}
