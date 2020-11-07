//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_RAMFS_RAMFS_H
#define FS_RAMFS_RAMFS_H

#include <base.h>
#include <fs.h>
#include <bitmap.h>

//
// Generic in-memory pseudo filesystem
//

typedef struct page page_t;

typedef enum {
  RAMFS_PAGE_BACKED,
  RAMFS_HEAP_BACKED,
} ramfs_backing_mem_t;

typedef struct {
  ramfs_backing_mem_t mem_type;
  void *mem;
  size_t size;
  size_t used;
} ramfs_file_t;

typedef struct {
  inode_t *inodes;
  bitmap_t free_inodes;
  page_t *pages;
  size_t max_inodes;
} ramfs_t;

extern fs_driver_t ramfs_driver;


fs_t *ramfs_mount(fs_device_t *device, fs_node_t *mount);
int ramfs_unmount(fs_t *fs, fs_node_t *mount);

inode_t *ramfs_locate(fs_t *fs, ino_t ino);
inode_t *ramfs_create(fs_t *fs, mode_t mode);
int ramfs_remove(fs_t *fs, inode_t *inode);
dirent_t *ramfs_link(fs_t *fs, inode_t *inode, inode_t *parent, char *name);
int ramfs_unlink(fs_t *fs, inode_t *inode, dirent_t *dirent);
int ramfs_update(fs_t *fs, inode_t *inode);

ssize_t ramfs_read(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
ssize_t ramfs_write(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
int ramfs_sync(fs_t *fs);

#endif
