//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#include "initrd.h"

#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>
#include <kernel/vfs/path.h>

#include <fs/ramfs/ramfs.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("initrd: " fmt, ##__VA_ARGS__)

#define HMAP_TYPE ramfs_node_t *
#include <hash_map.h>

//

int initrd_vfs_mount(vfs_t *vfs, device_t *device, __move ventry_t **root) {
  kio_t tmp;
  ssize_t res;
  initrd_header_t header;
  if ((res = __d_read(device, 0, &header, sizeof(header))) < 0 || res != sizeof(header)) {
    DPRINTF("mount: failed to read header\n");
    if (res >= 0)
      res = -EIO;
    return (int) res;
  }

  // make sure its a valid initrd
  if (strncmp(header.signature, "INITv1", 6) != 0) {
    DPRINTF("mount: invalid initrd signature: %.6s\n", header.signature);
    return -EINVAL;
  }

  // kprintf("initrd:   total size: %u\n", header.total_size);
  // kprintf("initrd:   data offset: %u\n", header.data_offset);
  // kprintf("initrd:   entry count: %d\n", header.entry_count);

  // read the metadata
  size_t metadata_size = header.data_offset - sizeof(header);
  void *metadata = vmalloc(metadata_size, VM_RDWR);
  ASSERT(metadata_size <= UINT16_MAX); // prevent overflow
  if ((res = __d_read(device, sizeof(header), metadata, metadata_size)) < 0 || res != metadata_size) {
    vfree(metadata);
    if (res >= 0)
      res = -EIO;
    return (int) res;
  }

  ramfs_mount_t *mount = ramfs_alloc_mount(vfs);
  vfs->data = mount;

  // build the filesystem tree
  id_t next_id = 1;
  hash_map_t *node_map = hash_map_new();
  hash_map_set_str(node_map, str_from("/"), mount->root);
  ramfs_node_t *dir_node = mount->root;
  path_t dirpath = path_new("/", 1);
  initrd_entry_t *entry = (initrd_entry_t *) metadata;
  for (int i = 0; i < header.entry_count; i++) {
    path_t path = path_new(entry->path, entry->path_len);
    path = path_strip_trailing(path, '/');

    path_t dirname = path_dirname(path);
    path_t basename = path_basename(path);
    if (!path_eq(dirname, dirpath)) {
      // this entry is in a different directory
      // find the parent directory of the new entry
      cstr_t dirstr = cstr_from_path(dirname);
      ramfs_node_t *parent = hash_map_get_cstr(node_map, dirstr);
      if (parent == NULL) {
        hash_map_free(node_map);
        // TODO: free all allocated nodes
        DPRINTF("mount: invalid entry order: failed to find parent directory for {:path}\n", &path);
        return -ENOENT;
      }

      dir_node = parent;
      dirpath = dirname;
    }

    enum vtype type;
    mode_t mode = 0755;
    switch (entry->entry_type) {
      case 'f': type = V_REG; mode |= S_IFREG; break;
      case 'd': type = V_DIR; mode |= S_IFDIR; break;
      case 'l': type = V_LNK; mode |= S_IFLNK; break;
      default:
        // TODO: free and return error
        panic("invalid entry type");
    }

    // allocate ramfs node
    ramfs_node_t *node = ramfs_alloc_node(mount, &make_vattr(type, mode));
    // allocate entry and add it to parent directory
    ramfs_dentry_t *dent = ramfs_alloc_dentry(node, cstr_from_path(basename));
    ramfs_add_dentry(dir_node, dent);

    if (entry->entry_type == 'f') {
      // kprintf("initrd:   file  {:path} (%u bytes)\n", &path, entry->data_size);
      initrd_node_t *rd_node = kmallocz(sizeof(initrd_node_t));
      node->data = rd_node;
      node->size = entry->data_size;
      rd_node->entry_offset = (size_t) entry - (size_t) metadata;
      rd_node->data_offset = entry->data_offset;
    } else if (entry->entry_type == 'd') {
      // kprintf("initrd:   dir   {:path} \n", &path);
      str_t path_str = str_from_path(path);
      hash_map_set_str(node_map, path_str, node);
      dir_node = node;
      dirpath = path;
    } else if (entry->entry_type == 'l') {
      // kprintf("initrd:   link  {:path}\n", &path);
      size_t len = entry->data_size - 1;
      str_t link = str_alloc_empty(len); // data_size includes null terminator
      tmp = kio_writeonly_from_str(link);
      if (d_nread(device, entry->data_offset, len, &tmp) < 0 || tmp.size != len) {
        DPRINTF("mount: failed to read link data\n");
        hash_map_free(node_map);
        // TODO: free all allocated nodes
        if (tmp.size >= 0)
          return -EIO;
        return (int) tmp.size;
      }
      node->size = len;
      node->n_link = link;
    }

    entry = offset_ptr(entry, sizeof(struct initrd_entry) + entry->path_len + 1);
  }
  hash_map_free(node_map);
  vfree(metadata);

  // create root vnode
  vnode_t *vn = vn_alloc(0, &make_vattr(V_DIR, 0755 | S_IFDIR));
  vn->data = mount->root;
  ventry_t *ve = ve_alloc_linked(cstr_new("/", 1), vn);
  *root = ve_moveref(&ve);
  return 0;
};

int initrd_vfs_stat(vfs_t *vfs, struct vfs_stat *stat) {
  unimplemented("initrd_vfs_stat");
}
