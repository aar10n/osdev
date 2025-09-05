//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/clock.h>

#include <kernel/vfs/vnode.h>

#include "procfs.h"

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("procfs: " fmt, ##__VA_ARGS__)

// example: multi-item file using full seq_ops iterator
// this demonstrates a file that lists multiple items

struct test_items {
  int count;
  const char *prefix;
};

static void *test_items_start(seqfile_t *sf, off_t *ppos) { // NOLINT(*-non-const-parameter)
  struct test_items *items = sf->data;
  off_t pos = *ppos;  // read the value to avoid const warning
  if (pos >= items->count)
    return NULL;
  return (void *)(uintptr_t)(pos + 1); // return non-NULL for valid position
}

static void test_items_stop(seqfile_t *sf, void *v) {
  // nothing to clean up
}

static void *test_items_next(seqfile_t *sf, void *v, off_t *pos) {
  struct test_items *items = sf->data;
  (*pos)++;
  if (*pos >= items->count)
    return NULL;
  return (void *)(uintptr_t)(*pos + 1);
}

static int test_items_show(seqfile_t *sf, void *v) {
  struct test_items *items = sf->data;
  off_t idx = (off_t)(uintptr_t)v - 1;
  return seq_printf(sf, "%s%ld\n", items->prefix, idx);
}

static struct seq_ops test_items_seq_ops = {
  .start = test_items_start,
  .stop = test_items_stop,
  .next = test_items_next,
  .show = test_items_show,
};

static struct test_items test_items_data = {
  .count = 10,
  .prefix = "item_",
};

// example: /proc/testdir - dynamic directory

static ssize_t dynamic_file_read(procfs_handle_t *h, off_t off, kio_t *kio) {
  if (off != 0) {
    return 0; // EOF
  }

  procfs_object_t *obj = h->obj;
  cstr_t name = procfs_obj_name(obj);
  size_t nbytes = kio_write_in(kio, cstr_ptr(name), cstr_len(name), 0);
  nbytes += kio_write_in(kio, "\n", 1, 0);
  return (ssize_t) nbytes;
}

static void dynamic_file_cleanup(procfs_object_t *obj) {
  cstr_t name = procfs_obj_name(obj);
  DPRINTF("cleaning up dynamic file object {:cstr}\n", &name);
}

static procfs_ops_t dynamic_dir_ops;
static procfs_ops_t dynamic_file_ops = {
  .proc_read = dynamic_file_read,
  .proc_cleanup = dynamic_file_cleanup,
};

static ssize_t dynamic_dir_readdir(procfs_handle_t *h, off_t *poff, struct dirent *dirent) {
  procfs_object_t *obj = h->obj;
  off_t idx = *poff;

  // simple static entries for demonstration
  const char *entries[] = {"file1", "file2", "recursive", NULL};
  if (idx >= sizeof(entries) / sizeof(entries[0]) || entries[idx] == NULL) {
    return 0; // no more entries
  }

  enum vtype type = idx == 2 ? V_DIR : V_REG;
  *dirent = dirent_make_entry(idx+1, idx, type, cstr_make(entries[idx]));
  (*poff)++;
  return dirent->d_reclen;
}

static int dynamic_dir_lookup(procfs_object_t *obj, cstr_t name, __move procfs_object_t **result) {
  if (!cstr_in_charp_list(name, charp_list("file1", "file2", "recursive"))) {
    return -ENOENT;
  }

  bool is_dir = cstr_eq_charp(name, "recursive");
  procfs_ops_t *ops;
  if (is_dir) {
    ops = &dynamic_dir_ops;
  } else {
    ops = &dynamic_file_ops;
  }

  procfs_object_t *child_obj = procfs_ephemeral_object(name, ops, NULL, 0644, is_dir);
  ASSERT(child_obj != NULL);
  *result = moveptr(child_obj);
  return 0; // success
}

static procfs_ops_t dynamic_dir_ops = {
  .proc_readdir = dynamic_dir_readdir,
  .proc_lookup = dynamic_dir_lookup,
};

//

static void procfs_seqfile_examples_register() {
  DPRINTF("registering procfs seqfile examples\n");
  procfs_register_dir(cstr_make("/sys/kernel/testdir"), &dynamic_dir_ops, NULL, 0555);
  procfs_register_seq_file(cstr_make("/test_items"), &test_items_seq_ops, &test_items_data, 0444);
}
MODULE_INIT(procfs_seqfile_examples_register);
