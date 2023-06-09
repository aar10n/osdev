//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#include "ramfs.h"
#include "ramfs_file.h"

#include <mm.h>
#include <panic.h>
#include <printf.h>
#include <fs.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ramfs: " fmt, ##__VA_ARGS__)

#define LOCK_MOUNT(mount) SPIN_LOCK(&(mount)->lock)
#define UNLOCK_MOUNT(mount) SPIN_UNLOCK(&(mount)->lock)
#define LOCK_NODE(node) mutex_lock(&(node)->lock)
#define UNLOCK_NODE(node) mutex_unlock(&(node)->lock)

extern struct vfs_ops ramfs_vfs_ops;
extern struct vnode_ops ramfs_vnode_ops;
extern struct ventry_ops ramfs_ventry_ops;
static fs_type_t ramfs_type;


static void ramfs_static_init() {
  ramfs_type.name = "ramfs";
  ramfs_type.vfs_ops = &ramfs_vfs_ops;
  ramfs_type.vnode_ops = &ramfs_vnode_ops;
  ramfs_type.ventry_ops = &ramfs_ventry_ops;
  if (fs_register_type(&ramfs_type) < 0) {
    DPRINTF("failed to register ramfs type\n");
  }
}
STATIC_INIT(ramfs_static_init);


// MARK: ramfs node functions

ramfs_node_t *ramfs_node_alloc(ramfs_mount_t *mount, enum vtype type, mode_t mode) {
  LOCK_MOUNT(mount);
  id_t id = mount->next_id++;
  UNLOCK_MOUNT(mount);

  ramfs_node_t *node = kmallocz(sizeof(ramfs_node_t));
  node->id = id;
  node->mount = mount;
  node->type = type;
  node->mode = mode;
  mutex_init(&node->lock, 0);
  return node;
}

void ramfs_node_free(ramfs_node_t *node) {
  ASSERT(mutex_trylock(&node->lock) == 0);
  kfree(node);
}

void ramfs_dir_add(ramfs_node_t *dir, ramfs_dirent_t *dirent) {
  ASSERT(dir->type == V_DIR);
  ASSERT(dirent->parent == NULL);
  LOCK_NODE(dir);
  dirent->parent = dir;
  LIST_ADD(&dir->n_dir, dirent, list);
  UNLOCK_NODE(dir);
}

void ramfs_dir_remove(ramfs_node_t *dir, ramfs_dirent_t *dirent) {
  ASSERT(dir->type == V_DIR);
  ASSERT(dirent->parent == dir);
  LOCK_NODE(dir);
  LIST_REMOVE(&dir->n_dir, dirent, list);
  UNLOCK_NODE(dir);
}


ramfs_dirent_t *ramfs_dirent_alloc(ramfs_node_t *node, cstr_t name) {
  ramfs_dirent_t *dirent = kmallocz(sizeof(ramfs_dirent_t));
  dirent->node = node;
  dirent->name = str_copy_cstr(name);
  return dirent;
}

void ramfs_dirent_free(ramfs_dirent_t *dirent) {
  str_free(&dirent->name);
  kfree(dirent);
}

ramfs_dirent_t *ramfs_dirent_lookup(ramfs_node_t *dir, cstr_t name) {
  ASSERT(dir->type == V_DIR);
  ramfs_dirent_t *dirent = NULL;
  LOCK_NODE(dir);
  dirent = LIST_FIND(d, &dir->n_dir, list, str_eq_c(d->name, name));
  UNLOCK_NODE(dir);
  return dirent;
}
