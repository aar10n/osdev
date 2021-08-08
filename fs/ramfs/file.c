//
// Created by Aaron Gill-Braun on 2021-07-17.
//

#include <ramfs/ramfs.h>


int ramfs_open(file_t *file, dentry_t *dentry) {
  inode_t *inode = file->dentry->inode;
  if (inode->pages == NULL && !IS_IFDIR(file->mode)) {
    inode->pages = alloc_page(PE_WRITE);
    inode->blocks = 1;
  } else if (inode->pages && !inode->pages->flags.present) {
    map_pages(inode->pages);
  }
  return 0;
}

int ramfs_flush(file_t *file) {
  inode_t *inode = file->dentry->inode;
  if (inode->pages != NULL) {
    unmap_pages(inode->pages);
  }
  return 0;
}

ssize_t ramfs_read(file_t *file, char *buf, size_t count, off_t *offset) {
  inode_t *inode = file->dentry->inode;
  if (inode->size == 0 || file->pos >= inode->size) {
    return 0;
  }

  ssize_t len = min(*offset + count, inode->size - *offset);
  char *data = (void *) inode->pages->addr;
  memcpy(buf, data, len);
  *offset += len;
  return len;
}

ssize_t ramfs_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  inode_t *inode = file->dentry->inode;
  size_t realsz = inode->blksize * inode->blocks;
  size_t free = realsz - inode->size;

  if (free < count) {
    size_t remaining = count - free;
    inode->blocks += SIZE_TO_PAGES(remaining);
    page_t *pages = alloc_pages(SIZE_TO_PAGES(remaining), PE_WRITE);
    if (inode->pages) {
      unmap_pages(inode->pages);
      inode->pages->next = pages;
    } else {
      inode->pages = pages;
    }
    map_pages(inode->pages);
  }

  char *data = (void *) inode->pages->addr;
  memcpy(data + *offset, buf, count);
  *offset += count;
  return count;
}

//

file_ops_t file_ops = {
  .open = ramfs_open,
  .flush = ramfs_flush,
  .read = ramfs_read,
  .write = ramfs_write,
};

file_ops_t *ramfs_file_ops = &file_ops;
