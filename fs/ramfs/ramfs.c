//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#include "ramfs.h"

#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
// #define DPRINTF(fmt, ...) kprintf("ramfs: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)

#define LOCK_MOUNT(mount) mtx_spin_lock(&(mount)->lock)
#define UNLOCK_MOUNT(mount) mtx_spin_unlock(&(mount)->lock)
#define LOCK_NODE(node) mtx_lock(&(node)->lock)
#define UNLOCK_NODE(node) mtx_unlock(&(node)->lock)


ramfs_mount_t *ramfs_alloc_mount(vfs_t *vfs) {
  ramfs_mount_t *mount = kmallocz(sizeof(ramfs_mount_t));
  mount->vfs = vfs;

  mount->next_id = 1;
  mtx_init(&mount->lock, MTX_SPIN, "ramfs_mount_lock");

  ramfs_node_t *root = ramfs_alloc_node(
    mount, &(struct vattr) {
      .type = V_DIR,
      .mode = 0755,
    }
  );
  mount->root = root;

  return mount;
}

void ramfs_free_mount(ramfs_mount_t *mount) {
  ASSERT(mount->root == NULL);
  ASSERT(mount->data == NULL);
  kfree(mount);
}

ramfs_dentry_t *ramfs_alloc_dentry(ramfs_node_t *node, cstr_t name) {
  ramfs_dentry_t *dirent = kmallocz(sizeof(ramfs_dentry_t));
  dirent->node = node;
  dirent->name = str_from_cstr(name);
  return dirent;
}

void ramfs_free_dentry(ramfs_dentry_t *dentry) {
  str_free(&dentry->name);
  kfree(dentry);
}

ramfs_node_t *ramfs_alloc_node(ramfs_mount_t *mount, struct vattr *vattr) {
  LOCK_MOUNT(mount);
  id_t id = mount->next_id++;
  UNLOCK_MOUNT(mount);

  ramfs_node_t *node = kmallocz(sizeof(ramfs_node_t));
  node->id = id;
  node->mount = mount;
  node->type = vattr->type;
  node->mode = vattr->mode;
  mtx_init(&node->lock, 0, "ramfs_node_lock");
  return node;
}

void ramfs_free_node(ramfs_node_t *node) {
  ASSERT(node->data == NULL);
  kfree(node);
}

void ramfs_add_dentry(ramfs_node_t *dir, ramfs_dentry_t *dentry) {
  ASSERT(dir->type == V_DIR);
  LOCK_NODE(dir);
  dentry->node->parent = dir;
  LIST_ADD(&dir->n_dir, dentry, list);
  UNLOCK_NODE(dir);
}

void ramfs_remove_dentry(ramfs_node_t *dir, ramfs_dentry_t *dentry) {
  ASSERT(dir->type == V_DIR);
  LOCK_NODE(dir);
  ASSERT(dentry->node->parent == dir);
  dentry->node->parent = NULL;
  LIST_REMOVE(&dir->n_dir, dentry, list);
  UNLOCK_NODE(dir);
}

ramfs_dentry_t *ramfs_lookup_dentry(ramfs_node_t *dir, cstr_t name) {
  ASSERT(dir->type == V_DIR);
  ramfs_dentry_t *dirent = NULL;
  LOCK_NODE(dir);
  dirent = LIST_FIND(d, &dir->n_dir, list, str_eq_cstr(d->name, name));
  UNLOCK_NODE(dir);
  return dirent;
}

// MARK: fs registration

struct vfs_ops ramfs_vfs_ops = {
  .v_mount = ramfs_vfs_mount,
  .v_unmount = ramfs_vfs_unmount,
  .v_sync = ramfs_vfs_sync,
  .v_stat = ramfs_vfs_stat,
  .v_cleanup = ramfs_vfs_cleanup,
};

struct vnode_ops ramfs_vnode_ops = {
  .v_read = ramfs_vn_read,
  .v_write = ramfs_vn_write,
  .v_getpage = ramfs_vn_getpage,
  .v_falloc = ramfs_vn_falloc,

  .v_readlink = ramfs_vn_readlink,
  .v_readdir = ramfs_vn_readdir,

  .v_lookup = ramfs_vn_lookup,
  .v_create = ramfs_vn_create,
  .v_mknod = ramfs_vn_mknod,
  .v_symlink = ramfs_vn_symlink,
  .v_hardlink = ramfs_vn_hardlink,
  .v_unlink = ramfs_vn_unlink,
  .v_mkdir = ramfs_vn_mkdir,
  .v_rmdir = ramfs_vn_rmdir,

  .v_cleanup = ramfs_vn_cleanup,
};

struct ventry_ops ramfs_ventry_ops = {
  .v_cleanup = ramfs_ve_cleanup,
};

static fs_type_t ramfs_type = {
  .name = "ramfs",
  .vfs_ops = &ramfs_vfs_ops,
  .vn_ops = &ramfs_vnode_ops,
  .ve_ops = &ramfs_ventry_ops,
};


static void ramfs_static_init() {
  if (fs_register_type(&ramfs_type) < 0) {
    panic("failed to register ramfs type\n");
  }
}
STATIC_INIT(ramfs_static_init);
