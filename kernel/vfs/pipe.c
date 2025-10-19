//
// Created by Aaron Gill-Braun on 2025-08-03.
//

#include <kernel/vfs/pipe.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/vfs.h>

#include <kernel/clock.h>
#include <kernel/kevent.h>
#include <kernel/proc.h>
#include <kernel/mm.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/errno.h>
#include <kernel/string.h>

#include <abi/fcntl.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("pipe: " fmt, ##__VA_ARGS__)
//#define DPRINTF(fmt, ...)


// pipe file operations
struct file_ops pipe_file_ops = {
  .f_open = pipe_f_open,
  .f_close = pipe_f_close,
  .f_allocate = NULL,
  .f_getpage = NULL,
  .f_read = pipe_f_read,
  .f_write = pipe_f_write,
  .f_ioctl = NULL,
  .f_stat = pipe_f_stat,
  .f_kqevent = pipe_f_kqevent,
  .f_cleanup = pipe_f_cleanup,
};

//
// pipe allocation and cleanup
//

__ref pipe_t *pipe_alloc(size_t buffer_size) {
  uintptr_t buffer = vmap_anon(buffer_size, 0, buffer_size, VM_RDWR, "pipe_buffer");
  if (!buffer) {
    return NULL;
  }
  DPRINTF("allocated pipe buffer at %p with size %zu\n", (void *)buffer, buffer_size);

  pipe_t *pipe = kmallocz(sizeof(pipe_t));
  pipe->buffer = (void *) buffer;
  pipe->buffer_size = buffer_size;
  pipe->ctime = clock_nano_time();

  ref_init(&pipe->refcount);
  mtx_init(&pipe->lock, 0, "pipe_lock");
  cond_init(&pipe->read_cond, "pipe_read");
  cond_init(&pipe->write_cond, "pipe_write");
  knlist_init(&pipe->knlist, &pipe->lock.lo);

  DPRINTF("allocated pipe %p with buffer at %p size %zu\n", pipe, pipe->buffer, buffer_size);
  return pipe;
}

void _pipe_cleanup(__move pipe_t **piperef) {
  pipe_t *pipe = moveref(*piperef);
  ASSERT(pipe != NULL);
  ASSERT(ref_count(&pipe->refcount) == 0);
  
  DPRINTF("!!! cleaning up pipe %p, buffer=%p !!!\n", pipe, pipe->buffer);
  
  if (pipe->buffer) {
    DPRINTF("freeing pipe buffer at %p\n", pipe->buffer);
    vmap_free((uintptr_t)pipe->buffer, pipe->buffer_size);
  }
  
  knlist_destroy(&pipe->knlist);
  mtx_destroy(&pipe->lock);
  cond_destroy(&pipe->read_cond);
  cond_destroy(&pipe->write_cond);

  kfree(pipe);
}

//
// pipe file operations
//

int pipe_f_open(file_t *file, int flags) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = (pipe_t *)file->data;
  
  mtx_lock(&pipe->lock);
  
  int accmode = file->flags & O_ACCMODE;
  if (accmode == O_WRONLY) {
    pipe->writers++;
    // wake up any waiting readers
    cond_broadcast(&pipe->read_cond);
  } else {
    pipe->readers++;
    // wake up any waiting writers
    cond_broadcast(&pipe->write_cond);
  }
  
  mtx_unlock(&pipe->lock);
  
  DPRINTF("opened pipe %p (%s end), readers=%u writers=%u\n",
          pipe, (accmode == O_WRONLY) ? "write" : "read", pipe->readers, pipe->writers);
  
  return 0;
}

