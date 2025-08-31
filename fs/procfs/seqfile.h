//
// Created by Aaron Gill-Braun on 2025-08-25.
//

#ifndef FS_PROCFS_SEQFILE_H
#define FS_PROCFS_SEQFILE_H

#include <kernel/base.h>
#include <kernel/kio.h>
#include <stdarg.h>

// https://docs.kernel.org/filesystems/seq_file.html

typedef struct procfs_object procfs_object_t;
typedef struct procfs_handle procfs_handle_t;
struct seq_ops;

#define SEQ_FILE_BUFSIZE_MIN 4096
#define SEQ_FILE_BUFSIZE_MAX (size_t)(256 * 1024)  // 256KB max buffer

/*
 * A seqfile is a file-like object that provides a way for the kernel to buffer
 * data from a dynamic iterator and present it as a file.
 */
typedef struct seqfile {
  void *buf;            // buffer for output
  size_t bufsize;       // total buffer size
  size_t count;         // amount of data in buffer
  size_t from;          // current read offset in buffer
  off_t index;          // iterator index
  bool full;            // buffer is full (need larger buffer)

  struct seq_ops *ops;  // iterator operations
  void *data;           // private data for iterator
} seqfile_t;

/*
 * Operations for a seqfile iterator.
 * Core iteration functions are required.
 */
struct seq_ops {
  // start iteration - return iterator state for position pos or NULL for end.
  void *(*start)(struct seqfile *sf, off_t *pos);
  // stop iteration - free iterator state
  void (*stop)(struct seqfile *sf, void *v);
  // advance to next item - return iterator state for next position or NULL for end.
  void *(*next)(struct seqfile *sf, void *v, off_t *pos);
  // show current item - output data for iterator state to seqfile
  int (*show)(struct seqfile *sf, void *v);

  // write data to the underlying object (optional)
  // this function should not modify the seqfile state or write to it and
  // instead should operate on the underlying object it is wrapping.
  ssize_t (*write)(seqfile_t *sf, off_t off, kio_t *kio);

  // cleanup seqfile when underlying file is closed (optional)
  void (*cleanup)(seqfile_t *sf);
};

typedef int (*simple_show_t)(seqfile_t *sf, void *data);
typedef ssize_t (*simple_write_t)(seqfile_t *sf, off_t off, kio_t *kio, void *data);

extern struct seq_ops seq_simple_ops;
extern struct procfs_ops seq_procfs_ops;

// seqfile procfs operations
int seq_proc_open(procfs_object_t *obj, int flags, void **handle_data);
int seq_proc_close(procfs_handle_t *h);
ssize_t seq_proc_read(procfs_handle_t *h, off_t off, kio_t *kio);
ssize_t seq_proc_write(procfs_handle_t *h, off_t off, kio_t *kio);
off_t seq_proc_lseek(procfs_handle_t *h, off_t offset, int whence);
void seq_proc_cleanup(procfs_object_t *obj);

struct seq_ctor *seq_ctor_create(struct seq_ops *ops, void *data);
struct seq_ctor *simple_ctor_create(simple_show_t show, simple_write_t write, void *data);
void seq_ctor_destroy(struct seq_ctor **ctorp);

// output functions
int seq_putc(seqfile_t *sf, char c);
int seq_puts(seqfile_t *sf, const char *s);
int seq_write(seqfile_t *sf, const void *data, size_t len);
int seq_printf(seqfile_t *sf, const char *fmt, ...) _printf_like(2, 3);
int seq_vprintf(seqfile_t *sf, const char *fmt, va_list args);
int seq_escape(seqfile_t *sf, const char *s, const char *esc);
int seq_pad(seqfile_t *sf, char c);

#endif
