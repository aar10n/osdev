//
// Created by Aaron Gill-Braun on 2020-09-08.
//

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "common.h"
#include "fs.h"

#define get_file(offset) \
  ((initrd_file_t *) (base + offset))

#define get_dirent(offset) \
  ((initrd_dirent_t *) (base + offset))

#define next_dirent(dirent) \
  ((initrd_dirent_t *) (((uint8_t *) dirent) + sizeof(initrd_dirent_t)))

#define get_data(file) \
  ((uint8_t *) (base + (file)->offset))

static uint8_t *base = NULL;
static fs_node_t **nodes = NULL;

//

void build_tree(fs_node_t *parent, initrd_file_t *first) {
  fs_node_t *node = NULL;
  initrd_dirent_t *dirent = get_dirent(first->dirent);
  while (true) {
    initrd_file_t *file = get_file(dirent->offset);

    fs_node_t *new_node;
    if (file->flags & FILE_REGULAR) {
      uint8_t *buf = malloc(file->length);
      memcpy(buf, get_data(file), file->length);
      new_node = create_file(dirent->name, parent, file->length, buf);
    } else if (file->flags & FILE_DIRECTORY) {
      new_node = create_directory(dirent->name, parent, false);
      initrd_dirent_t *child = get_dirent(file->offset);
      // recursively construct the tree
      build_tree(new_node, get_file(child->offset));
    } else if (file->flags & FILE_SYMLINK) {
      fs_node_t *link = nodes[get_file(file->offset)->id];
      new_node = create_symlink(dirent->name, parent, link);
    }

    if (node) {
      node->next = new_node;
      new_node->prev = node;
    } else {
      parent->dir.first = new_node;
    }

    if (dirent->flags & DIR_LAST_ENTRY) {
      break;
    }

    node = new_node;
    dirent = next_dirent(dirent);
  }

  parent->dir.last = node;
}

void initrd_read(char *filename, fs_t *fs) {
  struct stat st;
  int fd = open(filename, O_RDWR);
  if (fd == -1 || fstat(fd, &st) == -1) {
    fprintf(stderr, "initrd: %s: %s\n", filename, strerror(errno));
    exit(1);
  }


  base = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

  initrd_metadata_t *meta = (initrd_metadata_t *) base;
  if (meta->magic != INITRD_MAGIC) {
    fprintf(stderr, "initrd: %s: Is not a valid ramdisk\n", filename);
    munmap(base, st.st_size);
    exit(1);
  }

  uint16_t used_nodes = meta->total_nodes - meta->free_nodes;
  nodes = malloc(used_nodes * sizeof(fs_node_t *));

  fs_node_t *root = create_directory("/", NULL, false);

  last_id = 1;
  nodes[0] = root;
  initrd_file_t *root_file = get_file(meta->file_offset);
  initrd_dirent_t *dirent = get_dirent(root_file->offset);
  initrd_file_t *first = get_file(dirent->offset);
  build_tree(root, first);

  metadata_t m;
  m.last_id = meta->last_id;
  m.total_nodes = used_nodes;
  fs->meta = m;
  fs->root = root;

  free(nodes);
  munmap(base, st.st_size);
}
