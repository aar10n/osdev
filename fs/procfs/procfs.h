//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#ifndef FS_PROCFS_PROCFS_H
#define FS_PROCFS_PROCFS_H

#include "seqfile.h"

#include <kernel/vfs_types.h>

struct device;
typedef struct procfs_object procfs_object_t;
typedef struct procfs_dir procfs_dir_t;
typedef struct procfs_dirent procfs_dirent_t;

/**
 * A procfs handle represents an open instance of a procfs object. it contains
 * a pointer to the procfs object and any per-file private data returned from
 * open.
 */
typedef struct procfs_handle {
  procfs_object_t *obj; // the procfs object
  void *data;           // per-file private data (e.g. seqfile)
} procfs_handle_t;

/**
 * Operations for a procfs object.
 * - File objects must implement `proc_read`
 * - Directory objects must implement both `proc_readdir` and `proc_lookup`
 * - Symlink objects must implement `proc_readlink`
 */
typedef struct procfs_ops {
  /* common operations (optional) */

  // called when the object is opened.
  int (*proc_open)(procfs_object_t *obj, int flags, void **handle_data);
  // called when the object is closed.
  int (*proc_close)(procfs_handle_t *h);
  // called when an object is unregistered or ephemeral object is freed.
  void (*proc_cleanup)(procfs_object_t *obj);
  // called to validate that the object is still valid. by default objects
  // are assumed to always be valid, but ephemeral objects may want to
  // implement this to check if the underlying data is still valid.
  bool (*proc_validate)(procfs_object_t *obj);

  /* file object operations */

  // read data from the object (required)
  ssize_t (*proc_read)(procfs_handle_t *h, off_t off, kio_t *kio);
  // write data to the object (optional)
  ssize_t (*proc_write)(procfs_handle_t *h, off_t off, kio_t *kio);
  // seek within the object (optional)
  off_t (*proc_lseek)(procfs_handle_t *h, off_t offset, int whence);

  /* directory operations */

  // read directory entry from object (required)
  ssize_t (*proc_readdir)(procfs_handle_t *h, off_t *poff, struct dirent *dirent);
  // lookup an entry by name (required)
  // note: this function does not have access to dynamic data
  int (*proc_lookup)(procfs_object_t *obj, cstr_t name, __move procfs_object_t **result);

  /* symlink operations */

  // read the target of a symlink (required)
  // note: this function does not have access to dynamic data
  int (*proc_readlink)(procfs_object_t *obj, kio_t *kio);
} procfs_ops_t;

/**
 * procfs object types
 */
enum procfs_type {
  PROCFS_FILE,
  PROCFS_DIR,
  PROCFS_LINK,
};


//
// MARK: Internal structures and functions
//

#ifdef PROCFS_INTERNAL

/**
 * A procfs object represents a file or directory in the procfs object tree.
 * When a procfs mount is created, the procfs object tree is mirrored into
 * a ramfs tree for the mount.
 */
struct procfs_object {
  str_t path;                     // object path in procfs
  void *data;                     // private data for object implementation
  size_t size;                    // size hint for the object (0 if dynamic)
  int mode;                       // file permissions
  enum procfs_type type;      // object type

  /*
   * whether this is a static object. static objects do not have operations
   * and are simply placeholders for normal ramfs nodes.
   */
  bool is_static;

  /*
   * whether this object is ephemeral. ephemeral objects are returned by a
   * lookup on a dynamic directory and are freed when the owning vnode is
   * cleaned up. they are not registered in the procfs tree.
   */
  bool is_ephemeral;

  procfs_ops_t *ops;         // operations for this object (v2)
  LIST_HEAD(struct ramfs_node) nodes;  // ramfs nodes associated with this object
};

/**
 * A procfs dir represents a directory in the procfs object tree. Every dir
 * has an entry in the procfs_directories global hash table, and makes up
 * the hierarchy of the procfs tree.
 */
struct procfs_dir {
  str_t name;                     // directory name
  procfs_object_t *obj;           // the procfs object (NULL for root)
  procfs_dir_t *parent;           // parent directory (NULL for root)
  LIST_HEAD(procfs_dirent_t) entries;  // entries in this directory
};

/**
 * A procfs dirent represents an entry in a procfs directory. it contains
 * a pointer to the procfs object and, if the object is a directory, a
 * pointer to the corresponding procfs_dir structure.
 */
struct procfs_dirent {
  str_t name;
  procfs_object_t *obj;           // the procfs object
  procfs_dir_t *dir;              // the procfs_dir if this is a directory entry
  LIST_ENTRY(procfs_dirent_t) next;   // next entry in the directory
};

