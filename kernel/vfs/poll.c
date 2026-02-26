//
// Created by Aaron Gill-Braun on 2025-10-18.
//

#include <kernel/vfs/file.h>
#include <kernel/kevent.h>
#include <kernel/mm.h>
#include <kernel/proc.h>
#include <kernel/fs.h>
#include <kernel/alarm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <abi/epoll.h>
#include <abi/select.h>
#include <uapi/sys/eventfd.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("poll: " fmt, ##__VA_ARGS__)

//
// eventfd structure
//

typedef struct eventfd {
  uint64_t count;
  mtx_t lock;
  cond_t cond;
  int flags;
} eventfd_t;

//
// eventfd allocation and management
//

eventfd_t *eventfd_alloc(unsigned int initval, int flags) {
  eventfd_t *efd = kmallocz(sizeof(eventfd_t));
  if (!efd) {
    return NULL;
  }

  efd->count = initval;
  efd->flags = flags;
  mtx_init(&efd->lock, 0, "eventfd_lock");
  cond_init(&efd->cond, "eventfd_cond");

  return efd;
}

void eventfd_free(eventfd_t **efdp) {
  eventfd_t *efd = moveptr(*efdp);
  if (!efd) {
    return;
  }

  mtx_destroy(&efd->lock);
  cond_destroy(&efd->cond);
  kfree(efd);
}

//
// eventfd file operations
//

static int eventfd_open(file_t *file, int flags) {
  ASSERT(F_ISEVENTFD(file));
  return 0;
}

static int eventfd_close(file_t *file) {
  ASSERT(F_ISEVENTFD(file));
  return 0;
}

static void eventfd_cleanup(file_t *file) {
  ASSERT(F_ISEVENTFD(file));
  eventfd_t *efd = moveptr(file->data);
  eventfd_free(&efd);
}

static ssize_t eventfd_read(file_t *file, kio_t *kio) {
  ASSERT(F_ISEVENTFD(file));
  eventfd_t *efd = file->data;

  if (kio_remaining(kio) < sizeof(uint64_t)) {
    return -EINVAL;
  }

  mtx_lock(&efd->lock);

  while (efd->count == 0) {
    if (file->flags & O_NONBLOCK) {
      mtx_unlock(&efd->lock);
      return -EAGAIN;
    }
    cond_wait(&efd->cond, &efd->lock);
  }

  uint64_t value;
  if (efd->flags & EFD_SEMAPHORE) {
    value = 1;
    efd->count--;
  } else {
    value = efd->count;
    efd->count = 0;
  }

  cond_broadcast(&efd->cond);
  mtx_unlock(&efd->lock);

  size_t written = kio_write_in(kio, &value, sizeof(uint64_t), 0);
  if (written != sizeof(uint64_t)) {
    return -EFAULT;
  }

  return sizeof(uint64_t);
}

static ssize_t eventfd_write(file_t *file, kio_t *kio) {
  ASSERT(F_ISEVENTFD(file));
  eventfd_t *efd = file->data;

  if (kio_remaining(kio) < sizeof(uint64_t)) {
    return -EINVAL;
  }

  uint64_t value;
  size_t read = kio_read_out(&value, sizeof(uint64_t), 0, kio);
  if (read != sizeof(uint64_t)) {
    return -EFAULT;
  }

  if (value == UINT64_MAX) {
    return -EINVAL;
  }

  mtx_lock(&efd->lock);

  while (UINT64_MAX - efd->count <= value) {
    if (file->flags & O_NONBLOCK) {
      mtx_unlock(&efd->lock);
      return -EAGAIN;
    }
    cond_wait(&efd->cond, &efd->lock);
  }

  efd->count += value;
  cond_broadcast(&efd->cond);
  mtx_unlock(&efd->lock);

  return sizeof(uint64_t);
}

static const struct file_ops eventfd_file_ops = {
  .f_open = eventfd_open,
  .f_close = eventfd_close,
  .f_cleanup = eventfd_cleanup,
  .f_read = eventfd_read,
  .f_write = eventfd_write,
};

//
// epoll instance structure
//

typedef struct epoll {
  kqueue_t *kq;
  mtx_t lock;
  int flags;
} epoll_t;

//
// epoll allocation and management
//

