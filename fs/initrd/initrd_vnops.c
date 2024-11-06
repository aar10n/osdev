//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#include "initrd.h"

#include <fs/ramfs/ramfs.h>

#include <kernel/mm.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)

//

ssize_t initrd_vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  if (off >= vn->size) {
    return -ERANGE;
  }

  device_t *device = vn->device;
  ramfs_node_t *node = vn->data;
  initrd_node_t *rd_node = node->data;
  return d_nread(device, rd_node->data_offset + off, vn->size - off, kio);
}

int initrd_vn_getpage(vnode_t *vn, off_t off, __move page_t **result) {
  if (off >= vn->size) {
    return -ERANGE;
  }

  device_t *device = vn->device;
  ramfs_node_t *node = vn->data;
  initrd_node_t *rd_node = node->data;
  *result = d_getpage(device, rd_node->data_offset + off);
  return *result == NULL ? -EIO : 0;
}

void initrd_vn_cleanup(vnode_t *vn) {
  ramfs_node_t *node = vn->data;
  kfree(node->data);
  ramfs_vn_cleanup(vn);
}
