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

// example: /proc/version - kernel version
static ssize_t version_read(procfs_object_t *obj, off_t off, kio_t *kio) {
  if (off != 0) {
    return 0; // EOF
  }

  const char *version = "osdev 1.0.0\n";
  return procfs_obj_read_string(obj, off, kio, version);
}

static procfs_ops_t version_ops = {
  .pf_read = version_read,
};

// example: /proc/uptime - system uptime in seconds
static ssize_t uptime_read(procfs_object_t *obj, off_t off, kio_t *kio) {
  if (off != 0) {
    return 0; // EOF
  }

  uint64_t uptime_ms = NS_TO_MS(clock_get_nanos());
  uint64_t uptime_sec = uptime_ms / 1000;
  char buf[64];
  size_t len = ksnprintf(buf, sizeof(buf), "%llu.%03llu\n", uptime_sec, uptime_ms % 1000);
  return procfs_obj_read_string(obj, off, kio, buf);
}

static procfs_ops_t uptime_ops = {
  .pf_read = uptime_read,
};

// example: /proc/cmdline - kernel command line
static ssize_t cmdline_read(procfs_object_t *obj, off_t off, kio_t *kio) {
  if (off != 0) {
    return 0; // EOF
  }

  const char *cmdline = "console=ttyS0 debug\n";
  return procfs_obj_read_string(obj, off, kio, cmdline);
}

static procfs_ops_t cmdline_ops = {
  .pf_read = cmdline_read,
};

// example: /proc/sys/kernel/hostname - system hostname (read/write)
static char hostname[256] = "localhost";

static ssize_t hostname_read(procfs_object_t *obj, off_t off, kio_t *kio) {
  if (off != 0) {
    return 0; // EOF
  }

  char buf[257];
  size_t len = ksnprintf(buf, sizeof(buf), "%s\n", hostname);
  return procfs_obj_read_string(obj, off, kio, buf);
}

static ssize_t hostname_write(procfs_object_t *obj, off_t off, kio_t *kio) {
  if (off != 0) {
    return -EINVAL;
  }

  DPRINTF("hostname_write: writing to hostname\n");

  size_t len = min(kio_remaining(kio), sizeof(hostname) - 1);
  size_t nbytes = kio_read_out(hostname, len, 0, kio);
  if (nbytes == 0) {
    return (ssize_t) nbytes;
  }
  
  // remove trailing newline if present
  if (hostname[nbytes - 1] == '\n') {
    hostname[nbytes - 1] = '\0';
  } else {
    hostname[nbytes] = '\0';
  }
  
  return (ssize_t) nbytes;
}

static procfs_ops_t hostname_ops = {
  .pf_read = hostname_read,
  .pf_write = hostname_write,
};

// example: /proc/testdir - dynamic directory

static ssize_t dynamic_file_read(procfs_object_t *obj, off_t off, kio_t *kio) {
  if (off != 0) {
    return 0; // EOF
  }

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
  .pf_cleanup = dynamic_file_cleanup,
  .pf_read = dynamic_file_read,
};

static ssize_t dynamic_dir_readdir(procfs_object_t *obj, off_t idx, struct dirent *dirent) {
  // simple static entries for demonstration
  const char *entries[] = {"file1", "file2", "recursive", NULL};

  if (idx >= sizeof(entries) / sizeof(entries[0]) || entries[idx] == NULL) {
    return 0; // no more entries
  }

  enum vtype type = idx == 2 ? V_DIR : V_REG;
  *dirent = dirent_make_entry(idx+1, idx, type, cstr_make(entries[idx]));
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
  .pf_readdir = dynamic_dir_readdir,
  .pf_lookup = dynamic_dir_lookup,
};


// register example procfs entries
static void procfs_examples_module_init() {
  DPRINTF("registering example procfs entries\n");

  procfs_register_file(cstr_make("/version"), &version_ops, NULL, 0444);
  procfs_register_file(cstr_make("/uptime"), &uptime_ops, NULL, 0444);
  procfs_register_file(cstr_make("/cmdline"), &cmdline_ops, NULL, 0444);

  procfs_register_static_dir(cstr_make("/sys"), 0755);
  procfs_register_static_dir(cstr_make("/sys/kernel"), 0755);
  procfs_register_file(cstr_make("/sys/kernel/hostname"), &hostname_ops, NULL, 0644);
  procfs_register_dir(cstr_make("/sys/dynamic"), &dynamic_dir_ops, NULL, 0755);
  
  DPRINTF("example procfs entries registered\n");
}
MODULE_INIT(procfs_examples_module_init);
