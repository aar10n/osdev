//
// Created by Aaron Gill-Braun on 2020-09-08.
//

#include <string.h>
#include <stdio.h>

#include "common.h"
#include "fs.h"

typedef struct {
  fs_node_t *node;
  uint32_t offset;
} pending_node_t;

uint32_t nearest_multiple(uint32_t num, uint32_t multiple) {
  uint32_t remainder = num % multiple;
  if (multiple == 0 || remainder == 0) return num;
  return num + multiple - remainder;
}

uint32_t get_tree_data_length(fs_node_t *root) {
  assert(root->flags & FILE_DIRECTORY);

  uint32_t size = 0;
  fs_node_t *node = NULL;
  while ((node = next_node(root, node))) {
    if (node->flags & FILE_REGULAR) {
      size += nearest_multiple(node->file.length, block_size);
    } else if (node->flags & FILE_DIRECTORY) {
      uint32_t num_children = get_num_children(node);
      size += nearest_multiple(sizeof(initrd_dirent_t) * num_children, block_size);
    }
  }

  return size;
}

//

void initrd_write(char *file, fs_t *fs) {
  metadata_t meta = fs->meta;
  fs_node_t *root = fs->root;

  uint32_t used_nodes_len = meta.total_nodes * sizeof(initrd_file_t);
  uint32_t free_nodes_len = reserved * sizeof(initrd_file_t);
  uint32_t nodes_len = used_nodes_len + free_nodes_len;

  uint32_t meta_len = nearest_multiple(sizeof(initrd_metadata_t) + nodes_len, block_size);
  uint32_t data_len = get_tree_data_length(root);

  print(VERBOSE, "metadata length: %d\n", meta_len);
  print(VERBOSE, "data length: %d\n", data_len);

  uint32_t free_offset = reserved > 0 ? used_nodes_len : 0;

  uint8_t *buffer = malloc(meta_len + data_len);
  memset(buffer, 0, meta_len + data_len);

  // create the metadata block
  initrd_metadata_t m;
  m.magic = INITRD_MAGIC;
  m.size = meta_len + data_len;
  m.block_size = block_size;
  m.reserved = 0;
  m.last_id = meta.last_id;
  m.total_nodes = meta.total_nodes + reserved;
  m.free_nodes = reserved;
  m.free_offset = free_offset;
  m.file_offset = sizeof(initrd_metadata_t);
  m.data_offset = meta_len;
  memcpy(buffer, &m, sizeof(initrd_metadata_t));

  // allocate some arrays
  fs_node_t **nodes = malloc(meta.total_nodes * sizeof(fs_node_t *));
  uint32_t *offsets = malloc(meta.total_nodes * sizeof(uint32_t));
  pending_node_t *pending = malloc(meta.total_nodes * sizeof(pending_node_t));

  uint32_t num_pend = 0;
  uint32_t meta_ptr = sizeof(initrd_metadata_t);
  uint32_t data_ptr = meta_len;

  // write the all the files and file data
  fs_node_t *node = NULL;
  while ((node = next_node(root, node))) {
    nodes[node->id] = node;
    offsets[node->id] = meta_ptr;

    initrd_file_t f;
    f.id = node->id;
    f.flags = node->flags;

    if (node->flags & FILE_REGULAR) {
      f.length = node->file.length;
      f.offset = data_ptr;

      uint32_t rounded = nearest_multiple(f.length, block_size);
      f.blocks = rounded / block_size;
      memcpy(buffer + data_ptr, node->file.buffer, f.length);
      data_ptr += rounded;
    } else if (node->flags & FILE_DIRECTORY) {
      f.length = 0;
      f.offset = data_ptr;

      // resolve the directory entries after all the
      // file nodes have been written to the buffer
      pending[num_pend].node = node->dir.first;
      pending[num_pend].offset = data_ptr;
      num_pend++;

      uint32_t count = get_num_children(node);
      uint32_t length = count * sizeof(initrd_dirent_t);
      uint32_t rounded = nearest_multiple(length, block_size);
      f.blocks = rounded / block_size;
      data_ptr += rounded;
    } else if (node->flags & FILE_SYMLINK) {
      f.blocks = 0;
      f.length = 0;
      f.offset = offsets[node->link.ptr->id];
    }

    memcpy(buffer + meta_ptr, &f, sizeof(initrd_file_t));
    meta_ptr += sizeof(initrd_file_t);
  }

  // write all the pending directory entries
  for (uint32_t i = 0; i < num_pend; i++) {
    uint32_t offset = pending[i].offset;
    fs_node_t *child = pending[i].node;

    while (child) {
      initrd_dirent_t d;
      d.node = child->id;
      d.flags = 0;
      strcpy(d.name, child->name);
      d.offset = offsets[child->id];
      d.parent = offsets[child->parent->id];
      if (child->next == NULL) {
        d.flags |= DIR_LAST_ENTRY;
      }

      // update the 'dirent' field of the already written
      // node with the offset to its own directory entry
      void *field = buffer + offsets[child->id] + offsetof(initrd_file_t, dirent);
      memcpy(field, &offset, sizeof(uint32_t));

      // write the actual directory entry to the buffer
      memcpy(buffer + offset, &d, sizeof(initrd_dirent_t));
      offset += sizeof(initrd_dirent_t);

      child = child->next;
    }
  }

  assert(meta_ptr - sizeof(initrd_metadata_t) == used_nodes_len);
  assert(data_ptr - meta_len == data_len);

  print(NORMAL, "writing to file...\n")

  FILE *fp = fopen(file, "wb");
  fwrite(buffer, 1, meta_len + data_len, fp);
  fclose(fp);

  print(NORMAL, "wrote %u bytes\n", meta_len + data_len)
  print(NORMAL, "done!\n");
}
