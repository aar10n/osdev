//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <ramfs/ramfs.h>
#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>

fs_impl_t ramfs_impl = {
  ramfs_mount, ramfs_unmount,
  //
  ramfs_locate, ramfs_create, ramfs_remove,
  ramfs_link, ramfs_unlink, ramfs_update,
  //
  ramfs_read, ramfs_write, ramfs_sync
};

fs_driver_t ramfs_driver = {
  .name = "ramfs", .impl = &ramfs_impl,
};

//

inode_t *ramfs_alloc_inode(ramfs_t *ramfs) {
  bitmap_t *bmp = &ramfs->free_inodes;
  index_t index = bitmap_get_free(bmp);
  if (index == -1) {
    return NULL;
  }

  bitmap_set(bmp, index);
  inode_t *inode = &(ramfs->inodes[index]);
  inode->ino = index;
  return inode;
}

void ramfs_free_inode(ramfs_t *ramfs, inode_t *inode) {
  bitmap_t *bmp = &ramfs->free_inodes;
  index_t index = inode->ino;
  bitmap_clear(bmp, index);
}

inode_t *ramfs_get_inode(ramfs_t *ramfs, ino_t ino) {
  bitmap_t *bmp = &ramfs->free_inodes;
  bool is_set = bitmap_get(bmp, ino);
  if (!is_set) {
    return NULL;
  }

  return &(ramfs->inodes[ino]);
}

//

ramfs_file_t *ramfs_get_file(inode_t *inode) {
  ramfs_file_t *file = (void *) inode->data;
  if (file->mem_type == RAMFS_PAGE_BACKED) {
    page_t *page = file->mem;
    return (void *) page->addr;
  }
  return file;
}

ramfs_file_t *ramfs_alloc_file(inode_t *inode, ramfs_backing_mem_t backing, size_t size) {
  ramfs_file_t *file = kmalloc(sizeof(ramfs_file_t));
  file->mem_type = backing;

  if (backing == RAMFS_PAGE_BACKED) {
    size = align(size, PAGE_SIZE);
    page_t *pages = alloc_pages(SIZE_TO_PAGES(size), PE_WRITE);
    void *mem = vm_map_page(pages);
    file->mem = mem;
    file->size = size;
    file->used = 0;
  } else {
    file->mem = kmalloc(size);
    file->size = align(size, CHUNK_MIN_SIZE);
    file->used = 0;
  }

  inode->data = file;
  return file;
}

void ramfs_resize_file(inode_t *inode, size_t new_size) {
  ramfs_file_t *file = inode->data;

  if (file->size < new_size) {
    // optimization - free unneeded memory
    file->size = new_size;
  } else if (file->size == new_size) {
    return;
  }

  if (file->mem_type == RAMFS_PAGE_BACKED) {
    new_size = align(new_size, PAGE_SIZE);
    size_t size_diff = new_size - file->size;

    page_t *page = file->mem;
    vm_unmap_page(page);

    page_t *new_pages = alloc_pages(SIZE_TO_PAGES(size_diff), PE_WRITE);

    page_t *last = page;
    while (last->next) {
      last = last->next;
    }
    last->next = new_pages;

    void *mem = vm_map_page(page);
    file->mem = mem;
    file->size = new_size;
  } else {
    new_size = align(new_size, CHUNK_MIN_SIZE);
    void *mem = krealloc(file->mem, new_size);
    file->mem = mem;
    file->size = new_size;
  }
}

void ramfs_free_file(inode_t *inode) {
  ramfs_file_t *file = (void *) inode->data;
  if (file->mem_type == RAMFS_PAGE_BACKED) {
    page_t *page = file->mem;
    vm_unmap_page(page);
    free_page(page);
  } else {
    kfree(file->mem);
    kfree(file);
  }
  inode->data = NULL;
}

//

dirent_t *ramfs_add_dirent(inode_t *parent, ino_t ino, char *name) {
  ramfs_file_t *file = ramfs_get_file(parent);
  size_t remaining = file->size - file->used;
  if (remaining < sizeof(dirent_t)) {
    ramfs_resize_file(parent, file->size + 1024);
  }

  dirent_t *dir = file->mem;
  while (dir->name[0] != 0) {
    dir++;
  }

  dir->inode = ino;
  memcpy(dir->name, name, MAX_FILE_NAME);

  file->used += sizeof(dirent_t);
  return dir;
}

int ramfs_remove_dirent(inode_t *parent, dirent_t *dirent) {
  ramfs_file_t *file = ramfs_get_file(parent);

  memset(dirent, 0, sizeof(dirent_t));
  file->used -= sizeof(dirent_t);
  return 0;
}

