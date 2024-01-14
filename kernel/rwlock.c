//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/rwlock.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define RW_ASSERT(x, fmt, ...) kassertf(x, fmt, __VA_ARGS__)
#define RW_DEBUGF(m, fmt, ...) \
  if (__expect_false(rw_get_opts(m) & RW_DEBUG)) kprintf("rwlock: " fmt "", __VA_ARGS__)

#define rw_set_owner(m, td) (mtx->owner_opts = (uintptr_t)(td) | ((mtx)->owner_opts & MTX_OPT_MASK))
#define rw_set_opts(m, opts) (mtx->owner_opts = (opts) | ((mtx)->owner_opts & ~MTX_OPT_MASK))
#define rw_get_owner(m) ((thread_t *) ((mtx)->owner_opts & ~MTX_OPT_MASK))
#define rw_get_opts(m) ((mtx)->owner_opts & MTX_OPT_MASK)

// rwlock state
#define RW_UNOWNED   0x00 // free rwlock state
#define RW_LOCKED    0x01 // rwlock is locked
#define RW_READING   0x02 // rwlock is locked for reading
#define RW_DESTROYED 0x08 // rwlock has been destroyed

// MARK: common rwlock api

void _rw_init(rwlock_t *rw, uint32_t opts, const char *name) {
  rw->lo.name = name;
  rw->lo.flags = 0;
  rw->lo.data = 0; // recurse count
  rw->readers = 0;
}

void _rw_destroy(rwlock_t *rw) {

}

void _rw_assert(rwlock_t *rw, int what, const char *file, int line) {

}

int _rw_try_rlock(rwlock_t *rw, const char *file, int line) {
  todo();
}

int _rw_try_wlock(rwlock_t *rw, const char *file, int line) {
  todo();
}

void _rw_rlock(rwlock_t *rw, const char *file, int line) {

}

void _rw_wlock(rwlock_t *rw, const char *file, int line) {

}

void _rw_runlock(rwlock_t *rw) {

}

void _rw_wunlock(rwlock_t *rw) {

}
