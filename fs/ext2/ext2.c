//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#include <ext2/ext2.h>
#include <dentry.h>
#include <super.h>
#include <inode.h>
#include <thread.h>
#include <panic.h>
#include <string.h>

inode_t *ext2_alloc_inode(super_block_t *sb);
int ext2_destroy_inode(super_block_t *sb, inode_t *inode);
int ext2_read_inode(super_block_t *sb, inode_t *inode);
int ext2_write_inode(super_block_t *sb, inode_t *inode);

// int ext2_create(inode_t *dir, dentry_t *dentry, mode_t mode);
dentry_t *ext2_lookup(inode_t *dir, const char *name, bool filldir);
// int ext2_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev);
// int ext2_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode);
// int ext2_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry);

int ext2_open(file_t *file, dentry_t *dentry);
int ext2_flush(file_t *file);
ssize_t ext2_read(file_t *file, char *buf, size_t count, off_t *offset);
// ssize_t ext2_write(file_t *file, const char *buf, size_t count, off_t *offset);
int ext2_readdir(file_t *file, dentry_t *dirent, bool fill);

super_block_ops_t ext2_super_ops = {
  ext2_alloc_inode,
  ext2_destroy_inode,
  ext2_read_inode,
  ext2_write_inode,
};

inode_ops_t ext2_inode_ops = {
  .lookup = ext2_lookup,
};

file_ops_t ext2_file_ops = {
  .open = ext2_open,
  .flush = ext2_flush,
  .read = ext2_read,
  .readdir = ext2_readdir,
};

dentry_ops_t ext2_dentry_ops = {};

//

super_block_t *ext2_mount(file_system_t *fs, dev_t devid, blkdev_t *dev, dentry_t *mount) {
  ext2_super_t *esb = blkdev_read(dev, 2, 2);
  if (esb == NULL) {
    return NULL;
  }

  if (esb->s_magic != EXT2_SUPER_MAGIC) {
    ERRNO = EINVAL;
    return NULL;
  }

  uint32_t blocksz = 1024 << esb->s_log_block_size;
  uint32_t group_count = 1 + (esb->s_blocks_count - 1) / esb->s_blocks_per_group;
  // 1
  // 1kib -> (1) 1024 + 1024 = 4
  // 4kib -> (0) 4096 = 8
  uint32_t bgdt_off = (esb->s_first_data_block * blocksz) + blocksz;
  ext2_bg_desc_t *bgdt = blkdev_read(dev, SIZE_TO_SECS(bgdt_off), EXT2_BLK(blocksz, 1));
  if (bgdt == NULL) {
    return NULL;
  }

  ext2_data_t *ext2 = kmalloc(sizeof(ext2_data_t));
  ext2->sb = esb;
  ext2->bgdt = bgdt;
  ext2->bg_count = group_count;

  super_block_t *sb = kmalloc(sizeof(super_block_t));
  memset(sb, 0, sizeof(super_block_t));
  memcpy(sb->id, esb->s_volume_name, 16);
  sb->flags = FS_READONLY;
  sb->blksize = blocksz;
  sb->dev = dev;
  sb->devid = devid;
  sb->fs = fs;
  sb->ops = fs->sb_ops;
  sb->root = mount;
  sb->data = ext2;
  sb->inode_cache = create_rb_tree();

  inode_t *inode = i_alloc(EXT2_ROOT_INO, sb);
  if (inode == NULL) {
    panic("failed to mount");
  }

  inode->mode = S_IFDIR;
  inode->sb = sb;
  d_attach(mount, inode);

  if (ext2_read_inode(sb, inode) < 0) {
    panic("failed to read root");
  }
  rb_tree_insert(sb->inode_cache, inode->ino, inode);

  dentry_t *dot = ext2_lookup(inode, ".", true);
  dentry_t *dotdot = ext2_lookup(inode, "..", false);
  if (dot == NULL || dotdot == NULL) {
    panic("failed to load root");
  }
  inode->mode = S_IFDIR | S_ISFLL;
  dot->parent = mount;
  dot->inode = inode;
  dotdot->parent = mount;
  dotdot->inode = mount->parent->inode;
  return sb;
}

//

file_system_t ext2_file_system = {
  .name = "ext2",
  .flags = 0,
  .mount = ext2_mount,
  .sb_ops = &ext2_super_ops,
  .inode_ops = &ext2_inode_ops,
  .file_ops = &ext2_file_ops,
  .dentry_ops = &ext2_dentry_ops,
};


void ext2_init() {
  if (fs_register(&ext2_file_system) < 0) {
    panic("failed to register");
  }
}

