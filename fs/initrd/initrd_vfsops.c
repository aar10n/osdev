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
// #define DPRINTF(fmt, ...) kprintf("initrd: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("initrd: " fmt, ##__VA_ARGS__)

#define HMAP_TYPE ramfs_node_t *
#include <hash_map.h>

// these vnode ops are only used for regular files loaded from an initrd image
// to make them write-only in a read-write filesystem.
struct vnode_ops initrd_vnode_ops = {
  .v_read = initrd_vn_read,
  .v_getpage = initrd_vn_getpage,
  .v_readlink = ramfs_vn_readlink,
  .v_readdir = ramfs_vn_readdir,
  .v_lookup = ramfs_vn_lookup,
  .v_cleanup = initrd_vn_cleanup,
};

//

int initrd_vfs_mount(vfs_t *vfs, device_t *device, __move ventry_t **root) {
  kio_t tmp;
  ssize_t res;
  initrd_header_t header;
  if ((res = d_read_n(device, 0, &header, sizeof(header))) < 0 || res != sizeof(header)) {
    EPRINTF("mount: failed to read header\n");
    if (res >= 0)
      res = -EIO;
    return (int) res;
  }

  // make sure its a valid initrd
  if (strncmp(header.signature, "INITv1", 6) != 0) {
    EPRINTF("mount: invalid initrd signature: %.6s\n", header.signature);
    return -EINVAL;
  }

  // read the metadata
  size_t metadata_size = header.data_offset - sizeof(header);
  void *metadata = vmalloc(metadata_size, VM_RDWR);
  ASSERT(metadata_size <= UINT16_MAX); // prevent overflow
  if ((res = d_read_n(device, sizeof(header), metadata, metadata_size)) < 0 || res != metadata_size) {
    vfree(metadata);
    if (res >= 0)
      res = -EIO;
    return (int) res;
  }

  // back the initrd filesystem by a ramfs
  ramfs_mount_t *mount = ramfs_alloc_mount(vfs);
  vfs->data = mount;

  hash_map_t *node_map = hash_map_new();
  hash_map_set_str(node_map, str_from("/"), mount->root);

  // build the filesystem tree
  id_t next_id = mount->next_id;
  path_t dirpath = path_new("/", 1);
  ramfs_node_t *dir_node = mount->root;
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
        // if the initrd is well formed this cannot happen. so if we get here the
        // entire image can be assumed to be invalid and we have to bail out
        EPRINTF("mount: invalid entry order: failed to find parent directory for {:path}\n", &path);
        res = -ENOENT;
        goto error;
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
        EPRINTF("mount: invalid entry type: %c\n", entry->entry_type);
        continue; // skip invalid entry
    }

    // allocate ramfs node
    ramfs_node_t *node = ramfs_alloc_node(mount, &make_vattr(type, mode));
    // allocate entry and add it to parent directory
    ramfs_dentry_t *dent = ramfs_alloc_dentry(node, cstr_from_path(basename));
    ramfs_add_dentry(dir_node, dent);

    if (entry->entry_type == 'f') {
      DPRINTF("   file  {:path} (%u bytes)\n", &path, entry->data_size);
      initrd_node_t *rd_node = kmallocz(sizeof(initrd_node_t));
      rd_node->entry_offset = (size_t) entry - (size_t) metadata;
      rd_node->data_offset = entry->data_offset;
      node->data = rd_node;
      node->size = entry->data_size;
      node->ops = &initrd_vnode_ops;
    } else if (entry->entry_type == 'd') {
      DPRINTF("   dir   {:path} \n", &path);
      str_t path_str = str_from_path(path);
      hash_map_set_str(node_map, path_str, node);
      dir_node = node;
      dirpath = path;
    } else if (entry->entry_type == 'l') {
      DPRINTF("   link  {:path}\n", &path);
      size_t len = entry->data_size - 1;
      str_t link = str_alloc_empty(len); // data_size includes null terminator
      tmp = kio_writeonly_from_str(link);
      if (d_nread(device, entry->data_offset, len, &tmp) < 0 || tmp.size != len) {
        EPRINTF("mount: failed to read link data for {:path}\n", &path);
        str_free(&link);
        continue; // skip invalid entry
      }
      node->size = len;
      node->n_link = link;
    }

    entry = offset_ptr(entry, sizeof(struct initrd_entry) + entry->path_len + 1);
  }
  hash_map_free(node_map);
  vfree(metadata);

  // create the root vnode
  vnode_t *vn = vn_alloc(1, &make_vattr(V_DIR, 0755 | S_IFDIR));
  vn->data = mount->root;
  ventry_t *ve = ve_alloc_linked(cstr_new("/", 1), vn);
  *root = ve_moveref(&ve);
  vn_release(&vn);
  return 0;

LABEL(error);
  // TODO: cleanup all the nodes
  hash_map_free(node_map);
  vfree(metadata);
  ramfs_vfs_cleanup(vfs);
  return (int) res;
};

int initrd_vfs_stat(vfs_t *vfs, struct vfs_stat *stat) {
  unimplemented("initrd_vfs_stat");
}
