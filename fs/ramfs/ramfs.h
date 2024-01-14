//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#ifndef FS_RAMFS_RAMFS_H
#define FS_RAMFS_RAMFS_H

#include <kernel/vfs_types.h>
#include <kernel/mm_types.h>
#include <kernel/device.h>
#include <kernel/mutex.h>

struct memfile;
struct ramfs_node;
struct ramfs_dentry;

typedef struct ramfs_mount {
  vfs_t *vfs; // no ref held
  void *data; // embedded data

  struct ramfs_node *root;
  size_t num_nodes;
  mtx_t lock; // spin mtx
  id_t next_id;
} ramfs_mount_t;

typedef struct ramfs_node {
  id_t id;
  enum vtype type;
  mode_t mode;
  void *data; // embedded fs data
  size_t size;

  mtx_t lock; // wait mtx
  struct ramfs_node *parent;
  struct ramfs_mount *mount;

  union {
    str_t n_link;
    struct memfile *n_file;
    LIST_HEAD(struct ramfs_dentry) n_dir;
  };
} ramfs_node_t;

typedef struct ramfs_dentry {
  str_t name;
  ventry_t *ventry; // no ref held
  struct ramfs_node *node;
  LIST_ENTRY(struct ramfs_dentry) list;
} ramfs_dentry_t;

// MARK: ramfs api for embedding filesystems

ramfs_mount_t *ramfs_alloc_mount(vfs_t *vfs); // allocates root node too
void ramfs_free_mount(ramfs_mount_t *mount);
ramfs_dentry_t *ramfs_alloc_dentry(ramfs_node_t *node, cstr_t name);
void ramfs_free_dentry(ramfs_dentry_t *dentry);
ramfs_node_t *ramfs_alloc_node(ramfs_mount_t *mount, struct vattr *vattr);
void ramfs_free_node(ramfs_node_t *node);
void ramfs_add_dentry(ramfs_node_t *dir, ramfs_dentry_t *dentry);
void ramfs_remove_dentry(ramfs_node_t *dir, ramfs_dentry_t *dentry);
ramfs_dentry_t *ramfs_lookup_dentry(ramfs_node_t *dir, cstr_t name);

// MARK: implementation

// vfs operations
int ramfs_vfs_mount(vfs_t *vfs, device_t *device, __move ventry_t **rootve);
int ramfs_vfs_unmount(vfs_t *vfs);
int ramfs_vfs_sync(vfs_t *vfs);
int ramfs_vfs_stat(vfs_t *vm, struct vfs_stat *stat);
void ramfs_vfs_cleanup(vfs_t *vfs);

// vnode operations
ssize_t ramfs_vn_read(vnode_t *vn, off_t off, kio_t *kio);
ssize_t ramfs_vn_write(vnode_t *vn, off_t off, kio_t *kio);
int ramfs_vn_map(vnode_t *vn, off_t off, vm_mapping_t *vm);

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
