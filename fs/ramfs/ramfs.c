//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <ramfs/ramfs.h>
#include <super.h>
#include <dentry.h>
#include <panic.h>

inode_t *ramfs_alloc_inode(super_block_t *sb);
int ramfs_destroy_inode(super_block_t *sb, inode_t *inode);
int ramfs_read_inode(super_block_t *sb, inode_t *inode);
int ramfs_write_inode(super_block_t *sb, inode_t *inode);

int ramfs_create(inode_t *dir, dentry_t *dentry, mode_t mode);
int ramfs_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev);
int ramfs_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode);
int ramfs_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry);

int ramfs_open(file_t *file, dentry_t *dentry);
int ramfs_flush(file_t *file);
ssize_t ramfs_read(file_t *file, char *buf, size_t count, off_t *offset);
ssize_t ramfs_write(file_t *file, const char *buf, size_t count, off_t *offset);

super_block_ops_t ramfs_super_ops = {
  ramfs_alloc_inode,
  ramfs_destroy_inode,
  ramfs_read_inode,
  ramfs_write_inode,
};

inode_ops_t ramfs_inode_ops = {
  .create = ramfs_create,
  .mknod = ramfs_mknod,
  .mkdir = ramfs_mkdir,
  .rename = ramfs_rename,
};

file_ops_t ramfs_file_ops = {
  .open = ramfs_open,
  .flush = ramfs_flush,
  .read = ramfs_read,
  .write = ramfs_write,
};

dentry_ops_t ramfs_dentry_ops = {};

//

super_block_t *ramfs_mount(file_system_t *fs, blkdev_t *dev, dentry_t *mount) {
  ramfs_super_t *rsb = kmalloc(sizeof(ramfs_super_t));
  bitmap_init(&rsb->inodes, RAMFS_MAX_FILES);
  spin_init(&rsb->lock);

  super_block_t *sb = kmalloc(sizeof(super_block_t));
  sb->flags = 0;
  sb->blksize = PAGE_SIZE;
  sb->root = mount;
  sb->dev = dev;
  sb->ops = fs->sb_ops;
  sb->fs = fs;
  sb->data = rsb;

  inode_t *root = sb_alloc_inode(sb);
  root->mode = S_IFMNT | S_IFDIR;
  root->sb = sb;
  d_attach(mount, root);
  return sb;
}

//

file_system_t ramfs_file_system = {
  .name = "ramfs",
  .flags = 0,
  .mount = ramfs_mount,
  .sb_ops = &ramfs_super_ops,
  .inode_ops = &ramfs_inode_ops,
  .file_ops = &ramfs_file_ops,
  .dentry_ops = &ramfs_dentry_ops,
};


void ramfs_init() {
  if (fs_register(&ramfs_file_system) < 0) {
    panic("failed to register");
  }
}
