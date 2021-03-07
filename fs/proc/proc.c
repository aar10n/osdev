//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <proc/proc.h>
#include <printf.h>

fs_impl_t procfs_impl = {
  ramfs_mount, ramfs_unmount,
  //
  ramfs_locate, ramfs_create, ramfs_remove,
  procfs_link, procfs_unlink, ramfs_update,
  //
  ramfs_read, ramfs_write, ramfs_sync
};

fs_driver_t procfs_driver = {
  .name = "procfs", .impl = &procfs_impl,
};

//

dirent_t *procfs_link(fs_t *fs, inode_t *inode, inode_t *parent, char *name) {
  kprintf("[procfs] link\n");
  errno = ENOTSUP;
  return NULL;
}

int procfs_unlink(fs_t *fs, inode_t *inode, dirent_t *dirent) {
  kprintf("[procfs] unlink\n");
  errno = ENOTSUP;
  return -1;
}

//

// ssize_t ramfs_read(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf) {
//   kprintf("[ramfs] write\n");
//
//
//   if (offset >= file->size) {
//     return 0;
//   }
//
//   off_t available = file->size - offset;
//   ssize_t bytes = min(available, nbytes);
//
//
//   return bytes;
// }
//
// ssize_t ramfs_write(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf) {
//   kprintf("[procfs] write\n");
//   ramfs_file_t *file = ramfs_get_file(inode);
//   if (file == NULL) {
//     ramfs_backing_mem_t backing = (offset + nbytes) > (PAGE_SIZE / 4) ?
//                                   RAMFS_PAGE_BACKED : RAMFS_HEAP_BACKED;
//
//     file = ramfs_alloc_file(inode, backing, nbytes);
//   }
//
//   if (offset >= file->size) {
//     ramfs_resize_file(inode, file->size + align(offset - file->size, PAGE_SIZE));
//   }
//
//   if (offset + nbytes > file->size) {
//     size_t new_size = file->size + align(nbytes, PAGE_SIZE);
//     ramfs_resize_file(inode, new_size);
//   }
//
//   void *mem = (void *)((uintptr_t) ramfs_get_file_backing(file) + offset);
//   memcpy(mem, buf, nbytes);
//   return nbytes;
// }