epoll_t *epoll_alloc(void) {
  epoll_t *ep = kmallocz(sizeof(epoll_t));
  if (!ep) {
    return NULL;
  }

  ep->kq = kqueue_alloc();
  if (!ep->kq) {
    kfree(ep);
    return NULL;
  }

  mtx_init(&ep->lock, 0, "epoll_lock");
  ep->flags = 0;

  return ep;
}

void epoll_free(epoll_t **epp) {
  epoll_t *ep = moveptr(*epp);
  if (!ep) {
    return;
  }

  if (ep->kq) {
    kqueue_drain(ep->kq);
    kqueue_free(&ep->kq);
  }

  mtx_destroy(&ep->lock);
  kfree(ep);
}

//
// epoll file operations
//

static int epoll_open(file_t *file, int flags) {
  ASSERT(F_ISEPOLL(file));
  return 0;
}

static int epoll_close(file_t *file) {
  ASSERT(F_ISEPOLL(file));
  return 0;
}

static void epoll_cleanup(file_t *file) {
  ASSERT(F_ISEPOLL(file));
  epoll_t *ep = moveptr(file->data);
  epoll_free(&ep);
}

static const struct file_ops epoll_file_ops = {
  .f_open = epoll_open,
  .f_close = epoll_close,
  .f_cleanup = epoll_cleanup,
};

//
// epoll system calls
//

