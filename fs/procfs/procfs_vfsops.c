//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#define PROCFS_INTERNAL
#include "procfs.h"

#include <kernel/vfs/path.h>
#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>
#include <fs/ramfs/ramfs.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("procfs: " fmt, ##__VA_ARGS__)

extern procfs_dir_t *global_procfs_root_dir;
extern struct vnode_ops procfs_vn_ops;

static void procfs_reconstruct_dir(procfs_dir_t *dir, ramfs_node_t *ramfs_dir) {
  LIST_FOR_IN(entry, &dir->entries, next) {
    enum vtype type = (entry->dir != NULL) ? V_DIR : V_REG;
    ramfs_node_t *node = ramfs_alloc_node(ramfs_dir->mount, &make_vattr(type, entry->obj->mode));
    node->data = entry->obj;
    node->ops = &procfs_vn_ops;
    ramfs_dentry_t *dentry = ramfs_alloc_dentry(node, cstr_from_str(entry->name));
    ramfs_add_dentry(ramfs_dir, dentry);

    // track ramfs node on the object
    LIST_ADD(&dir->obj->nodes, node, list);

    if (entry->dir) {
      procfs_reconstruct_dir(entry->dir, node);
    }
  }
}

int procfs_vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve, __move ventry_t **root) {
  ASSERT(vfs != NULL);
  ASSERT(root != NULL);
  
  // create the hosting ramfs mount
  ramfs_mount_t *mount = ramfs_alloc_mount(vfs);
  vfs->data = mount;

  // reconstruct the profcs structure in ramfs
  procfs_reconstruct_dir(global_procfs_root_dir, mount->root);

  // create the root vnode
  vnode_t *vn = vn_alloc(1, &make_vattr(V_DIR, 0755 | S_IFDIR));
  vn->data = mount->root;
  ventry_t *ve = ve_alloc_linked(cstr_new("/", 1), vn);
  *root = moveref(ve);
  vn_putref(&vn);

  DPRINTF("mounted procfs\n");
  return 0;
}
