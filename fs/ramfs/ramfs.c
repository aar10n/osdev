//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <ramfs/ramfs.h>
#include <super.h>
#include <dentry.h>
#include <panic.h>

super_block_t *ramfs_mount(file_system_t *fs, blkdev_t *dev, dentry_t *mount) {
  ramfs_super_t *rsb = kmalloc(sizeof(ramfs_super_t));
  bitmap_init(&rsb->inodes, RAMFS_MAX_FILES);
  spin_init(&rsb->lock);

  super_block_t *sb = kmalloc(sizeof(super_block_t));
  sb->flags = 0;
  sb->blksize = PAGE_SIZE;
  sb->dev = dev;
  sb->fs = fs;
  sb->ops = fs->sb_ops;
  sb->root = mount;
  sb->data = rsb;

  // handle special case on root mount
  if (mount->inode == NULL) {
    inode_t *root = sb_alloc_inode(sb);
    root->mode = S_IFMNT | S_IFDIR;
    root->sb = sb;
    d_attach(mount, root);
  }
  return sb;
}

//

file_system_t ramfs_file_system = {
  .name = "ramfs",
  .flags = FS_NO_ROOT,
  .mount = ramfs_mount,
};

void ramfs_init() {
  ramfs_file_system.sb_ops = ramfs_super_ops;
  ramfs_file_system.inode_ops = ramfs_inode_ops;
  ramfs_file_system.dentry_ops = ramfs_dentry_ops;
  ramfs_file_system.file_ops = ramfs_file_ops;

  if (fs_register(&ramfs_file_system) < 0) {
    panic("failed to register");
  }
}