int poll_epoll_create1(int flags) {
  // validate flags - only EPOLL_CLOEXEC is allowed
  if (flags & ~EPOLL_CLOEXEC) {
    return -EINVAL;
  }

  proc_t *proc = curproc;

  // allocate file descriptor
  int fd = fs_proc_alloc_fd(proc);
  if (fd < 0) {
    return -EMFILE;
  }

  // allocate epoll instance
  epoll_t *ep = epoll_alloc();
  if (!ep) {
    fs_proc_free_fd(proc, fd);
    return -ENOMEM;
  }

  ep->flags = flags;

  // create file with FT_EPOLL type
  file_t *file = f_alloc(FT_EPOLL, 0, ep, (struct file_ops *)&epoll_file_ops);
  if (!file) {
    epoll_free(&ep);
    fs_proc_free_fd(proc, fd);
    return -ENOMEM;
  }

  // open the file
  f_lock(file);
  int res = f_open(file, 0);
  f_unlock(file);
  if (res < 0) {
    f_putref(&file);
    fs_proc_free_fd(proc, fd);
    return res;
  }

  // create fd entry
  int fdeflags = (flags & EPOLL_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_entry_t *fde = fd_entry_alloc(fd, fdeflags, cstr_make("epoll"), moveref(file));
  if (!fde) {
    fs_proc_free_fd(proc, fd);
    return -ENOMEM;
  }

  // add entry to file table
  fs_proc_add_fdentry(proc, moveref(fde));

  DPRINTF("epoll_create1: created epoll fd %d\n", fd);
  return fd;
}

//
// eventfd system calls
//

int poll_eventfd2(unsigned int count, int flags) {
  if (flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)) {
    return -EINVAL;
  }

  proc_t *proc = curproc;

  int fd = fs_proc_alloc_fd(proc);
  if (fd < 0) {
    return -EMFILE;
  }

  eventfd_t *efd = eventfd_alloc(count, flags);
  if (!efd) {
    fs_proc_free_fd(proc, fd);
    return -ENOMEM;
  }

  int file_flags = O_RDWR;
  if (flags & EFD_NONBLOCK) {
    file_flags |= O_NONBLOCK;
  }

  file_t *file = f_alloc(FT_EVENTFD, file_flags, efd, (struct file_ops *)&eventfd_file_ops);
  if (!file) {
    eventfd_free(&efd);
    fs_proc_free_fd(proc, fd);
    return -ENOMEM;
  }

  // open the file
  f_lock(file);
  int res = f_open(file, file_flags);
  f_unlock(file);
  if (res < 0) {
    f_putref(&file);
    fs_proc_free_fd(proc, fd);
    return res;
  }

  int fdeflags = (flags & EFD_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_entry_t *fde = fd_entry_alloc(fd, fdeflags, cstr_make("eventfd"), moveref(file));
  if (!fde) {
    fs_proc_free_fd(proc, fd);
    return -ENOMEM;
  }

  fs_proc_add_fdentry(proc, moveref(fde));

  DPRINTF("eventfd2: created eventfd fd %d with count=%u flags=0x%x\n", fd, count, flags);
  return fd;
}

int poll_eventfd(unsigned int count) {
  return poll_eventfd2(count, 0);
}

//
// select system call
//

int poll_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  DPRINTF("select: nfds=%d readfds=%p writefds=%p exceptfds=%p timeout=%p\n",
          nfds, readfds, writefds, exceptfds, timeout);

  if (nfds < 0 || nfds > FD_SETSIZE) {
    return -EINVAL;
  }

  if ((readfds && vm_validate_ptr((uintptr_t)readfds, true) < 0) ||
      (writefds && vm_validate_ptr((uintptr_t)writefds, true) < 0) ||
      (exceptfds && vm_validate_ptr((uintptr_t)exceptfds, true) < 0) ||
      (timeout && vm_validate_ptr((uintptr_t)timeout, true) < 0)) {
    return -EFAULT;
  }

  if (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000)) {
    return -EINVAL;
  }

  // count how many fds we need to poll
  int poll_count = 0;
  for (int fd = 0; fd < nfds; fd++) {
    bool want_read = readfds && FD_ISSET(fd, readfds);
    bool want_write = writefds && FD_ISSET(fd, writefds);
    bool want_except = exceptfds && FD_ISSET(fd, exceptfds);
    if (want_read || want_write || want_except) {
      poll_count++;
    }
  }

  if (poll_count == 0) {
    if (timeout) {
      uint64_t sleep_ns = (uint64_t)timeout->tv_sec * NS_PER_SEC + (uint64_t)timeout->tv_usec * 1000;
      if (sleep_ns > 0) {
        alarm_sleep_ns(sleep_ns);
      }
    }
    return 0;
  }

  struct pollfd *pollfds = kmallocz(sizeof(struct pollfd) * poll_count);
  if (!pollfds) {
    return -ENOMEM;
  }

  int *fd_map = kmallocz(sizeof(int) * poll_count);
  if (!fd_map) {
    kfree(pollfds);
    return -ENOMEM;
  }

  int idx = 0;
  for (int fd = 0; fd < nfds; fd++) {
    bool want_read = readfds && FD_ISSET(fd, readfds);
    bool want_write = writefds && FD_ISSET(fd, writefds);
    bool want_except = exceptfds && FD_ISSET(fd, exceptfds);
    if (want_read || want_write || want_except) {
      pollfds[idx].fd = fd;
      pollfds[idx].events = 0;
      pollfds[idx].revents = 0;
      if (want_read) {
        pollfds[idx].events |= POLLIN;
      }
      if (want_write) {
        pollfds[idx].events |= POLLOUT;
      }
      if (want_except) {
        pollfds[idx].events |= POLLPRI;
      }
      fd_map[idx] = fd;
      idx++;
    }
  }

  struct timespec ts;
  struct timespec *tsp = NULL;
  if (timeout) {
    ts.tv_sec = timeout->tv_sec;
    ts.tv_nsec = timeout->tv_usec * 1000;
    tsp = &ts;
  }

  int res = fs_poll(pollfds, poll_count, tsp);

  if (readfds) FD_ZERO(readfds);
  if (writefds) FD_ZERO(writefds);
  if (exceptfds) FD_ZERO(exceptfds);

  if (res > 0) {
    int ready_count = 0;
    for (int i = 0; i < poll_count; i++) {
      int fd = fd_map[i];
      short revents = pollfds[i].revents;

      if (revents & (POLLIN | POLLHUP | POLLERR)) {
        if (readfds) {
          FD_SET(fd, readfds);
          ready_count++;
        }
      }
      if (revents & (POLLOUT | POLLERR)) {
        if (writefds) {
          FD_SET(fd, writefds);
          ready_count++;
        }
      }
      if (revents & POLLPRI) {
        if (exceptfds) {
          FD_SET(fd, exceptfds);
          ready_count++;
        }
      }
    }
    res = ready_count;
  }

  kfree(pollfds);
  kfree(fd_map);
  return res;
}

//
// System call exports
//

SYSCALL_ALIAS(epoll_create1, poll_epoll_create1);
SYSCALL_ALIAS(eventfd, poll_eventfd);
SYSCALL_ALIAS(eventfd2, poll_eventfd2);
SYSCALL_ALIAS(select, poll_select);
