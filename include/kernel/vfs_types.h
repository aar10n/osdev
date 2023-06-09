//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_VFS_TYPES_H
#define KERNEL_VFS_TYPES_H

#include <base.h>
#include <queue.h>
#include <mutex.h>
#include <kio.h>
#include <ref.h>
#include <str.h>

#include <abi/dirent.h>
#include <abi/fcntl.h>
#include <abi/seek-whence.h>
#include <abi/stat.h>
#include <abi/vm-flags.h>

#define PATH_MAX 4096


struct device;
struct vm_mapping;

struct vnode;
struct vnode_ops;
struct vfs;
struct vfs_ops;
struct ventry;
struct ventry_ops;
struct vtable;
struct ftable;

typedef struct vcache vcache_t;
typedef struct file file_t;

typedef uint64_t hash_t;


typedef struct fs_type {
  const char *name;
  int flags; // mount flags
  struct vfs_ops *vfs_ops;
  struct vnode_ops *vnode_ops;
  struct ventry_ops *ventry_ops;
} fs_type_t;

// the main vfs objects share these lifecycle states
enum vstate {
  V_EMPTY,
  V_ALIVE,
  V_DEAD,
};
#define V_STATE_MAX V_DEAD

#define V_ISEMPTY(v) ((v)->state == V_EMPTY)
#define V_ISALIVE(v) ((v)->state == V_ALIVE)
#define V_ISDEAD(v) ((v)->state == V_DEAD)

#define v_getref(ptr) ({ \
  static_assert(__builtin_types_compatible_p(typeof(ptr), void *)); \
  if (ptr) ref_get(&(ptr)->refcount); \
  ptr;\
})

#define v_moveref(dptr) ({ \
  typeof(*(dptr)) __ptr = *(dptr); \
  *(dptr) = NULL; \
  __ptr; \
})

// =================================
//               vfs
// =================================

/**
 * A virtual filesystem.
 *
 *
 *
 * Locking:
 *   lock     - held while vfs struct is being accessed/modified
 *   op_lock  - held during vnode operations within this vfs
 *   mnt_lock - held during the call to vfs_mount() on a submount
 */
typedef struct vfs {
  enum vstate state : 8;          // lifecycle state
  uint16_t : 8;                   // reserved
  uint16_t flags;                 // flags
  int mount_flags;                // mount flags
  void *data;                     // filesystem private data

  mutex_t lock;                   // vfs lock
  rw_lock_t op_lock;              // vfs operation lock (held during vnode ops)
  refcount_t refcount;            // reference count

  struct fs_type *type;           // filesystem type
  struct vfs_ops *ops;            // vfs operations
  struct vtable *vtable;          // vtable for this vfs

  /* valid while mounted */
  struct ventry *root_ve;         // root ventry reference
  struct device *device;          // device mounted on
  struct vfs *parent;             // parent vfs (no ref)

  /* fs info */
  str_t label;                    // filesystem label
  uint64_t total_size;            // total size of filesystem
  uint64_t free_size;             // free size of filesystem
  uint64_t avail_size;            // available size of filesystem
  uint64_t total_files;           // total number of files

  LIST_HEAD(struct vnode) vnodes; // list of vnodes
  LIST_HEAD(struct vfs) submounts;// list of submounts
  LIST_ENTRY(struct vfs) list;    // entry in parent's submounts list
} vfs_t;

// mount flags
#define VFS_RDONLY 0x01
#define   VFS_ISRDONLY(vfs) ((vfs)->mount_flags & VFS_RDONLY)

struct vfs_stat {
  uint64_t total_size;
  uint64_t free_size;
  uint64_t avail_size;
  uint64_t total_files;
};

struct vfs_ops {
  int (*v_mount)(struct vfs *vfs, struct device *device, __move struct ventry **root);
  int (*v_unmount)(struct vfs *vfs);
  int (*v_sync)(struct vfs *vfs);
  int (*v_stat)(struct vfs *vfs, struct vfs_stat *stat);

  void (*v_cleanup)(struct vfs *vfs);
};


// =================================
//             vnode
// =================================

enum vtype {
  V_NONE, // no type (empty)
  V_REG,  // regular file
  V_DIR,  // directory
  V_LNK,  // symbolic link
  V_BLK,  // block device
  V_CHR,  // character device
  V_FIFO, // fifo
  V_SOCK, // socket
};
#define V_TYPE_MAX V_MNT

#define V_ISREG(v) ((v)->type == V_REG)
#define V_ISDIR(v) ((v)->type == V_DIR)
#define V_ISLNK(v) ((v)->type == V_LNK)
#define V_ISBLK(v) ((v)->type == V_BLK)
#define V_ISCHR(v) ((v)->type == V_CHR)
#define V_ISFIFO(v) ((v)->type == V_FIFO)
#define V_ISSOCK(v) ((v)->type == V_SOCK)

/**
 * A virtual filesystem node.
 *
 * A vnode represents an object in a filesystem. It owns the data associated
 * with the object and is referenced by one or more ventries.
 */
