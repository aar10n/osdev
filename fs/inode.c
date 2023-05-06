//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <inode.h>
#include <dentry.h>

#include <mm.h>
#include <printf.h>
#include <string.h>
#include <panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("inode: %s: " fmt, __func__, ##__VA_ARGS__)

static inline void link_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == NULL);
  I_LOCK(inode);
  D_LOCK(dentry);
  {
    inode->nlinks++;
    LIST_ADD(&inode->links, dentry, links);
    dentry->mode = inode->mode;
    dentry->inode = inode;
    dentry->ino = inode->ino;
  }
  D_UNLOCK(dentry);
  I_UNLOCK(inode);
}

static inline void unlink_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == inode);
  I_LOCK(inode);
  D_LOCK(dentry);
  {
    dentry->ino = 0;
    dentry->inode = NULL;
    LIST_REMOVE(&inode->links, dentry, links);
    inode->nlinks--;
  }
  D_UNLOCK(dentry);
  I_UNLOCK(inode);
}

//
// MARK: Virtual API
//

inode_t *i_alloc_empty() {
  inode_t *inode = kmallocz(sizeof(inode_t));
  mutex_init(&inode->lock, MUTEX_REENTRANT);
  rw_lock_init(&inode->data_lock);
  return inode;
}

inode_t *i_alloc(super_block_t *sb, ino_t ino, mode_t mode) {
  inode_t *inode = i_alloc_empty();
  inode->ino = ino;
  inode->mode = mode;
  inode->sb = sb;
  inode->ops = sb->fs->inode_ops;
  return inode;
}

void i_free(inode_t *inode) {
  // must be unlinked prior
  ASSERT(inode->sb == NULL);
  memset(inode, 0, sizeof(inode_t));
  kfree(inode);
}

int i_link_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == NULL);
  if (IS_IFDIR(inode) && inode->nlinks == 0) {
    // first time a 'directory' inode is getting linked to a child. add the
    // dot and dot dot entries before linking. handle the case where it is
    // a root node and has no parent.
    dentry_t *dparent = dentry->parent ? dentry->parent : dentry;
    inode_t *iparent = dparent->inode ? dparent->inode : inode;

    dentry_t *dot = d_alloc(".", 1, inode->mode, dentry->ops);
    d_add_child(dentry, dot);
    dentry_t *dotdot = d_alloc("..", 2, iparent->mode, dparent->ops);
    d_add_child(dentry, dotdot);

    link_dentry(inode, dot);
    link_dentry(iparent, dotdot);
    link_dentry(inode, dentry);
  } else {
    // normal case
    link_dentry(inode, dentry);
  }
  return 0;
}

int i_unlink_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == inode);
  unlink_dentry(inode, dentry);

  if (IS_IFDIR(dentry)) {
    // remove dot and dotdot dentries
    dentry_t *dot = d_get_child(dentry, ".", 1);
    ASSERT(dot != NULL);
    unlink_dentry(inode, dot);
    d_free(dot);

    dentry_t *dotdot = d_get_child(dentry, "..", 2);
    ASSERT(dotdot != NULL);
    unlink_dentry(dentry->parent->inode, dotdot);
    d_free(dotdot);
  }
  return 0;
}

//
// MARK: Operations
//

dentry_t *i_locate(inode_t *inode, dentry_t *dentry, const char *name, size_t name_len) {
  ASSERT(IS_IFDIR(inode));
  if (I_OPS(inode)->i_locate != NULL) {
    return I_OPS(inode)->i_locate(inode, dentry, name);
  }

  // load children if not already loaded and compare children names to find a match
  if (!IS_IFLLDIR(inode)) {
    if (i_loaddir(inode, dentry) < 0) {
      DPRINTF("i_locate: failed to load directory\n");
      return NULL;
    }
  }

  return d_get_child(dentry, name, name_len);
}

int i_loaddir(inode_t *inode, dentry_t *dentry) {
  ASSERT(I_OPS(inode)->i_loaddir != NULL);
  ASSERT(IS_IFDIR(inode));
  return I_OPS(inode)->i_loaddir(inode, dentry);
}


/**
 * Creates a regular file associated with the given inode. \b Optional.
 * Needed to support file creation.
 *
 * This should create a regular file entry in the parent directory. The inode and
 * dentry are both filled in and linked before this is called. If the blocks field
 * of the inode is non-zero, this function may want to preallocate some or all of
 * the requested blocks.
 *
 * @param dir The parent directory inode.
 * @param inode The regular file inode.
 * @param dentry The dentry for the inode.
 * @return
 */
int i_create(inode_t *dir, inode_t *inode, dentry_t *dentry) {
  if (I_OPS(dir)->i_create != NULL) {
    return I_OPS(dir)->i_create(dir, inode, dentry);
  }
  return -1;
}

int i_mknod(inode_t *dir, dentry_t *dentry, dev_t dev) {
  return -1;
}

int i_link(inode_t *dir, inode_t *inode, dentry_t *dentry) {
  return -1;
}

int i_unlink(inode_t *dir, dentry_t *dentry) {
  return -1;
}

int i_symlink(inode_t *dir, inode_t *dentry, const char *path) {
  return -1;
}

int i_readlink(dentry_t *dentry, size_t buflen, char *buffer) {
  return -1;
}

int i_mkdir(inode_t *dir, dentry_t *dentry) {
  return -1;
}

int i_rmdir(inode_t *dir, dentry_t *dentry) {
  return -1;
}

int i_rename(inode_t *o_dir, dentry_t *o_dentry, inode_t *n_dir, dentry_t *n_dentry) {
  return -1;
}
