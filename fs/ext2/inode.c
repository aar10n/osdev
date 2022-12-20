//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#include <ext2/ext2.h>
#include <dentry.h>
#include <thread.h>
#include <string.h>
#include <panic.h>

#define ext2sb(super) ((ext2_data_t *)((super)->data))

mode_t ext2_type_to_mode(uint16_t type) {
  switch (type) {
    case EXT2_FT_REG_FILE:
      return S_IFREG;
    case EXT2_FT_DIR:
      return S_IFDIR;
    case EXT2_FT_CHRDEV:
      return S_IFCHR;
    case EXT2_FT_BLKDEV:
      return S_IFBLK;
    case EXT2_FT_FIFO:
      return S_IFIFO;
    case EXT2_FT_SOCK:
      return S_IFSOCK;
    case EXT2_FT_SYMLINK:
      return S_IFLNK;
    default:
      return 0;
  }
}

//

int ext2_create(inode_t *dir, dentry_t *dentry, mode_t mode) {
  // ext2sb(dir->sb)
  ERRNO = ENOTSUP;
  return -1;
}

dentry_t *ext2_lookup(inode_t *dir, const char *name, bool filldir) {
  dentry_t *parent = LIST_FIRST(&dir->dentries);
  dentry_t *dentry = NULL;
  kassert(parent != NULL);

  // check existing dentries
  dentry_t *child = LIST_FIRST(&parent->children);
  while (child) {
    if (strcmp(child->name, name) == 0) {
      // filldir == true && IS_FULL(dir->mode) -> true
      // filldir == true && !IS_FULL(dir->mode) -> false
      // !filldir -> false
      //
      // filldir && !IS_FULL(dir->mode)
      //
      if (IS_FULL(dir->mode) || !filldir) {
        return child;
      }
      // continue loading children
      dentry = child;
      break;
    }
    child = LIST_NEXT(child, siblings);
  }

  ext2_load_chunk_t *chunk = dir->data;
  void *buf = NULL;
  super_block_t *sb = parent->inode->sb;

  // load new dentries
 outer:
  buf = EXT2_READ(dir->sb, chunk->start, chunk->len);
  if (buf == NULL) {
    // free chain
    panic("panic");
    ERRNO = EFAILED;
    return NULL;
  }

  ext2_ll_dentry_t *dent = buf;
  while (dent->inode != 0 || dent->file_type != EXT2_FT_UNKNOWN) {
    char n[dent->name_len + 1];
    memcpy(n, dent->name, dent->name_len);
    n[dent->name_len] = '\0';

    dentry_t *d = d_alloc(LIST_FIRST(&dir->dentries), n);
    d->ino = dent->inode;
    d->mode = ext2_type_to_mode(dent->file_type);
    d_add_child(parent, d);

    if (sb->inode_cache) {
      rb_node_t *node = rb_tree_find(sb->inode_cache, d->ino);
      if (node != NULL) {
        d_attach(d, node->data);
        d->mode |= S_ISLDD;
      }
    }

    if (strcmp(name, n) == 0) {
      if (!filldir) {
        return d;
      }
      dentry = d;
    }

    dent = offset_ptr(dent, dent->rec_len);
    if (((void *) dent) >= offset_ptr(buf, dir->sb->blksize)) {
      chunk = LIST_NEXT(chunk, chunks);
      if (chunk == NULL) {
        break;
      }
      goto outer;
    }
  }

  if (filldir)
    parent->mode |= S_ISFLL;
  return dentry;
}

int ext2_link(inode_t *dir, dentry_t *old_dentry, dentry_t *dentry) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_unlink(inode_t *dir, dentry_t *dentry) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_symlink(inode_t *dir, dentry_t *dentry, const char *path) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_rmdir(inode_t *dir, dentry_t *dentry) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry) {
  ERRNO = ENOTSUP;
  return -1;
}

int ext2_readlink(dentry_t *dentry, char *buffer, int buflen) {
  ERRNO = ENOTSUP;
  return -1;
}

void ext2_truncate(inode_t *inode) {
  ERRNO = ENOTSUP;
}
