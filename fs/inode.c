//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <inode.h>
#include <device.h>
#include <dentry.h>
#include <process.h>
#include <thread.h>
#include <panic.h>
#include <string.h>
#include <murmur3.h>
#include <mm.h>

// allocates a new inode struct
// NOTE: this does not allocate an inode on disk
inode_t *i_alloc(ino_t ino, super_block_t *sb) {
  inode_t *inode = kmalloc(sizeof(inode_t));
  memset(inode, 0, sizeof(inode_t));
  inode->ino = ino;
  // inode->ctime =
  // inode->mtime =
  // inode->atime =
  rw_lock_init(&inode->lock);

  if (sb) {
    file_system_t *fs = sb->fs;
    inode->sb = sb;
    inode->ops = fs->inode_ops;
    inode->blkdev = sb->dev;
    // LIST_ADD(&sb->inodes, inode, inodes);
  }

  return inode;
}

//

/**
 * Creates a new inode associated with the given dentry.
 * Filesystems that want to support regular files must implement
 * the `create` inode operation. Implementations must allocate a
 * new inode, fill in the dentry and then attach the inode to the
 * dentry.
 * @param dir The directory.
 * @param dentry The empty dentry for the new inode.
 * @param mode Initial file mode.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_create(inode_t *dir, dentry_t *dentry, mode_t mode) {
  kassert(IS_IFDIR(dir->mode));
  if (!(dir->ops && dir->ops->create)) {
    ERRNO = ENOTSUP;
    return -1;
  }

  int result = dir->ops->create(dir, dentry, mode);
  if (result < 0) {
    return -1;
  }

  kassert(dentry->inode != NULL);
  d_add_child(LIST_FIRST(&dir->dentries), dentry);
  return 0;
}

/**
 * Locates a dentry in a given directory.
 * Filesystems do not have to implement the `lookup` inode operation
 * if all dentries are loaded in memory.
 * @param dir The directory.
 * @param name The name of the dentry to locate.
 * @return On success, the dentry or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
dentry_t *i_lookup(inode_t *dir, const char *name) {
  kassert(IS_IFDIR(dir->mode));
  if (dir->ops && dir->ops->lookup) {
    return dir->ops->lookup(dir, name, false);
  }

  uint32_t hash;
  size_t len = strlen(name);
  murmur_hash_x86_32(name, len, 0xDEADBEEF, &hash);

  dentry_t *parent = LIST_FIRST(&dir->dentries);
  dentry_t *dentry = LIST_FIRST(&parent->children);
  while (dentry && dentry->hash != hash) {
    dentry = LIST_NEXT(dentry, siblings);
  }

  if (dentry == NULL) {
    ERRNO = ENOENT;
  }
  return dentry;
}

/**
 * Creates a hard link.
 * Filesystems do not have to implement the `link` inode operation
 * unless it should be handled differently than default. Implementations
 * must attach the the inode from the dentry.
 * @param dir The directory in which to add the new link.
 * @param dentry The dentry to link to.
 * @param new_dentry The dentry of the new link.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_link(inode_t *dir, dentry_t *dentry, dentry_t *new_dentry) {
  kassert(IS_IFDIR(dir->mode));
  kassert(dentry->inode->blkdev == dir->blkdev);
  dentry_t *parent = LIST_FIRST(&dir->dentries);
  if (dir->ops && dir->ops->link) {
    int result = dir->ops->link(dir, dentry, new_dentry);
    if (result < 0) {
      return -1;
    }

    d_add_child(parent, dentry);
    return 0;
  }

  d_attach(new_dentry, dentry->inode);
  d_add_child(parent, new_dentry);
  return 0;
}

/**
 * Removes a hard link.
 * Filesystems do not have to implement the `unlink` inode operation
 * unless it should be handled differently than default. Implementations
 * must detach the the inode from the dentry.
 * @param dir The directory which contains the link.
 * @param dentry The dentry to unlink.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_unlink(inode_t *dir, dentry_t *dentry) {
  kassert(IS_IFDIR(dir->mode));
  dentry_t *parent = LIST_FIRST(&dir->dentries);
  if (dir->ops && dir->ops->unlink) {
    int result = dir->ops->unlink(dir, dentry);
    if (result < 0) {
      return -1;
    }

    d_remove_child(parent, dentry);
    return 0;
  }

  d_detach(dentry);
  d_remove_child(parent, dentry);
  return 0;
}

/**
 * Creates a symbolic link to a path.
 * Filesystems that want to support symbolic linkss must implement
 * the `symlink` inode operation. Implementations must allocate a
 * new inode, fill in the dentry and attach the inode to the dentry.
 * @param dir The directory in which to add the symlink.
 * @param dentry The dentry of the new symlink.
 * @param path The path which is symlinked.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_symlink(inode_t *dir, dentry_t *dentry, const char *path) {
  kassert(IS_IFDIR(dir->mode));
  if (!(dir->ops && dir->ops->symlink)) {
    ERRNO = ENOTSUP;
    return -1;
  }

  int result = dir->ops->symlink(dir, dentry, path);
  if (result < 0) {
    return -1;
  }

  dentry_t *parent = LIST_FIRST(&dir->dentries);
  d_add_child(parent, dentry);
  return 0;
}

/**
 * Creates a new directory.
 * Filesystems that want to support directories must implement
 * the `mkdir` inode operation. Implementations must allocate a
 * new inode, fill in the dentry and attach the inode to the dentry.
 * By default, two entries for "." and ".." are added to the new
 * directory, but implementations may return a value of 1 to prevent
 * the special entries from being added.
 * @param dir The directory in which to add the directory.
 * @param dentry The dentry of the new directory.
 * @param mode Initial directory mode.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode) {
  kassert(IS_IFDIR(dir->mode));
  if (!(dir->ops && dir->ops->mkdir)) {
    ERRNO = ENOTSUP;
    return -1;
  }

  dentry_t *parent = LIST_FIRST(&dir->dentries);
  dentry_t *pparent = LIST_FIRST(&parent->inode->dentries);
  int result = dir->ops->mkdir(dir, dentry, mode);
  if (result < 0) {
    return -1;
  }

  d_add_child(parent, dentry);
  if (result == 0) {
    dentry_t *dot = d_alloc(dentry, ".");
    if (i_link(dentry->inode, parent, dot) < 0) {
      d_destroy(dot);
      return -1;
    }

    dentry_t *dotdot = d_alloc(dentry, "..");
    if (i_link(dentry->inode, pparent, dotdot) < 0) {
      d_destroy(dotdot);
      return -1;
    }
  }
  return 0;
}

/**
 * Removes an empty directory.
 * Filesystems may implement the `rmdir` inode operation if they
 * so choose. Implementations must detach the inode from the dentry
 * and release the inode. The dentry should not be freed by the
 * implementation.
 * @param dir The parent directory.
 * @param dentry The directory to remove.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_rmdir(inode_t *dir, dentry_t *dentry) {
  kassert(IS_IFDIR(dir->mode));
  dentry_t *parent = LIST_FIRST(&dir->dentries);
  kassert(LIST_FIRST(&parent->children) == NULL);

  if (dir->ops && dir->ops->rmdir) {
    int result = dir->ops->rmdir(dir, dentry);
    if (result < 0) {
      return -1;
    }
  } else {
    d_detach(dentry);
    int result = dir->sb->ops->destroy_inode(dir->sb, dir);
    if (result < 0) {
      return -1;
    }
  }

  d_remove_child(parent, dentry);
  d_detach(dentry);
  return 0;
}

/**
 * Creates a special device file.
 * Filesystems that want to support special device files must
 * implement the `mknod` inode operation. Implementations must
 * allocate a new inode, fill in the dentry and then attach the
 * inode to the dentry.
 * @param dir The directory in which to add the file.
 * @param dentry The dentry of the new device file.
 * @param mode The initial mode.
 * @param dev The device identifier.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev) {
  kassert(IS_IFDIR(dir->mode));
  if (!(dir->ops && dir->ops->mknod)) {
    ERRNO = ENOTSUP;
    return -1;
  }

  int result = dir->ops->mknod(dir, dentry, mode, dev);
  if (result < 0) {
    return -1;
  }

  dentry_t *parent = LIST_FIRST(&dir->dentries);
  d_add_child(parent, dentry);
  dentry->inode->dev = dev;

  device_t *device = locate_device(dev);
  if (device && device->ops && device->ops->fill_inode) {
    device->ops->fill_inode(device, dentry->inode);
  }

  return 0;
}

/**
 * Renames a file.
 * Filesystems that want to support file renaming must implement
 * the `rename` inode operation. Implementations must allocate a
 * new inode, fill in the new dentry and then attach the inode to
 * the dentry. It should then detach the old inode from the old
 * dentry but it should not remove the old dentry.
 * @param old_dir The old parent directory.
 * @param old_dentry The old dentry.
 * @param new_dir The new parent directory.
 * @param new_dentry The new dentry.
 * @return On success, 0 or -1 on failure. If the result is
 *         -1, errno shall be set to the error code.
 */
