//
// Created by Aaron Gill-Braun on 2021-07-16.
//

#include <super.h>
#include <inode.h>
#include <dcache.h>
#include <mm.h>

#include <string.h>
#include <panic.h>
#include <printf.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("super: %s: " fmt, __func__, ##__VA_ARGS__)

#define ITABLE(sb) ((sb)->itable)
#define ITABLE_LOCK(sb) spin_lock(&ITABLE(sb)->lock)
#define ITABLE_UNLOCK(sb) spin_unlock(&ITABLE(sb)->lock)

// This is defined as an opaque type in fs_types.h because it should only be
// modified and accessed by this file.
struct itable {
  rb_tree_t *tree;
  spinlock_t lock;
};

static struct itable *itable_alloc() {
  struct itable *itable = kmallocz(sizeof(struct itable));
  itable->tree = create_rb_tree();
  spin_init(&itable->lock);
  return itable;
}

static void itable_free(struct itable *itable) {
  kfree(itable->tree->nil);
  kfree(itable->tree);
  kfree(itable);
}

//
// MARK: Superblock API
//

super_block_t *sb_alloc(fs_type_t *fs_type) {
  super_block_t *sb = kmallocz(sizeof(super_block_t));
  sb->fs = fs_type;
  sb->ops = fs_type->sb_ops;
  mutex_init(&sb->lock, MUTEX_REENTRANT);
  return sb;
}

void sb_free(super_block_t *sb) {
  ASSERT(sb->data == NULL);
  ASSERT(sb->dcache == NULL);
  ASSERT(sb->ino_count == 0);

  if (sb->itable != NULL) {
    // in case the superblock was never mounted or the unmount went wrong
    itable_free(sb->itable);
  }
  kfree(sb);
}

int sb_add_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(inode->sb == NULL);
  ASSERT(inode->ops == NULL);
  if (rb_tree_find(sb->itable->tree, inode->ino) != NULL) {
    DPRINTF("duplicate inode %d already exists\n", inode->ino);
    return -1;
  }

  S_LOCK(sb);
  I_LOCK(inode);
  sb->ino_count++;
  inode->sb = sb;
  inode->ops = sb->fs->inode_ops;
  I_UNLOCK(inode);
  S_UNLOCK(sb);

  ITABLE_LOCK(sb);
  rb_tree_insert(sb->itable->tree, inode->ino, inode);
  ITABLE_UNLOCK(sb);
  return 0;
}

int sb_remove_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(inode->sb == sb);
  ASSERT(inode->ops == sb->fs->inode_ops);
  if (rb_tree_find(sb->itable->tree, inode->ino) == NULL) {
    DPRINTF("inode %d not found in itable\n", inode->ino);
    return -1;
  }

  ITABLE_LOCK(sb);
  rb_tree_delete(sb->itable->tree, inode->ino);
  ITABLE_UNLOCK(sb);

  S_LOCK(sb);
  I_LOCK(inode);
  sb->ino_count--;
  inode->sb = NULL;
  inode->ops = NULL;
  I_UNLOCK(inode);
  S_UNLOCK(sb);
  return 0;
}

//
// MARK: Superblock Operations
//

int sb_mount(super_block_t *sb, dentry_t *mount, device_t *device, int flags) {
  ASSERT(IS_IFDIR(mount));
  ASSERT(mount->inode == NULL);

  // allocate new inode for mount point
  inode_t *inode = i_alloc(sb, 0, S_IFDIR);

  int res;
  sb->itable = itable_alloc();
  sb->dcache = dcache_create(&sb->mount);
  sb->mount = mount;
  sb->device = device;
  sb->mount_flags = sb->fs->flags | flags;

  if ((res = S_OPS(sb)->sb_mount(sb, mount)) < 0) {
    DPRINTF("failed to mount filesystem: %d\n", res);
    dcache_destroy(sb->dcache);
    itable_free(sb->itable);
    i_free(inode);
    return res;
  }

  ASSERT(mount->inode != NULL);
  if ((res = I_OPS(mount->inode)->i_loaddir(mount->inode, mount)) < 0) {
    DPRINTF("failed to load directory: %d\n", res);
    dcache_destroy(sb->dcache);
    itable_free(sb->itable);
    i_unlink_dentry(inode, mount);
    i_free(inode);
    return res;
  }

  // add to mounts
  FS_TYPE_LOCK(sb->fs);
  LIST_ADD(&sb->fs->mounts, sb, list);
  FS_TYPE_UNLOCK(sb->fs);
  return 0;
}

int sb_unmount(super_block_t *sb) {
  int res;
  if ((res = S_OPS(sb)->sb_unmount(sb)) < 0) {
    DPRINTF("failed to unmount filesystem: %d\n", res);
    return res;
  }

  dcache_destroy(sb->dcache);
  sb->dcache = NULL;
  itable_free(sb->itable);
  sb->itable = NULL;
  return 0;
}

int sb_write(super_block_t *sb) {
  ASSERT(!(sb->mount_flags & FS_RDONLY));

  int res;
  S_LOCK(sb);
  {
    if ((res = S_OPS(sb)->sb_write(sb)) < 0) {
      DPRINTF("failed to write superblock: %d\n", res);
      S_UNLOCK(sb);
      return res;
    }
  }
  S_UNLOCK(sb);
  return 0;
}

int sb_read_inode(super_block_t *sb, inode_t *inode) {
  if (inode->flags & I_LOADED)
    return 0;

  int res;
  S_LOCK(sb);
  I_LOCK(inode);
  {
    if ((res = S_OPS(sb)->sb_read_inode(sb, inode)) < 0) {
      DPRINTF("failed to read inode %d: %d\n", inode->ino, res);
      I_UNLOCK(inode);
      S_UNLOCK(sb);
      return res;
    }
    inode->flags |= I_LOADED;
  }
  I_UNLOCK(inode);
  S_UNLOCK(sb);
  return 0;
}

int sb_write_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(!(inode->sb->mount_flags & FS_RDONLY));
  if (!(inode->flags & I_DIRTY))
    return 0;

  int res;
  S_LOCK(sb);
  I_LOCK(inode);
  {
    if ((res = S_OPS(sb)->sb_write_inode(sb, inode)) < 0) {
      DPRINTF("failed to write inode %d: %d\n", inode->ino, res);
      I_UNLOCK(inode);
      S_UNLOCK(sb);
      return res;
    }
    inode->flags &= ~I_DIRTY;
  }
  I_UNLOCK(inode);
  S_UNLOCK(sb);
  return 0;
}

int sb_alloc_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(!(inode->sb->mount_flags & FS_RDONLY));

  int res;
  S_LOCK(sb);
  {
    if ((res = S_OPS(sb)->sb_alloc_inode(sb, inode)) < 0) {
      DPRINTF("failed to allocate inode: %d\n", res);
      S_UNLOCK(sb);
      return res;
    }
  }
  S_UNLOCK(sb);
  return 0;
}

int sb_delete_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(!(inode->sb->mount_flags & FS_RDONLY));

  int res;
  S_LOCK(sb);
  {
    if ((res = S_OPS(sb)->sb_delete_inode(sb, inode)) < 0) {
      DPRINTF("failed to delete inode: %d\n", res);
      S_UNLOCK(sb);
      return res;
    }
  }
  S_UNLOCK(sb);
  return 0;
}

