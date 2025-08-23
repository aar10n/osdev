//
// Created by Aaron Gill-Braun on 2025-06-12.
//

#include "devfs.h"

#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <kernel/vfs/ventry.h>

#include <fs/ramfs/ramfs.h>

#define ASSERT(x) kassert(x)
// #define DPRINTF(fmt, ...) kprintf("devfs: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("devfs: " fmt, ##__VA_ARGS__)


int devfs_vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve, __move ventry_t **root) {
  int res = ramfs_vfs_mount(vfs, device, mount_ve, root);
  if (res < 0) {
    EPRINTF("failed to mount devfs: %d\n", res);
    return res;
  }

  char path[PATH_MAX];
  sbuf_t pathbuf = sbuf_init(path, PATH_MAX);
  size_t pathlen = ve_get_path(mount_ve, &pathbuf);
  if (pathlen < 0) {
    EPRINTF("failed to get devfs root path: {:err}\n", pathlen);
    return -ENAMETOOLONG;
  }

  devfs_mount_t *devfs_mount = kmallocz(sizeof(devfs_mount_t));
  devfs_mount->path = str_new(path, pathlen);
  devfs_mount->pid = -1;

  ramfs_mount_t *ramfs_mount = vfs->data;
  ramfs_mount->data = devfs_mount;

  // start the devfs synchronization process
  __ref proc_t *proc = proc_alloc_new(getref(curproc->creds));
  devfs_mount->pid = proc->pid;
  proc_setup_add_thread(proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  proc_setup_entry(proc, (uintptr_t) devfs_synchronize_main, 1, devfs_mount);
  proc_setup_name(proc, cstr_make("devfs_synchronize"));
  proc_finish_setup_and_submit_all(moveref(proc));
  return 0;
}

int devfs_vfs_unmount(vfs_t *vfs) {
  ramfs_mount_t *ramfs_mount = vfs->data;
  devfs_mount_t *devfs_mount = ramfs_mount->data;
  if (devfs_mount) {
    // kill the devfs process
    proc_t *proc = proc_lookup(devfs_mount->pid);
    proc_terminate(proc, 0, SIGTERM);

    str_free(&devfs_mount->path);
    kfree(devfs_mount);
    ramfs_mount->data = NULL;
  }

  return ramfs_vfs_unmount(vfs);
}
