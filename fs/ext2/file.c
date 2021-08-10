//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#include <ext2/ext2.h>
#include <super.h>
#include <thread.h>

dentry_t *ext2_lookup(inode_t *dir, const char *name, bool filldir);

//

int ext2_open(file_t *file, dentry_t *dentry) {
  inode_t *inode = file->dentry->inode;
  if (file->flags & O_RDWR || file->flags & O_WRONLY) {
    ERRNO = EROFS;
    return -1;
  } else if (inode->size == 0 || IS_FULL(inode->mode)) {
    return 0;
  }

  page_t *pages = alloc_pages(SIZE_TO_PAGES(dentry->inode->size), PE_WRITE);
  inode->pages = pages;
  return 0;
}

int ext2_flush(file_t *file) {
  if (file->dentry->inode->pages) {
    free_pages(file->dentry->inode->pages);
  }
  return 0;
}

ssize_t ext2_read(file_t *file, char *buf, size_t count, off_t *offset) {
  inode_t *inode = file->dentry->inode;
  void *addr = (void *) inode->pages->addr;
  if (!IS_FULL(inode->mode)) {
    uint64_t off = 0;

    ext2_load_chunk_t *chunk = inode->data;
    while (chunk) {
      int result = EXT2_READBUF(inode->sb, chunk->start, chunk->len, addr + off);
      if (result < 0) {
        return -1;
      }
      off += result;
      chunk = LIST_NEXT(chunk, chunks);
    }
    inode->mode |= S_ISFLL;
  }

  size_t len = min(*offset + count, inode->size - *offset);
  memcpy(buf, addr + *offset, len);
  *offset += len;
  return len;
}

ssize_t ext2_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  ERRNO = EROFS;
  return -1;
}

// off_t ext2_lseek(file_t *file, off_t offset, int origin) {}

int ext2_readdir(file_t *file, dentry_t *dirent, bool fill) {
  if (file->pos == 0 && !IS_FULL(file->dentry->mode)) {
    if (ext2_lookup(file->dentry->inode, ".", true) == NULL) {
      return -1;
    }
  }

  dentry_t *next;
  if (file->pos == 0) {
    next = LIST_FIRST(&file->dentry->children);
  } else {
    next = LIST_NEXT(file->dentry, siblings);
  }

  if (!next) {
    return -1;
  }

  file->pos++;
  file->dentry = next;

  sb_read_inode(next);
  memcpy(dirent, next, sizeof(dentry_t));
  return 0;
}