//

fs_t *ramfs_mount(dev_t dev, fs_node_t *mount) {
  // allocate some space for the filesystem
  page_t *pages = alloc_pages(1, PE_2MB_SIZE | PE_WRITE);
  void *mem = vm_map_page(pages);

  // filesystem data
  size_t max_inodes = PAGE_SIZE_2MB / align(sizeof(inode_t), 8);
  size_t size = sizeof(uint64_t) * (max_inodes / 64);
  uint64_t *map = kmalloc(size);

  ramfs_t *ramfs = kmalloc(sizeof(ramfs_t));
  ramfs->free_inodes.map = map;
  ramfs->free_inodes.free = max_inodes;
  ramfs->free_inodes.size = size;
  ramfs->free_inodes.used = 0;
  ramfs->inodes = mem;
  ramfs->pages = pages;
  ramfs->max_inodes = max_inodes;

  // root node
  fs_node_t *root = __create_fs_node();
  inode_t *inode = ramfs_alloc_inode(ramfs);
  __init_inode(inode, inode->ino, dev, S_IFDIR | S_IFMNT);

  root->inode = inode->ino;
  root->mode = inode->mode;
  root->name = "/";

  // filesystem struct
  fs_t *fs = kmalloc(sizeof(fs_t));
  fs->dev = dev;
  fs->root = root;
  fs->mount = mount;
  fs->data = ramfs;
  fs->impl = &ramfs_impl;

  root->fs = fs;
  return fs;
}

int ramfs_unmount(fs_t *fs, fs_node_t *mount) {
  // in-memory filesystems dont support being unmounted
  errno = ENOTSUP;
  return -1;
}

//

inode_t *ramfs_locate(fs_t *fs, ino_t ino) {
  ramfs_t *ramfs = fs->data;
  if (ino > ramfs->max_inodes) {
    errno = EINVAL;
    return NULL;
  }

  inode_t *inode = ramfs_get_inode(ramfs, ino);
  if (inode == NULL) {
    errno = ENOENT;
    return NULL;
  }
  return inode;
}

inode_t *ramfs_create(fs_t *fs, mode_t mode) {
  ramfs_t *ramfs = fs->data;
  inode_t *inode = ramfs_alloc_inode(ramfs);
  if (inode == NULL) {
    errno = ENOSPC;
    return NULL;
  }

  inode->mode = mode;
  return inode;
}

int ramfs_remove(fs_t *fs, inode_t *inode) {
  ramfs_t *ramfs = fs->data;

  // free file data memory
  ramfs_free_file(inode);
  ramfs_free_inode(ramfs, inode);
  return 0;
}

dirent_t *ramfs_link(fs_t *fs, inode_t *inode, inode_t *parent, char *name) {
  ramfs_t *ramfs = fs->data;
  dirent_t *dirent = ramfs_add_dirent(parent, inode->ino, name);
  inode->nlink++;
  return dirent;
}

int ramfs_unlink(fs_t *fs, inode_t *inode, dirent_t *dirent) {
  ramfs_t *ramfs = fs->data;
  inode_t *parent = ramfs_get_inode(ramfs, dirent->inode);
  ramfs_remove_dirent(parent, dirent);
  inode->nlink--;

  if (inode->nlink == 0) {
    return ramfs_remove(fs, inode);
  }
  return 0;
}

int ramfs_update(fs_t *fs, inode_t *inode) {
  // updates are already made to the inode and
  // nothing needs to be persisted to disk
  return 0;
}

//

ssize_t ramfs_read(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf) {
  ramfs_file_t *file = ramfs_get_file(inode);

  if (offset >= file->size) {
    errno = EOVERFLOW;
    return -1;
  }

  off_t available = file->size - offset;
  ssize_t bytes = min(available, nbytes);

  void *mem = (void *)((uintptr_t) file->mem + offset);
  memcpy(buf, mem, bytes);
  return bytes;
}

ssize_t ramfs_write(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf) {
  ramfs_file_t *file = ramfs_get_file(inode);

  if (offset >= file->size) {
    errno = EOVERFLOW;
    return -1;
  }

  if (offset + nbytes > file->size) {
    size_t new_size = file->size + align(nbytes, PAGE_SIZE);
    ramfs_resize_file(inode, new_size);
  }

  void *mem = (void *)((uintptr_t) file->mem + offset);
  memcpy(mem, buf, nbytes);
  return nbytes;
}

int ramfs_sync(fs_t *fs) {
  // everything is in memory so we dont actually
  // have to sync anything
  return 0;
}
