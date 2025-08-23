//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#ifndef FS_PROCFS_PROCFS_H
#define FS_PROCFS_PROCFS_H

#include <kernel/vfs_types.h>
#include <kernel/device.h>

typedef struct procfs_object procfs_object_t;
typedef struct procfs_dir procfs_dir_t;
typedef struct procfs_dirent procfs_dirent_t;
struct ramfs_node;

typedef struct procfs_ops {
  /* common hooks (optional) */

  // called when the object is first accessed
  int (*pf_open)(procfs_object_t *obj, int flags);
  // called when all references are closed
  int (*pf_close)(procfs_object_t *obj);
  // cleanup when object is unregistered or ephemeral object is freed
  void (*pf_cleanup)(struct procfs_object *obj);

  /* file hooks */

  // read data from the object (required)
  ssize_t (*pf_read)(procfs_object_t *obj, off_t off, kio_t *kio);
  // write data to the object (optional)
  ssize_t (*pf_write)(struct procfs_object *obj, off_t off, kio_t *kio);

  /* directory hooks */

  // read directory entry from object (required)
  ssize_t (*pf_readdir)(procfs_object_t *obj, off_t index, struct dirent *dirent);
  // lookup an entry by name (required)
  int (*pf_lookup)(procfs_object_t *obj, cstr_t name, __move procfs_object_t **result);
} procfs_ops_t;


#ifdef PROCFS_INTERNAL
struct procfs_object {
  str_t path;                     // object path in procfs
  void *data;                     // private data for object implementation
  size_t size;                    // size hint for the object (0 if dynamic)
  int mode;                       // file permissions
  bool is_dir;                    // whether this is a directory

  /**
   * whether this is a static object. static objects do not have operations
   * and are simply placeholders for normal ramfs nodes.
   */
  bool is_static;

  /**
   * whether this object is ephemeral. ephemeral objects are returned by a
   * lookup on a dynamic directory and are freed when the owning vnode is
   * cleaned up. they are not registered in the procfs tree.
   */
  bool is_ephemeral;

  struct procfs_ops *ops;         // operations for this object
  LIST_HEAD(ramfs_node_t) nodes;  // ramfs nodes associated with this object
};

struct procfs_dir {
  str_t name;                     // directory name
  struct procfs_object *obj;      // the procfs object (NULL for root)
  struct procfs_dir *parent;      // parent directory (NULL for root)
  LIST_HEAD(struct procfs_dirent) entries;  // entries in this directory
};

struct procfs_dirent {
  str_t name;
  struct procfs_object *obj;  // the procfs object
  struct procfs_dir *dir;     // the procfs_dir if this is a directory entry
  LIST_ENTRY(struct procfs_dirent) next;
};

// vfs operations
int procfs_vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve, __move ventry_t **root);

// vnode operations
int procfs_vn_open(vnode_t *vn, int flags);
int procfs_vn_close(vnode_t *vn);
ssize_t procfs_vn_read(vnode_t *vn, off_t off, kio_t *kio);
ssize_t procfs_vn_write(vnode_t *vn, off_t off, kio_t *kio);
int procfs_vn_getpage(vnode_t *vn, off_t off, __move struct page **result);
int procfs_vn_falloc(vnode_t *vn, size_t len);

ssize_t procfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf);
int procfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result);

void procfs_vn_cleanup(vnode_t *vn);
#endif

// Public API
int procfs_register_file(cstr_t path, struct procfs_ops *ops, void *data, int mode);
int procfs_register_dir(cstr_t path, struct procfs_ops *ops, void *data, int mode);
int procfs_register_static_dir(cstr_t path, int mode);
int procfs_unregister(cstr_t path);

procfs_object_t *procfs_ephemeral_object(cstr_t name, procfs_ops_t *ops, void *data, int mode, bool is_dir);
cstr_t procfs_obj_name(procfs_object_t *obj);
void *procfs_obj_data(procfs_object_t *obj);
ssize_t procfs_obj_read_string(procfs_object_t *obj, off_t off, kio_t *kio, const char *str);


#endif
