//
// Created by Aaron Gill-Braun on 2021-07-17.
//

#include <dentry.h>
#include <inode.h>
#include <panic.h>
#include <murmur3.h>
#include <thread.h>

// allocates a new dentry struct
dentry_t *d_alloc(dentry_t *parent, const char *name) {
  dentry_t *dentry = kmalloc(sizeof(dentry_t));
  memset(dentry, 0, sizeof(dentry_t));
  if (parent != NULL) {
    kassert(IS_IFDIR(parent->mode));
    dentry->parent = parent;
  }

  size_t len = min(strlen(name), MAX_FILE_NAME - 1);
  memcpy(dentry->name, name, len);
  murmur_hash_x86_32(dentry->name, len, 0xDEADBEEF, &dentry->hash);
  return dentry;
}

// attaches an inode to the given dentry
void d_attach(dentry_t *dentry, inode_t *inode) {
  kassert(dentry->inode == NULL);
  kassert(!IS_LOADED(dentry->mode));

  dentry->ino = inode->ino;
  dentry->mode = (inode->mode & (I_TYPE_MASK | I_PERM_MASK)) | S_ISLDD;
  dentry->ops = inode->sb->fs->dentry_ops;
  dentry->inode = inode;

  LIST_ADD(&inode->dentries, dentry, dentries);
  inode->nlink++;
}

// detaches the inode from the given dentry
void d_detach(dentry_t *dentry) {
  kassert(dentry->inode != NULL);
  kassert(IS_LOADED(dentry->mode));

  inode_t *inode = dentry->inode;
  inode->nlink--;
  LIST_REMOVE(&inode->dentries, dentry, dentries);
  dentry->inode = NULL;
}

// sync a dentry and inode
void d_sync(dentry_t *dentry) {
  inode_t *inode = dentry->inode;
  dentry->ino = inode->ino;
  dentry->mode = inode->mode;
}

void d_add_child(dentry_t *parent, dentry_t *child) {
  kassert(IS_IFDIR(parent->mode));
  child->parent = parent;
  LIST_ADD(&parent->children, child, siblings);
}

void d_remove_child(dentry_t *parent, dentry_t *child) {
  kassert(IS_IFDIR(parent->mode));
  child->parent = NULL;
  LIST_REMOVE(&parent->children, child, siblings);
}

int d_populate_dir(dentry_t *dir) {
  dentry_t *dot = d_alloc(dir, ".");
  if (i_link(dir->inode, dir, dot) < 0) {
    d_destroy(dot);
    return -1;
  }

  dentry_t *dotdot = d_alloc(dir, "..");
  if (i_link(dir->inode, dir->parent, dotdot) < 0) {
    d_destroy(dotdot);
    return -1;
  }
  return 0;
}

void d_destroy(dentry_t *dentry) {
  kassert(dentry->inode == NULL);
  kfree(dentry);
}

