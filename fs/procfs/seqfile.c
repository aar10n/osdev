//
// Created by Aaron Gill-Braun on 2025-08-25.
//

#define PROCFS_INTERNAL
#include "seqfile.h"
#include "procfs.h"

#include <kernel/mm.h>
#include <kernel/mm/vmalloc.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/vfs/file.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("seqfile: %s: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("seqfile: %s: " fmt, __func__, ##__VA_ARGS__)

// seq_ctor is used to pass constructor arguments to the open function it is
// allocated and attached to all seqfile procfs objects when registered.
struct seq_ctor {
  struct seq_ops *ops;
  void *data;
  bool is_simple;
  simple_show_t show;
  simple_write_t write;
};

// seqfile_simple is used to hold data for simple seqfiles. it is allocated
// and attached to the seqfile when opened, and freed when the file is closed.
struct seqfile_simple {
  simple_show_t show;
  simple_write_t write;
  void *data;
};

//
// MARK: simple seqfile operations
//

static void *seq_simple_start(seqfile_t *sf, off_t *pos) {
  off_t index = *pos;
  if (index == 0) {
    *pos = 1; // next position
    return sf->data; // single item
  }
  return NULL; // EOF
}

static void seq_simple_stop(seqfile_t *sf, void *v) {}

static void *seq_simple_next(seqfile_t *sf, void *v, off_t *pos) {
  // only one item
  return NULL; // EOF
}

static int seq_simple_show(seqfile_t *sf, void *v) {
  struct seqfile_simple *sfs = sf->data;
  ASSERT(sfs != NULL);
  ASSERT(sfs->show != NULL);
  return sfs->show(sf, sfs->data);
}

static ssize_t seq_simple_write(seqfile_t *sf, off_t off, kio_t *kio) {
  struct seqfile_simple *sfs = sf->data;
  ASSERT(sfs != NULL);
  if (sfs->write == NULL) {
    return -ENOTSUP;
  }
  return sfs->write(sf, off, kio, sfs->data);
}

static void seq_simple_cleanup(seqfile_t *sf) {
  struct seqfile_simple *sfs = sf->data;
  kfree(sfs);
  sf->data = NULL;
}

struct seq_ops seq_simple_ops = {
  .start = seq_simple_start,
  .stop = seq_simple_stop,
  .next = seq_simple_next,
  .show = seq_simple_show,
  .write = seq_simple_write,
  .cleanup = seq_simple_cleanup,
};

//
// MARK: core seqfile functions
//

static int seq_alloc_buf(seqfile_t *sf, size_t size) {
  ASSERT(sf != NULL);
  if (size < SEQ_FILE_BUFSIZE_MIN)
    size = SEQ_FILE_BUFSIZE_MIN;
  if (size > SEQ_FILE_BUFSIZE_MAX)
    size = SEQ_FILE_BUFSIZE_MAX;

  // resize existing buffer if exists
  if (sf->buf) {
    if (size <= sf->bufsize)
      return 0; // already large enough

    uintptr_t new_buf;
    int res = vmap_resize((uintptr_t ) sf->buf, sf->bufsize, size, /*allow_move=*/true, &new_buf);
    if (res < 0) {
      EPRINTF("seq_alloc_buf: vmap_resize failed: {:err}\n", res);
      return res;
    }

    sf->buf = (void *)new_buf;
    sf->bufsize = size;
    sf->full = false;
    return 0;
  }

  // allocate new buffer
  sf->buf = (void *) vmap_anon(size, 0, size, VM_RDWR, "seq_file");
  if (!sf->buf)
    return -ENOMEM;

  sf->bufsize = size;
  sf->count = 0;
  sf->full = false;
  return 0;
}

static ssize_t seq_read(seqfile_t *sf, kio_t *kio, off_t *ppos) {
  ASSERT(sf != NULL);
  ASSERT(kio != NULL);
  ASSERT(ppos != NULL);

  size_t copied = 0;
  off_t pos = *ppos;

  // if we have buffered data from a previous read, return it
  if (sf->count) {
    size_t n = sf->count - sf->from;
    if (n > kio_remaining(kio))
      n = kio_remaining(kio);

    size_t written = kio_write_in(kio, (char *)sf->buf + sf->from, n, 0);
    sf->from += written;
    copied += written;

    if (sf->from >= sf->count) {
      // buffer exhausted, reset
      sf->from = 0;
      sf->count = 0;
    }

    if (!kio_remaining(kio) || written < n)
      goto done;
  }

  // need to refill buffer
  sf->from = 0;
  sf->count = 0;
  sf->index = pos;

restart:;
  // start iteration
  void *p = sf->ops->start(sf, &sf->index);
  while (p) {
    int err = sf->ops->show(sf, p);

    if (err < 0) {
      sf->ops->stop(sf, p);
      sf->count = 0;
      return err;
    }

    if (sf->full) {
      // buffer is full, need to grow it
      sf->ops->stop(sf, p);

      size_t new_size = sf->bufsize * 2;
      if (seq_alloc_buf(sf, new_size) < 0) {
        return -ENOMEM;
      }

      sf->count = 0;
      sf->from = 0;
      sf->index = pos;

      // restart with larger buffer
      goto restart;
    }

    // move to next item
    p = sf->ops->next(sf, p, &sf->index);
  }

  sf->ops->stop(sf, p);

  // copy data to user
  if (sf->count) {
    size_t n = sf->count;
    if (n > kio_remaining(kio))
      n = kio_remaining(kio);

    size_t written = kio_write_in(kio, sf->buf, n, 0);
    sf->from = written;
    copied += written;
  }

done:
  *ppos = (off_t)(pos + copied);
  return copied ? (ssize_t)copied : 0;
}

