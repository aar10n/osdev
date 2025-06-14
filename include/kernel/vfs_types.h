//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_VFS_TYPES_H
#define KERNEL_VFS_TYPES_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/rwlock.h>
#include <kernel/kio.h>
#include <kernel/ref.h>
#include <kernel/str.h>

#include <abi/dirent.h>
#include <abi/fcntl.h>
#include <abi/stat.h>

struct device;
struct page;
struct pgcache;
struct vm_mapping;

struct vnode;
struct vnode_ops;
struct vfs;
struct vfs_ops;
struct ventry;
struct ventry_ops;
struct vtable;
struct file;
struct file_ops;
struct ftable;

typedef struct vcache vcache_t;
typedef struct file file_t;

typedef uint64_t hash_t;


typedef struct fs_type {
  const char *name;
  int flags; // mount flags
  struct vfs_ops *vfs_ops;
  struct vnode_ops *vn_ops;
  struct ventry_ops *ve_ops;
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

// =================================
//               vfs
// =================================

/*
 * A virtual filesystem.
 */
typedef struct vfs {
  id_t id;                        // unique vfs id
  enum vstate state : 8;          // lifecycle state
  uint16_t : 8;                   // reserved
  uint16_t flags;                 // flags
  int mount_flags;                // mount flags
  void *data;                     // filesystem private data

  mtx_t lock;                     // vfs lock
  rwlock_t op_lock;               // vfs operation lock (held during vnode ops)
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
  int (*v_mount)(struct vfs *vfs, struct device *device, struct ventry *mount_ve, __move struct ventry **root);
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
  V_FIFO, // named pipe
  V_SOCK, // socket
};
#define V_TYPE_MAX V_SOCK

#define V_ISREG(v) ((v)->type == V_REG)
#define V_ISDIR(v) ((v)->type == V_DIR)
#define V_ISLNK(v) ((v)->type == V_LNK)
#define V_ISBLK(v) ((v)->type == V_BLK)
#define V_ISCHR(v) ((v)->type == V_CHR)
#define V_ISFIFO(v) ((v)->type == V_FIFO)
#define V_ISSOCK(v) ((v)->type == V_SOCK)
#define V_ISDEV(v) (V_ISBLK(v) || V_ISCHR(v))

/*
 * A virtual filesystem node.
 *
 * A vnode represents an object in a filesystem. It owns the data associated
 * with the object and must be referenced by one or more ventries to be kept
 * alive.
 */
typedef struct vnode {
  id_t id;                        // vnode id
  enum vtype type : 8;            // vnode type
  enum vstate state : 8;          // lifecycle state
  uint16_t flags;                 // vnode flags
  void *data;                     // filesystem private data

  mtx_t lock;                     // vnode lock
  rwlock_t data_lock;             // vnode file data lock
  refcount_t refcount;            // vnode reference count
  uint32_t nopen;                 // number of open file descriptors
  // cond_t waiters;                 // waiters for nopen == 0

  id_t parent_id;                 // parent vnode id
  struct vfs *vfs;                // owning vfs reference
  struct device *device;          // owning device
  struct vnode_ops *ops;          // vnode operations

  /* attributes */
  size_t nlink;                   // number of hard links
  size_t size;                    // size in bytes
  size_t blocks;                  // number of blocks
  time_t atime;                   // last access time
  time_t mtime;                   // last modification time
  time_t ctime;                   // last status change time

  /* associated data */
  struct pgcache *pgcache;        // vnode page cache
  union {
    struct device *v_dev;         // device pointer (V_BLK, V_CHR)
    str_t v_link;                 // symlink path (V_LNK)
    struct vnode *v_shadow;       // shadowed vnode (V_DIR & VN_MOUNT)
  };

  LIST_ENTRY(struct vnode) list;  // vfs vnode list
} vnode_t;

// vnode flags
#define VN_LOADED 0x01 /// vnode has been loaded
#define   VN_ISLOADED(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_LOADED))
#define VN_DIRTY  0x02 /// vnode has been modified
#define   VN_ISDIRTY(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_DIRTY))
#define VN_ROOT   0x08 /// vnode is the root of a filesystem
#define   VN_ISROOT(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_ROOT))
#define VN_OPEN   0x10 /// vnode is open (has open file descriptors)
#define   VN_ISOPEN(vn) __type_checked(struct vnode *, vn, ((vn)->flags & VN_OPEN))


struct vattr {
  enum vtype type;
  mode_t mode;
};
#define make_vattr(t, m) ((struct vattr){.type = (t), .mode = (m)})

struct vnode_ops {
  // file operations
  int (*v_open)(struct vnode *vn, int flags);
  int (*v_close)(struct vnode *vn);
  ssize_t (*v_read)(struct vnode *vn, off_t off, kio_t *kio);
  ssize_t (*v_write)(struct vnode *vn, off_t off, kio_t *kio);
  int (*v_getpage)(struct vnode *vn, off_t off, __move struct page **result);
  int (*v_falloc)(struct vnode *vn, size_t len);

  // node operations
  int (*v_load)(struct vnode *vn);
  int (*v_save)(struct vnode *vn);
  int (*v_readlink)(struct vnode *vn, kio_t *kio);
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

/*
 * A virtual filesystem reference to a vnode.
 *
 * Fields that are synced to the underlying vnode are preserved even after
 * the vnode is unlinked. The id and vfs_id fields together form a unique
 * id for the ventry<->vnode pair that remains valid until the ventry is
 * is destroyed.
 *
 * @ = updated during ve_syncvn()
 */
typedef struct ventry {
  id_t id;                            // vnode id
  enum vtype type : 8;                // vnode type
  enum vstate state : 8;              // lifecycle state @
  uint16_t flags;                     // ventry flags

  str_t name;                         // entry name
  hash_t hash;                        // entry name hash
  void *data;                         // filesystem private data

  mtx_t lock;                         // ventry lock
  refcount_t refcount;                // reference count
  id_t vfs_id;                        // vfs id @

  struct vnode *vn;                   // vnode reference
  struct ventry *parent;              // parent ventry reference
  struct ventry_ops *ops;             // ventry operations @

  size_t chld_count;                  // child count (V_DIR)
  union {
    struct ventry *mount;            // mounted root ventry reference (VE_MOUNT)
    LIST_HEAD(struct ventry) children;// child ventry references (V_DIR)
  };

  LIST_ENTRY(struct ventry) list;     // parent `children` list entry
} ventry_t;

// ventry flags
#define VE_LINKED  0x01  /// ventry has been linked to a vnode
#define   VE_ISLINKED(ve) __type_checked(struct ventry*, ve, ((ve)->flags & VE_LINKED))
#define VE_MOUNT   0x02 /// ventry is a mount point
#define   VE_ISMOUNT(ve) __type_checked(struct ventry*, ve, ((ve)->flags & VE_MOUNT))
#define VE_FSROOT  0x04 /// ventry is the filesystem root
#define   VE_ISFSROOT(ve) __type_checked(struct ventry*, ve, ((ve)->flags & VE_FSROOT))


struct ventry_ops {
  hash_t (*v_hash)(cstr_t name);
  bool (*v_cmp)(struct ventry *ve, cstr_t name);

  void (*v_cleanup)(struct ventry *ve);
};


// =================================
//              file
// =================================

enum ftype {
  FT_VNODE, // file
  FT_PIPE,  // pipe
  FT_PTS,   // pseudo-terminal slave
};

#define F_ISVNODE(f) ((f)->type == FT_VNODE)
#define F_ISPIPE(f) ((f)->type == FT_PIPE)
#define F_ISPTS(f) ((f)->type == FT_PTS)

/*
 * An open file descriptor.
 */
typedef struct file {
  int fd;               // file descriptor
  int flags;            // open flags (flags from open())
  enum ftype type;      // file type
  str_t real_path;      // full path to file

  void *data;           // file private data
  struct file_ops *ops; // file operations

  mtx_t lock;           // file lock
  refcount_t refcount;  // reference count
  off_t offset;         // current file offset
  bool closed;          // file closed
} file_t;


struct file_ops {
  int (*f_close)(file_t *file);
  ssize_t (*f_read)(file_t *file, kio_t *kio);
  ssize_t (*f_write)(file_t *file, kio_t *kio);
  int (*f_ioctl)(file_t *file, unsigned long request, void *arg);
  int (*f_stat)(file_t *file, struct stat *statbuf);
  void (*f_cleanup)(file_t *file);
};


#endif
