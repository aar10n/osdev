//
// Created by Aaron Gill-Braun on 2021-07-17.
//

#include <ramfs/ramfs.h>
#include <string.h>

int ramfs_open(file_t *file, dentry_t *dentry) {
  inode_t *inode = file->dentry->inode;
  if (inode->pages == NULL && !IS_IFDIR(file->mode)) {
    inode->pages = valloc_page(PG_WRITE);
    inode->blocks = 1;
  } else if (inode->pages && !IS_PG_MAPPED(inode->pages->flags)) {
    _vmap_pages(inode->pages);
  }
  return 0;
}

int ramfs_flush(file_t *file) {
  inode_t *inode = file->dentry->inode;
  if (inode->pages != NULL) {
    vfree_pages(inode->pages);
  }
  return 0;
}

ssize_t ramfs_read(file_t *file, char *buf, size_t count, off_t *offset) {
  inode_t *inode = file->dentry->inode;
  if (inode->size == 0 || file->pos >= inode->size) {
    return 0;
  }

  ssize_t len = min(*offset + count, inode->size - *offset);
  char *data = (void *) PAGE_VIRT_ADDR(inode->pages);
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
    page_t *pages = _alloc_pages(SIZE_TO_PAGES(remaining), PG_WRITE);
    if (inode->pages) {
      _vunmap_pages(inode->pages);
      page_t *last = SLIST_GET_LAST(inode->pages, next);
      SLIST_ADD_EL(last, pages, next);
    } else {
      inode->pages = pages;
    }
    _vmap_pages(pages);
  }

  char *data = (void *) PAGE_VIRT_ADDR(inode->pages);
  memcpy(data + *offset, buf, count);
  *offset += count;
  return count;
}

//

static file_ops_t file_ops = {
  .open = ramfs_open,
  .flush = ramfs_flush,
  .read = ramfs_read,
  .write = ramfs_write,
};

file_ops_t *ramfs_file_ops = &file_ops;