static off_t seq_lseek(seqfile_t *sf, off_t offset, int whence) {
  ASSERT(sf != NULL);

  if (whence == SEEK_SET) {
    if (offset < 0)
      return -EINVAL;

    sf->count = 0;
    sf->from = 0;
    sf->index = 0;
    return offset;
  } else {
    return -EINVAL; // not supported for seqfile
  }
}

//
// MARK: seqfile procfs operations
//

struct procfs_ops seq_procfs_ops = {
  .proc_open = seq_proc_open,
  .proc_close = seq_proc_close,
  .proc_read = seq_proc_read,
  .proc_lseek = seq_proc_lseek,
  .proc_write = seq_proc_write,
  .proc_cleanup = seq_proc_cleanup,
};

int seq_proc_open(procfs_object_t *obj, int flags, void **handle_data) {
  ASSERT(!obj->is_dir && !obj->is_static);
  struct seq_ctor *ctor = obj->data;
  struct seq_ops *ops = ctor->ops;
  void *data = ctor->data;
  ASSERT(ops != NULL);
  ASSERT(ops->start != NULL);
  ASSERT(ops->stop != NULL);
  ASSERT(ops->next != NULL);
  ASSERT(ops->show != NULL);

  seqfile_t *sf = kmallocz(sizeof(seqfile_t));
  ASSERT(sf != NULL);
  sf->ops = ops;
  sf->data = data;

  // allocate initial buffer
  if (seq_alloc_buf(sf, SEQ_FILE_BUFSIZE_MIN) < 0) {
    EPRINTF("failed to allocate buffer\n");
    kfree(sf);
    return -ENOMEM;
  }

  // allocate a seqfile_simple if its a simple ctor
  if (ctor->is_simple) {
    struct seqfile_simple *sfs = kmallocz(sizeof(struct seqfile_simple));
    ASSERT(sfs != NULL);
    sfs->show = ctor->show;
    sfs->write = ctor->write;
    sfs->data = data;
    sf->data = sfs;
  }

  *handle_data = sf;
  return 0;
}

int seq_proc_close(procfs_handle_t *h) {
  procfs_object_t *obj = h->obj;
  ASSERT(!obj->is_dir && !obj->is_static);
  seqfile_t *sf = moveptr(h->data);

  if (sf->buf)
    vmap_free((uintptr_t) sf->buf, sf->bufsize);

  if (sf->ops->cleanup) {
    sf->ops->cleanup(sf);
  }

  kfree(sf);
  return 0;
}

void seq_proc_cleanup(procfs_object_t *obj) {
  ASSERT(!obj->is_dir && !obj->is_static);
  struct seq_ctor *ctor = moveptr(obj->data);
  void *data = moveptr(ctor->data);
  seq_ctor_destroy(&ctor);
}

ssize_t seq_proc_read(procfs_handle_t *h, off_t off, kio_t *kio) {
  procfs_object_t *obj = h->obj;
  ASSERT(!obj->is_dir && !obj->is_static);
  seqfile_t *sf = h->data;

  off_t pos = off;
  return seq_read(sf, kio, &pos);
}

ssize_t seq_proc_write(procfs_handle_t *h, off_t off, kio_t *kio) {
  procfs_object_t *obj = h->obj;
  ASSERT(!obj->is_dir && !obj->is_static);
  seqfile_t *sf = h->data;

  if (sf->ops->write) {
    return sf->ops->write(sf, off, kio);
  }
  return -ENOTSUP;
}

off_t seq_proc_lseek(procfs_handle_t *h, off_t offset, int whence) {
  procfs_object_t *obj = h->obj;
  ASSERT(!obj->is_dir && !obj->is_static);
  seqfile_t *sf = h->data;

  return seq_lseek(sf, offset, whence);
}

//

struct seq_ctor *seq_ctor_create(struct seq_ops *ops, void *data) {
  ASSERT(ops != NULL);
  ASSERT(ops->start != NULL);
  ASSERT(ops->stop != NULL);
  ASSERT(ops->next != NULL);
  ASSERT(ops->show != NULL);

  struct seq_ctor *ctor = kmallocz(sizeof(struct seq_ctor));
  ASSERT(ctor != NULL);

