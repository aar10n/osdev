//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#ifndef FS_INITRD_INITRD_H
#define FS_INITRD_INITRD_H

#include <kernel/vfs_types.h>
#include <kernel/device.h>

#include <fs/ramfs/ramfs.h>

// V1 format structures
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

// V2 format structures
typedef struct initrd_header_v2 {
  char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '2'
  uint16_t flags;          // image flags
  uint32_t total_size;     // total size of the initrd image
  uint32_t data_offset;    // offset to data section
  uint16_t entry_count;    // number of entries
  uint32_t metadata_size;  // size of metadata section
  uint32_t data_size;      // size of data section
  uint32_t checksum;       // CRC32 of entire data section (0 = no checksum)
  uint32_t reserved[4];    // reserved for future use
} initrd_header_v2_t;
static_assert(sizeof(struct initrd_header_v2) == 48);

typedef struct initrd_entry_v2 {
  uint8_t entry_type;      // 'f'=file | 'd'=directory | 'l'=symlink
  uint8_t reserved;        // reserved
  uint16_t path_len;       // length of path
  uint16_t mode;           // unix permission bits
  uint16_t reserved2;      // reserved
  uint32_t uid;            // user id
  uint32_t gid;            // group id
  uint32_t mtime;          // modification timestamp
  uint32_t data_offset;    // offset from data section start
  uint32_t data_size;      // size of file data
  uint32_t checksum;       // CRC32 checksum of file data (0 = no checksum)
  uint32_t reserved3;      // reserved
  char path[];             // null-terminated full path
} initrd_entry_v2_t;
static_assert(sizeof(struct initrd_entry_v2) == 36);
// stride = sizeof(struct initrd_v2_entry) + entry.path_len + 1

typedef struct initrd_node {
  uint32_t data_offset;    // absolute offset to file data
  uint32_t data_size;      // size of file data
  uint32_t checksum;       // CRC32 checksum of file data
} initrd_node_t;

// vfs operations
int initrd_vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve, ventry_t **root);
int initrd_vfs_stat(vfs_t *vfs, struct vfs_stat *stat);

// vnode operations
ssize_t initrd_vn_read(vnode_t *vn, off_t off, kio_t *kio);
int initrd_vn_getpage(vnode_t *vn, off_t off, __move page_t **result);
void initrd_vn_cleanup(vnode_t *vn);

#endif