typedef struct vnode {
  id_t id;                        // vnode id
  enum vtype type : 8;            // vnode type
  enum vstate state : 8;          // lifecycle state
  uint16_t flags;                 // vnode flags
  void *data;                     // filesystem private data

  mutex_t lock;                   // vnode lock
  rw_lock_t data_lock;            // vnode file data lock
  refcount_t refcount;            // vnode reference count
  uint32_t nopen;                 // number of open file descriptors
  cond_t waiters;                 // waiters for nopen == 0

  id_t parent_id;                 // parent vnode id
  struct device *device;          // owning device
  struct vfs *vfs;                // owning vfs
  struct vnode_ops *ops;          // vnode operations

  /* attributes */
  size_t nlink;                   // number of hard links
  size_t size;                    // size in bytes
  size_t blocks;                  // number of blocks
  time_t atime;                   // last access time
  time_t mtime;                   // last modification time
  time_t ctime;                   // last status change time

  /* associated data */
  union {
    dev_t v_dev;                  // device number (V_BLK, V_CHR)
    str_t v_link;                 // symlink (V_LNK)
    struct vnode *v_shadow;       // shadowed vnode (V_DIR && VN_MOUNT)
  };

  LIST_ENTRY(struct vnode) list;  // vfs vnode list
} vnode_t;

// vnode flags
#define VN_LOADED 0x01 // vnode has been loaded
#define   VN_ISLOADED(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_LOADED))
#define VN_DIRTY  0x02 // vnode has been modified
#define   VN_ISDIRTY(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_DIRTY))
#define VN_MOUNT  0x04 // vnode is a mount point
#define   VN_ISMOUNT(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_MOUNT))
#define VN_ROOT   0x08 // vnode is the root of a filesystem
#define   VN_ISROOT(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_ROOT))
#define VN_OPEN   0x10 // vnode is open (has open file descriptors)
#define   VN_ISOPEN(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_OPEN))


struct vattr {
  enum vtype type;
  mode_t mode;
};

struct vnode_ops {
  // file operations
  int (*v_open)(struct vnode *vn, int flags);
  int (*v_close)(struct vnode *vn);
  ssize_t (*v_read)(struct vnode *vn, off_t off, struct kio *kio);
  ssize_t (*v_write)(struct vnode *vn, off_t off, struct kio *kio);
  int (*v_falloc)(struct vnode *vn, off_t off, size_t len);
  int (*v_map)(struct vnode *vn, off_t off, struct vm_mapping *mapping);

  // node operations
  int (*v_load)(struct vnode *vn);
  int (*v_save)(struct vnode *vn);
  int (*v_readlink)(struct vnode *vn, struct kio *kio);
  ssize_t (*v_readdir)(struct vnode *vn, off_t off, kio_t *dirbuf);

  // directory operations
  int (*v_lookup)(struct vnode *dir, cstr_t name, __move struct ventry **result);
  int (*v_create)(struct vnode *dir, cstr_t name, struct vattr *vattr, __move struct ventry **result);
  int (*v_mknod)(struct vnode *dir, cstr_t name, struct vattr *vattr, dev_t dev, __move struct ventry **result);
  int (*v_symlink)(struct vnode *dir, cstr_t name, struct vattr *vattr, cstr_t target, __move struct ventry **result);
  int (*v_hardlink)(struct vnode *dir, cstr_t name, struct vnode *target, __move struct ventry **result);
  int (*v_unlink)(struct vnode *dir, struct vnode *vn, struct ventry *ve);
  int (*v_mkdir)(struct vnode *dir, cstr_t name, struct vattr *vattr, __move struct ventry **result);
  int (*v_rmdir)(struct vnode *dir, struct vnode *vn, struct ventry *ve);
  // int (*v_rename)(struct vnode *dir, struct vnode *vn, struct ventry *old_ve, struct vnode *new_dir, cstr_t new_name);

  // lifecycle handlers
  void (*v_cleanup)(struct vnode *vn);
};


// =================================
//             ventry
// =================================

/**
 * A virtual filesystem entry.
 *
 * A ventry is a named reference to a vnode.
 */
typedef struct ventry {
  id_t id;                            // vnode id
  enum vtype type : 8;                // vnode type
  enum vstate state : 8;              // lifecycle state
  uint16_t flags;                     // vnode flags

  str_t name;                         // entry name
  hash_t hash;                        // entry name hash
  void *data;                         // filesystem private data

  mutex_t lock;                       // ventry lock
  refcount_t refcount;                // reference count

  struct vnode *vn;                   // vnode reference
  struct ventry *parent;              // parent ventry reference
  struct ventry_ops *ops;             // ventry operations

  size_t chld_count;                  // child count (V_DIR)
  LIST_HEAD(struct ventry) children;  // child ventry references (V_DIR)

  LIST_ENTRY(struct ventry) list;     // parent child list
} ventry_t;

// ventry flags
#define VE_LINKED 0x01  // ventry has been linked to a vnode
#define   VE_ISLINKED(ve) __type_checked(struct ventry *, ve, ((ve)->flags & VE_LINKED))


struct ventry_ops {
  hash_t (*v_hash)(cstr_t name);
  bool (*v_cmp)(struct ventry *ve, cstr_t name);

  void (*v_cleanup)(struct ventry *ve);
};


#endif
