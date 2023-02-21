//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <inode.h>
#include <mm.h>

#include <printf.h>
#include <string.h>
#include <panic.h>
#include <murmur3.h>

#define ASSERT(x) kassert(x)

#define MURMUR3_SEED 0xDEADBEEF

//
// MARK: Virtual API
//

inode_t *i_alloc_empty() {
  inode_t *inode = kmalloc(sizeof(inode_t));
  memset(inode, 0, sizeof(inode_t));
  mutex_init(&inode->lock, MUTEX_REENTRANT);
  rw_lock_init(&inode->data_lock);
  return inode;
}

void i_free(inode_t *inode) {
  memset(inode, 0, sizeof(inode_t));
  kfree(inode);
}

int i_add_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == NULL);
  I_LOCK(inode);
  D_LOCK(dentry);
  inode->nlinks++;
  LIST_ADD(&inode->links, dentry, list);
  dentry->inode = inode;
  dentry->ino = inode->ino;
  D_UNLOCK(dentry);
  I_UNLOCK(inode);
  return 0;
}

int i_remove_dentry(inode_t *inode, dentry_t *dentry) {
  ASSERT(dentry->inode == inode);
  I_LOCK(inode);
  D_LOCK(dentry);
  dentry->ino = 0;
  dentry->inode = NULL;
  LIST_REMOVE(&inode->links, dentry, list);
  inode->nlinks--;
  D_UNLOCK(dentry);
  I_UNLOCK(inode);
  return 0;
}

//
// MARK: Operations
//

dentry_t *i_locate(inode_t *inode, dentry_t *dentry, const char *name) {
  kassert(IS_IFDIR(inode));
  if (I_OPS(inode)->i_locate != NULL) {
    return I_OPS(inode)->i_locate(inode, dentry, name);
  }

  // default implementation
  if (i_loaddir(inode, dentry) < 0) {
    kprintf("i_locate: failed to load directory\n");
    // it's possible it has been loaded at some other point
    if (!IS_IFLLDIR(inode)) {
      return NULL;
    }
  }

  size_t name_len = strlen(name);
  LIST_FOR_IN(d, &dentry->d_children, list) {
    if (D_OPS(d)->d_compare != NULL && D_OPS(d)->d_compare(d, name, name_len) == 0) {
      return d;
    } else {
      uint64_t hash = 0;
      if (D_OPS(d)->d_hash) {
        D_OPS(d)->d_hash(name, name_len, &hash);
      } else {
        hash = murmur_hash64(name, name_len, MURMUR3_SEED);
      }

      if (hash == d->hash) {
        return d;
      }
    }
  }

  return NULL;
}

int i_loaddir(inode_t *inode, dentry_t *dentry) {
  kassert(I_OPS(inode)->i_loaddir != NULL);
  kassert(IS_IFDIR(inode));
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
