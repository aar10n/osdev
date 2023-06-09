//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#ifndef FS_RAMFS_RAMFS_H
#define FS_RAMFS_RAMFS_H

#include <vfs_types.h>
#include <mm_types.h>
#include <device.h>
#include <queue.h>

struct ramfs_file;
struct ramfs_node;
struct ramfs_dirent;

typedef struct ramfs_mount {
  id_t next_id;
  size_t num_nodes;
  spinlock_t lock;
  vfs_t *vfs; // no ref held
} ramfs_mount_t;

typedef struct ramfs_node {
  id_t id;
  enum vtype type;
  mode_t mode;
  vnode_t *vnode; // no ref held

  mutex_t lock;
  ramfs_mount_t *mount;

  union {
    dev_t n_dev;
    str_t n_link;
    struct ramfs_file *n_file;
    LIST_HEAD(struct ramfs_dirent) n_dir;
  };
} ramfs_node_t;

typedef struct ramfs_dirent {
  str_t name;
  ramfs_node_t *node;
  ramfs_node_t *parent;
  LIST_ENTRY(struct ramfs_dirent) list; // sibling list
  ventry_t *ventry; // no ref held
} ramfs_dirent_t;

ramfs_node_t *ramfs_node_alloc(ramfs_mount_t *mount, enum vtype type, mode_t mode);
void ramfs_node_free(ramfs_node_t *node);
void ramfs_dir_add(ramfs_node_t *dir, ramfs_dirent_t *dirent);
void ramfs_dir_remove(ramfs_node_t *dir, ramfs_dirent_t *dirent);

ramfs_dirent_t *ramfs_dirent_alloc(ramfs_node_t *node, cstr_t name);
void ramfs_dirent_free(ramfs_dirent_t *dirent);
ramfs_dirent_t *ramfs_dirent_lookup(ramfs_node_t *dir, cstr_t name);

// MARK: implementation

// vfs operations
int ramfs_vfs_mount(vfs_t *vfs, device_t *device, __move ventry_t **rootve);
int ramfs_vfs_unmount(vfs_t *vfs);
int ramfs_vfs_sync(vfs_t *vfs);
int ramfs_vfs_stat(vfs_t *vm, struct vfs_stat *stat);

// vnode operations
ssize_t ramfs_vn_read(vnode_t *vn, off_t off, kio_t *kio);
ssize_t ramfs_vn_write(vnode_t *vn, off_t off, kio_t *kio);
int ramfs_vn_map(vnode_t *vn, off_t off, vm_mapping_t *vm);

int ramfs_vn_load(vnode_t *vn);
int ramfs_vn_save(vnode_t *vn);
int ramfs_vn_readlink(vnode_t *vn, kio_t *kio);
ssize_t ramfs_vn_readdir(vnode_t *vn, off_t off, kio_t *kio);

int ramfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result);
int ramfs_vn_create(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result);
int ramfs_vn_mknod(vnode_t *dir, cstr_t name, struct vattr *vattr, dev_t dev, __move ventry_t **result);
int ramfs_vn_symlink(vnode_t *dir, cstr_t name, struct vattr *vattr, cstr_t target, __move ventry_t **result);
int ramfs_vn_hardlink(vnode_t *dir, cstr_t name, vnode_t *target, __move ventry_t **result);
int ramfs_vn_unlink(vnode_t *dir, vnode_t *vn, ventry_t *ve);
int ramfs_vn_mkdir(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result);
int ramfs_vn_rmdir(vnode_t *dir, vnode_t *vn, ventry_t *ve);

void ramfs_vn_cleanup(vnode_t *vn);
void ramfs_ve_cleanup(ventry_t *ve);

#endif
