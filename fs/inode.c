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

#define REAL true
#define FAKE false

static inline void link_dentry(inode_t *inode, dentry_t *dentry, bool real) {
  ASSERT(dentry->inode == NULL);
  I_LOCK(inode);
  D_LOCK(dentry);
  {
    if (real) {
      inode->nlinks++;
    }
    LIST_ADD(&inode->links, dentry, links);
    dentry->ino = inode->ino;
    dentry->mode = inode->mode;
    dentry->inode = inode;
  }
  D_UNLOCK(dentry);
  I_UNLOCK(inode);
}

static inline void unlink_dentry(inode_t *inode, dentry_t *dentry, bool real) {
  ASSERT(dentry->inode == inode);
  I_LOCK(inode);
  D_LOCK(dentry);
  {
    dentry->ino = 0;
    dentry->inode = NULL;
    LIST_REMOVE(&inode->links, dentry, links);
    if (real) {
      inode->nlinks--;
    }
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
    link_dentry(inode, dentry, REAL);

    // first time a 'directory' inode is getting linked to a child. add the
    // dot and dot dot entries after linking the main dentry. handle the case
    // where it is a root node and has no parent.
    dentry_t *dparent = dentry->parent ? dentry->parent : dentry;
    inode_t *iparent = dparent->inode ? dparent->inode : inode;

    dentry_t *dot = d_alloc(".", 1, inode->mode, dentry->ops);
    dentry_t *dotdot = d_alloc("..", 2, iparent->mode, dparent->ops);

    // add in reverse order so that we have '.' followed by '..'
    d_add_child_front(dentry, dotdot);
    link_dentry(inode, dot, FAKE);

    d_add_child_front(dentry, dot);
    link_dentry(iparent, dotdot, FAKE);
  } else {
    // normal case
    link_dentry(inode, dentry, REAL);
  }
  return 0;
}

int i_unlink_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == inode);
  unlink_dentry(inode, dentry, REAL);

  if (IS_IFDIR(dentry)) {
    // remove dot and dotdot dentries
    dentry_t *dot = d_get_child(dentry, ".", 1);
    ASSERT(dot != NULL);
    unlink_dentry(inode, dot, FAKE);
    d_free(dot);

    dentry_t *dotdot = d_get_child(dentry, "..", 2);
    ASSERT(dotdot != NULL);
    unlink_dentry(dentry->parent->inode, dotdot, FAKE);
    d_free(dotdot);
  }
  return 0;
}

//
// MARK: Operations
//

int i_locate(inode_t *inode, dentry_t *dentry, const char *name, size_t name_len, dentry_t **result) {
  ASSERT(IS_IFDIR(inode));
  if (I_OPS(inode)->i_locate != NULL) {
    return I_OPS(inode)->i_locate(inode, dentry, name, name_len, result);
  }

  // load children if not already loaded and compare children names to find a match
  if (!IS_IFLLDIR(inode)) {
    if (i_loaddir(inode, dentry) < 0) {
      DPRINTF("i_locate: failed to load directory\n");
      return -ENOENT;
    }
  }

  dentry_t *child = d_get_child(dentry, name, name_len);
  if (child == NULL) {
    return -ENOENT;
  }

  if (result != NULL)
    *result = child;
  return 0;
}

int i_loaddir(inode_t *inode, dentry_t *dentry) {
  ASSERT(I_OPS(inode)->i_loaddir != NULL);
  ASSERT(IS_IFDIR(inode));
  return I_OPS(inode)->i_loaddir(inode, dentry);
}

int i_create(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  super_block_t *sb = inode->sb;
  if (IS_RDONLY(sb)) {
    return -EROFS;
  }



  return -1;
}

int i_mknod(inode_t *inode, dentry_t *dentry, inode_t *dir, dev_t dev) {
  return -1;
}

int i_symlink(inode_t *inode, inode_t *dentry, inode_t *dir, const char *path, size_t len) {
  return -1;
}

int i_readlink(inode_t *inode, size_t buflen, char *buffer) {
  return -1;
}

int i_link(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  if (IS_RDONLY(inode->sb)) {
    return -EROFS;
  }

  return -1;
}

int i_unlink(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  super_block_t *sb = inode->sb;
  if (IS_RDONLY(sb)) {
    return -EROFS;
  }

  int res;
  S_LOCK(sb);
  {
    if ((res = I_OPS(inode)->i_unlink(inode, dentry, dir)) < 0) {
      S_UNLOCK(sb);
      return res;
    }

    if (dentry->inode != NULL) {
      i_unlink_dentry(inode, dentry);
    }

    if (inode->nlinks == 0) {
      // remove inode from superblock
      if ((res = S_OPS(sb)->sb_delete_inode(sb, inode)) < 0) {
        DPRINTF("i_unlink: failed to delete inode\n");
        // leave inode dirty but dont return error
      }
      // inode is unlinked so we should free it even if the above failed
      i_free(inode);
    } else if (IS_IDIRTY(inode)) {
      // update inode on disk
      if ((res = S_OPS(sb)->sb_write_inode(sb, inode)) < 0) {
        DPRINTF("i_unlink: failed to update inode\n");
        // leave inode dirty but dont return error
      }
    }
  }
  S_UNLOCK(sb);
  return 0;
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
