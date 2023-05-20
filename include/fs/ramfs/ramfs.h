//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#ifndef FS_RAMFS_RAMFS_H
#define FS_RAMFS_RAMFS_H

#include <fs_types.h>
#include <mm_types.h>
#include <kio.h>

typedef enum ramfs_mem {
  RAMFS_MEM_HEAP,
  RAMFS_MEM_PAGE,
} ramfs_mem_t;

typedef struct ramfs_file {
  ramfs_mem_t type;
  size_t size;
  size_t capacity;
  union {
    void *heap;
    page_t *page;
  };
} ramfs_file_t;

// File API
ramfs_file_t *ramfs_alloc_file(size_t size);
ramfs_file_t *ramfs_alloc_file_type(ramfs_mem_t type, size_t size);
void ramfs_free_file(ramfs_file_t *file);
int ramfs_truncate_file(ramfs_file_t *file, size_t newsize);
ssize_t ramfs_read_file(ramfs_file_t *file, size_t off, kio_t *kio);
ssize_t ramfs_write_file(ramfs_file_t *file, size_t off, kio_t *kio);
int ramfs_map_file(ramfs_file_t *file, vm_mapping_t *vm);

// ramfs operations

// super.c
int ramfs_sb_mount(super_block_t *sb, dentry_t *mount);
int ramfs_sb_unmount(super_block_t *sb);
int ramfs_sb_write(super_block_t *sb);
int ramfs_sb_read_inode(super_block_t *sb, inode_t *inode);
int ramfs_sb_write_inode(super_block_t *sb, inode_t *inode);
int ramfs_sb_alloc_inode(super_block_t *sb, inode_t *inode);
int ramfs_sb_delete_inode(super_block_t *sb, inode_t *inode);
// inode.c
int ramfs_i_loaddir(inode_t *inode, dentry_t *dentry);
int ramfs_i_create(inode_t *inode, dentry_t *dentry, inode_t *dir);
int ramfs_i_mknod(inode_t *inode, dentry_t *dentry, inode_t *dir, dev_t dev);
int ramfs_i_symlink(inode_t *inode, dentry_t *dentry, inode_t *dir, const char *path, size_t len);
int ramfs_i_readlink(inode_t *inode, size_t buflen, char *buffer);
int ramfs_i_unlink(inode_t *inode, dentry_t *dentry, inode_t *dir);
int ramfs_i_mkdir(inode_t *inode, dentry_t *dentry, inode_t *dir);
int ramfs_i_rmdir(inode_t *inode, dentry_t *dentry, inode_t *dir);
int ramfs_i_rename(inode_t *inode, dentry_t *o_dentry, inode_t *o_dir, dentry_t *n_dentry, inode_t *n_dir);
// file.c
int ramfs_f_open(file_t *file);
int ramfs_f_close(file_t *file);
int ramfs_f_sync(file_t *file);
int ramfs_f_truncate(file_t *file, size_t len);
ssize_t ramfs_f_read(file_t *file, off_t off, kio_t *kio);
ssize_t ramfs_f_write(file_t *file, off_t off, kio_t *kio);
int ramfs_f_mmap(file_t *file, off_t off, vm_mapping_t *vm);

#endif