  ctor->ops = ops;
  ctor->data = data;
  ctor->is_simple = false;
  return ctor;
}

struct seq_ctor *simple_ctor_create(simple_show_t show, simple_write_t write, void *data) {
  ASSERT(show != NULL);

  struct seqfile_simple *sfs = kmallocz(sizeof(struct seqfile_simple));
  ASSERT(sfs != NULL);
  sfs->show = show;
  sfs->data = data;

  struct seq_ctor *ctor = kmallocz(sizeof(struct seq_ctor));
  ASSERT(ctor != NULL);

  ctor->ops = &seq_simple_ops;
  ctor->data = sfs;
  ctor->is_simple = true;
  ctor->show = show;
  ctor->write = write;
  return ctor;
}

void seq_ctor_destroy(struct seq_ctor **ctorp) {
  struct seq_ctor *ctor = moveptr(*ctorp);
  if (ctor) {
    // data should be freed by caller before
    ASSERT(ctor->data == NULL);
    kfree(ctor);
  }
}

//
// MARK: seqfile output functions
//

int seq_putc(seqfile_t *sf, char c) {
  ASSERT(sf != NULL);
  
  if (sf->count >= sf->bufsize) {
    sf->full = true;
    return -1;
  }
  
  ((char *)sf->buf)[sf->count++] = c;
  return 0;
}

int seq_puts(seqfile_t *sf, const char *s) {
  ASSERT(sf != NULL);
  ASSERT(s != NULL);
  
  size_t len = strlen(s);
  if (sf->count + len >= sf->bufsize) {
    sf->full = true;
    return -1;
  }
  
  memcpy((char *)sf->buf + sf->count, s, len);
  sf->count += len;
  return 0;
}

int seq_write(seqfile_t *sf, const void *data, size_t len) {
  ASSERT(sf != NULL);
  ASSERT(data != NULL || len == 0);
  
  if (sf->count + len >= sf->bufsize) {
    sf->full = true;
    return -1;
  }
  
  memcpy((char *)sf->buf + sf->count, data, len);
  sf->count += len;
  return 0;
}

int seq_vprintf(seqfile_t *sf, const char *fmt, va_list args) {
  ASSERT(sf != NULL);
  ASSERT(fmt != NULL);
  
  size_t avail = sf->bufsize - sf->count;
  if (avail <= 1) {
    sf->full = true;
    return -1;
  }
  
  int len = kvsnprintf((char *)sf->buf + sf->count, avail, fmt, args);
  if (len >= avail) {
    sf->full = true;
    return -1;
  }
  
  sf->count += len;
  return 0;
}

int seq_printf(seqfile_t *sf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = seq_vprintf(sf, fmt, args);
  va_end(args);
  return ret;
}

int seq_escape(seqfile_t *sf, const char *s, const char *esc) {
  ASSERT(sf != NULL);
  ASSERT(s != NULL);
  
  char buf[256];
  size_t bufpos = 0;
  
  while (*s) {
    if (esc && strchr(esc, *s)) {
      // escape this character
      if (bufpos >= sizeof(buf) - 2) {
        // flush buffer
        if (seq_write(sf, buf, bufpos) < 0)
          return -1;
        bufpos = 0;
      }
      buf[bufpos++] = '\\';
      buf[bufpos++] = *s;
    } else if (*s == '\\') {
      // always escape backslash
      if (bufpos >= sizeof(buf) - 2) {
        if (seq_write(sf, buf, bufpos) < 0)
          return -1;
        bufpos = 0;
      }
      buf[bufpos++] = '\\';
      buf[bufpos++] = '\\';
    } else if (*s < 0x20 || *s >= 0x7f) {
      // escape non-printable characters
      if (bufpos >= sizeof(buf) - 4) {
        if (seq_write(sf, buf, bufpos) < 0)
          return -1;
        bufpos = 0;
      }
      bufpos += ksnprintf(buf + bufpos, 5, "\\x%02x", (unsigned char)*s);
    } else {
      // regular character
      if (bufpos >= sizeof(buf) - 1) {
        if (seq_write(sf, buf, bufpos) < 0)
          return -1;
        bufpos = 0;
      }
      buf[bufpos++] = *s;
    }
    s++;
  }
  
  // flush remaining buffer
  if (bufpos > 0)
    return seq_write(sf, buf, bufpos);
  
  return 0;
}

int seq_pad(seqfile_t *sf, char c) {
  ASSERT(sf != NULL);
  
  // find last newline in buffer
  int last_nl = -1;
  for (int i = sf->count - 1; i >= 0; i--) {
    if (((char *)sf->buf)[i] == '\n') {
      last_nl = i;
      break;
    }
  }
  
  // calculate current column position
  int col = sf->count - last_nl - 1;
  
  // pad to next tab stop (8 columns)
  int target = (col + 7) & ~7;
  int pad = target - col;
  
  while (pad-- > 0) {
    if (seq_putc(sf, c) < 0)
      return -1;
  }
  
  return 0;
}