int pipe_f_close(file_t *file) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = (pipe_t *)file->data;
  
  mtx_lock(&pipe->lock);
  
  int accmode = file->flags & O_ACCMODE;
  if (accmode == O_WRONLY) {
    ASSERT(pipe->writers > 0);
    pipe->writers--;
    if (pipe->writers == 0) {
      pipe->flags |= PIPE_WRITE_CLOSED;
      // wake up any waiting readers
      cond_broadcast(&pipe->read_cond);
      // notify kqueue watchers of EOF
      knlist_activate_notes(&pipe->knlist, 0);
    }
  } else {
    ASSERT(pipe->readers > 0);
    pipe->readers--;
    if (pipe->readers == 0) {
      pipe->flags |= PIPE_READ_CLOSED;
      // wake up any waiting writers
      cond_broadcast(&pipe->write_cond);
      // notify kqueue watchers of broken pipe
      knlist_activate_notes(&pipe->knlist, 0);
    }
  }

  mtx_unlock(&pipe->lock);
  
  DPRINTF("closed pipe %p (%s end), readers=%u writers=%u\n",
          pipe, (accmode == O_WRONLY) ? "write" : "read", pipe->readers, pipe->writers);
  
  return 0;
}

ssize_t pipe_f_read(file_t *file, kio_t *kio) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = (pipe_t *)file->data;
  
  // check if we have read permission
  int accmode = file->flags & O_ACCMODE;
  if (accmode != O_RDONLY && accmode != O_RDWR) {
    DPRINTF("pipe_f_read: EBADF - file->flags=0x%x, accmode=0x%x, need O_RDONLY(0x%x) or O_RDWR(0x%x)\n", 
            file->flags, accmode, O_RDONLY, O_RDWR);
    return -EBADF;
  }
  
  size_t total_read = 0;
  size_t to_read = kio_remaining(kio);
  
  mtx_lock(&pipe->lock);
  
  while (to_read > 0) {
    // wait for data or pipe closure
    while (pipe->count == 0) {
      // check if all writers have closed
      if (pipe->flags & PIPE_WRITE_CLOSED) {
        mtx_unlock(&pipe->lock);
        return (ssize_t) total_read;
      }

      if (file->flags & O_NONBLOCK) {
        if (total_read > 0) {
          break;
        }
        mtx_unlock(&pipe->lock);
        return -EAGAIN;
      }
      
      // wait for data
      cond_wait(&pipe->read_cond, &pipe->lock);
    }
    
    if (pipe->count == 0) {
      break; // no more data available
    }
    
    // calculate how much we can read
    size_t available = pipe->count;
    size_t chunk = min(to_read, available);
    
    // handle wrap-around
    if (pipe->read_pos + chunk > pipe->buffer_size) {
      // read in two parts
      size_t first_part = pipe->buffer_size - pipe->read_pos;
      size_t second_part = chunk - first_part;

      kio_write_in(kio, (uint8_t *)pipe->buffer + pipe->read_pos, first_part, 0);
      kio_write_in(kio, pipe->buffer, second_part, 0);
      
      pipe->read_pos = second_part;
    } else {
      // read in one part
      kio_write_in(kio, (uint8_t *)pipe->buffer + pipe->read_pos, chunk, 0);
      pipe->read_pos += chunk;
      
      if (pipe->read_pos == pipe->buffer_size) {
        pipe->read_pos = 0;
      }
    }
    
    pipe->count -= chunk;
    total_read += chunk;
    to_read -= chunk;

    // wake up waiting writers
    cond_broadcast(&pipe->write_cond);
    // notify kqueue watchers that space is available
    knlist_activate_notes(&pipe->knlist, 0);
  }

  mtx_unlock(&pipe->lock);
  
  return (ssize_t) total_read;
}

