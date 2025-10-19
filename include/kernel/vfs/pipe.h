//
// Created by Aaron Gill-Braun on 2025-08-03.
//

#ifndef KERNEL_VFS_PIPE_H
#define KERNEL_VFS_PIPE_H

#include <kernel/base.h>
#include <kernel/vfs_types.h>
#include <kernel/mm_types.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>
#include <kernel/kio.h>

// pipe buffer size - 64KB (16 pages)
#define PIPE_BUFFER_SIZE PAGES_TO_SIZE(16)

// pipe flags
#define PIPE_READ_CLOSED  0x01  // read end closed
#define PIPE_WRITE_CLOSED 0x02  // write end closed

typedef struct pipe {
  uint32_t flags;               // pipe flags
  size_t buffer_size;           // size of buffer
  void *buffer;                 // pipe buffer
  struct timespec ctime;        // creation time
  
  size_t read_pos;              // read position
  size_t write_pos;             // write position
  size_t count;                 // bytes in buffer
  
  uint32_t readers;             // number of readers
  uint32_t writers;             // number of writers
  
  mtx_t lock;                   // pipe lock
  cond_t read_cond;             // readers wait here
  cond_t write_cond;            // writers wait here
  struct knlist knlist;         // knote list for kqueue events

  _refcount;                    // reference count
} pipe_t;

// pipe operations
__ref pipe_t *pipe_alloc(size_t buffer_size);
void _pipe_cleanup(__move pipe_t **piperef);

// pipe file operations
__ref file_t *pipe_create_read_file(pipe_t *pipe, int flags);
__ref file_t *pipe_create_write_file(pipe_t *pipe, int flags);
int pipe_f_open(file_t *file, int flags);
int pipe_f_close(file_t *file);
ssize_t pipe_f_read(file_t *file, kio_t *kio);
ssize_t pipe_f_write(file_t *file, kio_t *kio);
int pipe_f_stat(file_t *file, struct stat *statbuf);
int pipe_f_kqevent(file_t *file, knote_t *kn);
void pipe_f_cleanup(file_t *file);

// pipe reference counting
#define pipe_getref(pipe) ({ \
  ASSERT_IS_TYPE(pipe_t *, pipe); \
  pipe_t *__pipe = (pipe); \
  __pipe ? ref_get(&__pipe->refcount) : NULL; \
  __pipe; \
})

#define pipe_putref(piperef) ({ \
  ASSERT_IS_TYPE(pipe_t **, piperef); \
  pipe_t *__pipe = *(piperef); \
  *(piperef) = NULL; \
  if (__pipe) { \
    kassert(__pipe->refcount > 0); \
    if (ref_put(&__pipe->refcount)) { \
      _pipe_cleanup(&__pipe); \
    } \
  } \
})

// file operations table
extern struct file_ops pipe_file_ops;

#endif