int i_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry) {
  kassert(IS_IFDIR(old_dir->mode));
  kassert(IS_IFDIR(new_dir->mode));
  if (!(old_dir->ops && old_dir->ops->rename)) {
    ERRNO = ENOTSUP;
    return -1;
  }

  int result = old_dir->ops->rename(old_dir, old_dentry, new_dir, new_dentry);
  if (result < 0) {
    return -1;
  }

  dentry_t *old_parent = LIST_FIRST(&old_dir->dentries);
  dentry_t *new_parent = LIST_FIRST(&new_dir->dentries);
  d_add_child(new_parent, new_dentry);
  d_remove_child(old_parent, old_dentry);
  d_destroy(old_dentry);
  return 0;
}

/**
 * Reads a symbolic link.
 * Filesystems that want to support file symbolic links must implement
 * the `readlink` inode operation. Implementations must write the link
 * path into the given buffer and return the number of characters written.
 * @param dentry The symbolic link to read.
 * @param buffer The buffer.
 * @param buflen The length of the buffer.
 * @return On success, the number of characters written or -1 on failure.
 *         If the result is -1, errno shall be set to the error code.
 */
int i_readlink(dentry_t *dentry, char *buffer, int buflen) {
  kassert(IS_IFLNK(dentry->mode));
  if (!(dentry->inode->ops && dentry->inode->ops->readlink)) {
    ERRNO = ENOTSUP;
    return -1;
  }
  return dentry->inode->ops->readlink(dentry, buffer, buflen);
}

/**
 * Truncates an inode. This sets its size to zero, effectively deleting its
 * data without removing the node. Filesystems that are in-memory only do not
 * need to implement the `truncate` inode operation.
 * @param inode
 */
void i_truncate(inode_t *inode) {
  kassert(IS_IFREG(inode->mode));
  if (inode->ops && inode->ops->truncate) {
    inode->ops->truncate(inode);
    return;
  }

  if (inode->size == 0 || inode->pages == NULL) {
    return;
  }

  vfree_pages(inode->pages);
  inode->pages = NULL;
}