ssize_t pipe_f_write(file_t *file, kio_t *kio) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = (pipe_t *)file->data;
  
  // check if we have write permission
  int accmode = file->flags & O_ACCMODE;
  if (accmode != O_WRONLY && accmode != O_RDWR) {
    return -EBADF;
  }
  
  size_t total_written = 0;
  size_t to_write = kio_remaining(kio);
  
  mtx_lock(&pipe->lock);
  
  // check if all readers have closed
  if (pipe->flags & PIPE_READ_CLOSED) {
    mtx_unlock(&pipe->lock);
    // send SIGPIPE to the process
    proc_signal(curproc, &(siginfo_t){
      .si_signo = SIGPIPE,
      .si_code = SI_USER,
      .si_pid = curproc->pid,
    });
    return -EPIPE;
  }
  
  while (to_write > 0) {
    // wait for space
    while (pipe->count == pipe->buffer_size) {
      // check if all readers have closed
      if (pipe->flags & PIPE_READ_CLOSED) {
        mtx_unlock(&pipe->lock);
        // send SIGPIPE to the process
        proc_signal(curproc, &(siginfo_t){
          .si_signo = SIGPIPE,
          .si_code = SI_USER,
          .si_pid = curproc->pid,
        });
        return -EPIPE;
      }
      
      // non-blocking mode
      if (file->flags & O_NONBLOCK) {
        if (total_written > 0) {
          break; // return what we wrote
        }
        mtx_unlock(&pipe->lock);
        return -EAGAIN;
      }
      
      // wait for space
      cond_wait(&pipe->write_cond, &pipe->lock);
    }
    
    if (pipe->count == pipe->buffer_size) {
      break; // no more space available
    }

    size_t available = pipe->buffer_size - pipe->count;
    size_t chunk = min(to_write, available);
    
    // handle wrap-around
    if (pipe->write_pos + chunk > pipe->buffer_size) {
      // write in two parts
      size_t first_part = pipe->buffer_size - pipe->write_pos;
      size_t second_part = chunk - first_part;
      
      kio_read_out((uint8_t *)pipe->buffer + pipe->write_pos, first_part, 0, kio);
      kio_read_out(pipe->buffer, second_part, 0, kio);
      
      pipe->write_pos = second_part;
    } else {
      // write in one part
      kio_read_out((uint8_t *)pipe->buffer + pipe->write_pos, chunk, 0, kio);
      pipe->write_pos += chunk;
      
      if (pipe->write_pos == pipe->buffer_size) {
        pipe->write_pos = 0;
      }
    }
    
    pipe->count += chunk;
    total_written += chunk;
    to_write -= chunk;

    // wake up waiting readers
    cond_broadcast(&pipe->read_cond);
    // notify kqueue watchers that data is available
    knlist_activate_notes(&pipe->knlist, 0);
  }

  mtx_unlock(&pipe->lock);
  
  return (ssize_t) total_written;
}

int pipe_f_stat(file_t *file, struct stat *statbuf) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = file->data;
  
  memset(statbuf, 0, sizeof(struct stat));

  statbuf->st_ino = (ino_t)(uintptr_t)pipe; // use pipe address as inode
  statbuf->st_mode = S_IFIFO | 0666; // pipe with rw permissions
  statbuf->st_blksize = PAGE_SIZE;

  statbuf->st_ctim = pipe->ctime;
  statbuf->st_mtim = pipe->ctime;
  statbuf->st_atim = pipe->ctime;
  return 0;
}

int pipe_f_kqevent(file_t *file, knote_t *kn) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = (pipe_t *)file->data;
  int ret = 0;

  mtx_lock(&pipe->lock);
  switch (kn->event.filter) {
    case EVFILT_READ: {
      int accmode = file->flags & O_ACCMODE;
      if (accmode == O_RDONLY || accmode == O_RDWR) {
        // check if data is available
        if (pipe->count > 0) {
          kn->event.udata = (void *) pipe->count;
          ret = 1;
        } else if (pipe->flags & PIPE_WRITE_CLOSED) {
          // EOF condition
          kn->flags |= EV_EOF;
          ret = 1;
        }
      }
      break;
    }
    case EVFILT_WRITE: {
      int accmode = file->flags & O_ACCMODE;
      if (accmode == O_WRONLY || accmode == O_RDWR) {
        // check if space is available
        size_t space = pipe->buffer_size - pipe->count;
        if (space > 0) {
          kn->event.udata = (void *)space;
          ret = 1;
        } else if (pipe->flags & PIPE_READ_CLOSED) {
          // broken pipe
          kn->flags |= EV_EOF;
          ret = 1;
        }
      }
      break;
    }
    default:
      ret = -EINVAL;
      break;
  }
  
  mtx_unlock(&pipe->lock);
  return ret;
}

void pipe_f_cleanup(file_t *file) {
  ASSERT(F_ISPIPE(file));
  pipe_t *pipe = moveref(file->data);
  pipe_putref(&pipe);
}
