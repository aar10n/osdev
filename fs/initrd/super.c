//
// Created by Aaron Gill-Braun on 2023-04-25.
//

#include <initrd/initrd.h>

#include <device.h>
#include <inode.h>
#include <dentry.h>
#include <path.h>

#include <string.h>
#include <printf.h>
#include <panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("initrd: %s: " fmt, __func__, ##__VA_ARGS__)


int initrd_sb_mount(super_block_t *sb, dentry_t *mount) {
  const fs_type_t *fs = sb->fs;
  device_t *device = sb->device;

  ssize_t res;
  initrd_header_t header;
  if ((res = __dev_read(device, 0, &header, sizeof(header))) < 0) {
    return (int) res;
  } else if (res != sizeof(header)) {
    return -EIO;
  }

  // make sure its a valid initrd
  if (strncmp(header.signature, "INITv1", 6) != 0) {
    return -EINVAL;
  }

  kprintf("initrd:     total size: {:d}\n", header.total_size);
  kprintf("initrd:     data offset: {:d}\n", header.data_offset);
  kprintf("initrd:     entry count: {:d}\n", header.entry_count);

  // read the metadata
  size_t metadata_size = header.data_offset - sizeof(header);
  ASSERT(metadata_size <= UINT16_MAX); // prevent overflow
  uint8_t metadata[metadata_size];
  if ((res = __dev_read(device, sizeof(header), metadata, metadata_size)) < 0) {
    return (int) res;
  } else if (res != metadata_size) {
    return -EIO;
  }

  // the initrd format guarentees that all intermedite directories appear
  // before their children, although the specific order is not guarenteed.

  // build the directory tree
  ino_t ino = 1;
  initrd_entry_t *entry = (initrd_entry_t *) metadata;
  for (int i = 0; i < header.entry_count; i++) {
    path_t path = strn2path(entry->path, entry->path_len);
    kprintf("initrd:     {:path}\n", &path);

    // iterate over directory path components
    dentry_t *parent = mount;
    path_t part = path_dirname(path);
    path_t n = path_basename(path);
    while (!path_is_null(part = path_next_part(part))) {
      kprintf("initrd:       -> {:path}\n", &part);
      dentry_t *next = d_get_child(parent, path_start(part), path_len(part));
      if (next == NULL) {
        // malformed initrd
        DPRINTF("missing parent directory: {:path}\n", &part);
        return -EINVAL;
      } else if (!IS_IFDIR(next)) {
        // malformed initrd
        DPRINTF("parent is not a directory: {:path}\n", &part);
        return -EINVAL;
      }
      parent = next;
    }

    // create the entry
    path_t name = path_basename(path);
    mode_t mode = 0755;
    switch (entry->entry_type) {
      case 'd': mode |= S_IFDIR; break;
      case 'f': mode |= S_IFREG; break;
      case 'l': mode |= S_IFLNK; break;
      default:
        panic("invalid entry type");
    }

    inode_t *inode = i_alloc(sb, ino++, mode);
    dentry_t *child = d_alloc(path_start(path), path_len(path), mode, fs->dentry_ops);
    i_link_dentry(inode, child);
    d_add_child(parent, child);

    entry = offset_ptr(entry, sizeof(struct initrd_entry) + entry->path_len + 1);
  }

  return 0;
}

int initrd_sb_unmount(super_block_t *sb) {
  return 0;
}

int initrd_sb_read_inode(super_block_t *sb, inode_t *inode) {
  return 0;
}
