//
// Created by Aaron Gill-Braun on 2025-06-12.
//

#ifndef FS_DEVFS_DEVFS_H
#define FS_DEVFS_DEVFS_H

#include <kernel/device.h>
#include <kernel/vfs_types.h>
#include <kernel/str.h>

typedef struct devfs_mount {
  str_t path;
  pid_t pid;
} devfs_mount_t;

typedef struct devfs_class {
  int major;
  int minor;
  const char *prefix;
  int attr;
  LIST_ENTRY(struct devfs_class) list;
} devfs_class_t;

/* devfs attributes */
#define DEVFS_NUMBERED  0
#define DEVFS_LETTERED  1

int devfs_register_class(int major, int minor, const char *prefix, int attr);

int devfs_synchronize_main(devfs_mount_t *mount);

int devfs_vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve, __move ventry_t **root);
int devfs_vfs_unmount(vfs_t *vfs);


#endif