// file operations
int procfs_f_open(file_t *file, int flags);
int procfs_f_close(file_t *file);
int procfs_f_getpage(file_t *file, off_t off, __move struct page **page);
ssize_t procfs_f_read(file_t *file, kio_t *kio);
ssize_t procfs_f_write(file_t *file, kio_t *kio);
ssize_t procfs_f_readdir(file_t *file, kio_t *kio);
off_t procfs_f_lseek(file_t *file, off_t offset, int whence);
int procfs_f_stat(file_t *file, struct stat *statbuf);
void procfs_f_cleanup(file_t *file);

// vfs operations
int procfs_vfs_mount(vfs_t *vfs, struct device *device, ventry_t *mount_ve, __move ventry_t **root);

// vnode operations
int procfs_vn_open(vnode_t *vn, int flags);
int procfs_vn_close(vnode_t *vn);
ssize_t procfs_vn_read(vnode_t *vn, off_t off, kio_t *kio);
ssize_t procfs_vn_write(vnode_t *vn, off_t off, kio_t *kio);
int procfs_vn_getpage(vnode_t *vn, off_t off, __move struct page **result);
int procfs_vn_falloc(vnode_t *vn, size_t len);
int procfs_vn_readlink(vnode_t *vn, kio_t *kio);
ssize_t procfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf);

ssize_t procfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf);
int procfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result);

void procfs_vn_alloc_file(vnode_t *vn, struct file *file);
void procfs_vn_cleanup(vnode_t *vn);
bool procfs_ve_validate(ventry_t *ve);
#endif

//
// MARK: Public API
//

int procf_register_file(cstr_t path, struct procfs_ops *ops, void *data, int mode);
int procfs_register_dir(cstr_t path, struct procfs_ops *ops, void *data, int mode);
int procfs_register_symlink(cstr_t path, struct procfs_ops *ops, void *data, int mode);
int procfs_register_seq_file(cstr_t path, struct seq_ops *seq_ops, void *data, int mode);
int procfs_register_static_dir(cstr_t path, int mode);
int procfs_unregister(cstr_t path);

procfs_object_t *procfs_ephemeral_object(cstr_t name, procfs_ops_t *ops, void *data, int mode, enum procfs_type type);
procfs_object_t *procfs_ephemeral_seq_object(cstr_t name, struct seq_ops *seq_ops, void *data, int mode);
cstr_t procfs_obj_name(procfs_object_t *obj);
enum procfs_type procfs_obj_type(procfs_object_t *obj);
void *procfs_obj_data(procfs_object_t *obj);
void procfs_obj_data_clear(procfs_object_t *obj);

static inline enum vtype procfs_obj_vtype(procfs_object_t *obj) {
  switch (procfs_obj_type(obj)) {
    case PROCFS_FILE: return V_REG;
    case PROCFS_DIR: return V_DIR;
    case PROCFS_LINK: return V_LNK;
    default: unreachable;
  }
}

#define PROCFS_SIMPLE_OPS(name, _show, _write, _cleanup) \
  static struct seq_ops name = {               \
    .start = seq_simple_start,                 \
    .stop = seq_simple_stop,                   \
    .next = seq_simple_next,                   \
    .show = (_show),                           \
    .write = (_write),                         \
    .cleanup = (_cleanup),                     \
  }

/**
 * Helper macro to statically register a simple procfs file.
 */
#define PROCFS_REGISTER_SIMPLE(name, path, _show, _write, mode) \
  static struct seq_ops __simple_ops_##name = {              \
    .start = seq_simple_start,                               \
    .stop = seq_simple_stop,                                 \
    .next = seq_simple_next,                                 \
    .show = (_show),                                         \
    .write = (_write),                                       \
    .cleanup = NULL,                                         \
  };                                                         \
  void __register_##name(void) { \
    if (procfs_register_seq_file(cstr_make(path), &__simple_ops_##name, NULL, mode) < 0) \
      panic("failed to register procfs entry " path "\n"); \
  } \
  MODULE_INIT(__register_##name)

/**
 * Helper macro to statically register a seqfile procfs file.
 */
#define PROCFS_REGISTER_SEQFILE(name, path, seq_ops, mode) \
  void __register_##name(void) { \
    if (procfs_register_seq_file(cstr_make(path), seq_ops, NULL, mode) < 0) \
      panic("failed to register procfs entry " path "\n"); \
  } \
  MODULE_INIT(__register_##name)

/**
 * Helper macro to statically register a procfs dynamic directory.
 */
#define PROCFS_REGISTER_DIR(name, path, ops, mode) \
  void __register_##name(void) { \
    if (procfs_register_dir(cstr_make(path), ops, NULL, mode) < 0) \
      panic("failed to register procfs entry " path "\n"); \
  } \
  MODULE_INIT(__register_##name)

/**
 * Helper macro to statically register a procfs symlink.
 */
#define PROCFS_REGISTER_SYMLINK(name, path, ops, mode) \
  void __register_##name(void) { \
    if (procfs_register_symlink(cstr_make(path), ops, NULL, mode) < 0) \
      panic("failed to register procfs entry " path "\n"); \
  } \
  MODULE_INIT(__register_##name)

#endif
