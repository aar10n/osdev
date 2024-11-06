//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#ifndef FS_INITRD_INITRD_H
#define FS_INITRD_INITRD_H

#include <kernel/vfs_types.h>
#include <kernel/device.h>

#include <fs/ramfs/ramfs.h>

typedef struct initrd_header {
  char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '1'
  uint16_t flags;          // initrd flags
  uint32_t total_size;     // total size of the initrd image
  uint32_t data_offset;    // offset from start of image to start of data section
  uint16_t entry_count;    // number of entries in the metadata section
  uint8_t reserved[14];    // reserved
} initrd_header_t;
static_assert(sizeof(struct initrd_header) == 32);

typedef struct initrd_entry {
  uint8_t entry_type;   // type: 'f'=file | 'd'=directory | 'l'=symlink
  uint8_t reserved;     // reserved
  uint16_t path_len;    // length of the file path
  uint32_t data_offset; // offset from start of image to associated data
  uint32_t data_size;   // size of the associated data
  char path[];          // file path
} initrd_entry_t;
static_assert(sizeof(struct initrd_entry) == 12);
// stride = sizeof(struct initrd_entry) + entry.path_len + 1

typedef struct initrd_node {
  uint32_t entry_offset;
  uint32_t data_offset;
} initrd_node_t;

// vfs operations
int initrd_vfs_mount(vfs_t *vfs, device_t *device, ventry_t **root);
int initrd_vfs_stat(vfs_t *vfs, struct vfs_stat *stat);

// vnode operations
ssize_t initrd_vn_read(vnode_t *vn, off_t off, kio_t *kio);
int initrd_vn_getpage(vnode_t *vn, off_t off, __move page_t **result);
void initrd_vn_cleanup(vnode_t *vn);

#endif

