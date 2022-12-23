//
// Created by Aaron Gill-Braun on 2022-12-19.
//

#ifndef FS_FS_TYPES_H
#define FS_FS_TYPES_H

#include <base.h>
#include <queue.h>
#include <iov.h>
#include <spinlock.h>
#include <mutex.h>

#include <abi/fcntl.h>
#include <abi/seek-whence.h>
#include <abi/stat.h>
#include <abi/vm-flags.h>

struct page;

struct fs_type;
struct super_block;
struct super_block_ops;
struct inode;
struct inode_ops;
struct dentry;
struct dentry_out;
struct dentry_ops;
struct file;
struct file_ops;
struct device;
struct device_ops;

struct bdev;
struct bdev_ops;
struct cdev;
struct cdev_ops;

struct vm_mapping;

//
//
// MARK: File System
//
//

// filesystem flags
#define FS_RDONLY  0x01 // filesystem is inherently read-only
#define FS_VIRTUAL 0x02 // filesystem is purely in-memory (not backed by disk)

typedef struct fs_type {
  const char *name;                 // filesystem name
  uint32_t flags;                   // filesystem flags

  struct super_block_ops *sb_ops;   // superblock operations
  struct inode_ops *inode_ops;      // inode operations
  struct dentry_ops *dentry_ops;    // dentry operations
  struct file_ops *file_ops;        // file operations
} fs_type_t;

//
//
// MARK: Super Block
//
//

#define IS_RDONLY(sb) ((sb)->mount_flags & FS_RDONLY)

// All read-write fields except for data should only be written to once
// during the call to super_init. Otherwise they should be read-only.
typedef struct super_block {
  /* read-write */
  char *label;                    // volume label
  size_t total_size;              // total size of the filesystem
  size_t block_size;              // block size in bytes
  size_t ino_count;               // number of inodes in the filesystem
  void *data;                     // private data

  /* read-only */
  uint32_t flags;                 // superblock flags
  uint32_t mount_flags;           // mount flags (same as filesystem flags but possibly more restricted)
  mutex_t lock;

  struct dentry *mount;           // the mount point dentry
  struct bdev *bdev;              // block device containing the filesystem
  struct fs_type *fs;             // filesystem type
  struct super_block_ops *ops;    // superblock operations

  LIST_HEAD(struct inode) inodes; // owned inodes
} super_block_t;

/// Describes operations that involve or relate to the superblock.
typedef struct super_block_ops {
  /**
   * Mounts the superblock for a filesystem. \b Required.
   *
   * This is called when mounting a filesystem. It should load the superblock from
   * the block device (if required), perform any initialization of internal data
   * and fill out the relevent superblock read-write fields.
   *
   * By default the vfs allocates a new virtual inode 0 to act as the root before
   * this function is called. If the filesystem has its own internal concept of a
   * root node, it should take ownership over the root using i_takeown() to ensure
   * it uses the filesystem inode_ops. In either case the function is free to modify
   * the root inode's read-write fields
   *
   * \note All read-only fields shall be initialized prior to this function.
   *
   * @param sb [in,out] The superblock to be filled in.
   * @param root [in,out] The root inode for the filesystem.
   * @return
   */
  int (*sb_mount)(struct super_block *sb, struct inode *root);

  /**
   * Unmounts the superblock for a filesystem. \b Required.
   *
   * This is called when unmounting a filesystem and should perform any cleanup
   * of internal data. It does not need to sync the superblock or any inodes as
   * that is handled before this is called.
   *
   * @param sb The superblock being unmounted.
   * @return
   */
  int (*sb_unmount)(struct super_block *sb);

  /**
   * Write the superblock to the on-device filesystem. \b Required if not read-only.
   *
   * This should write the superblock to the on-device filesystem. It is called
   * when certain read-write fields change to sync the changes to disk.
   *
   * @param sb The superblock to write.
   * @return
   */
  int (*sb_write)(struct super_block *sb);

  /**
   * Reads an inode from the filesystem. \b Required.
   *
   * This should load the inode (specified by the given inode's ino field) from
   * the superblock and fill in the relevent read-write fields.
   *
   * \note The inode IFLOADED flag will be set after this function.
   *
   * @param sb The superblock to read the inode from.
   * @param inode [in,out] The inode to be filled in.
   * @return
   */
  int (*sb_read_inode)(struct super_block *sb, struct inode *inode);

  /**
   * Writes an inode to the on-device filesystem. \b Required if not read-only.
   *
   * This should write the given inode to the on-device superblock and it is called
   * when certain read-write fields change.
   *
   * \note The inode IFDIRTY flag will be cleared after this function.
   *
   * @param sb The superblock to write the inode to.
   * @param inode The inode to be writen.
   * @return
   */
  int (*sb_write_inode)(struct super_block *sb, struct inode *inode);

  /**
   * Allocates a new inode in the superblock. \b Required if not read-only.
   *
   * This should allocate a new inode in the superblock and then fill in the
   * provided inode with the ino number. It should not pre-allocate any blocks
   * for associated data.
   *
   * @param sb The superblock to allocate the inode in.
   * @param inode [in,out] The inode to be filled in.
   * @return
   */
  int (*sb_alloc_inode)(struct super_block *sb, struct inode *inode);

  /**
   * Deletes an inode from the superblock. \b Required if not read-only.
   *
   * This should delete the given inode from the superblock and release any data
   * data blocks still held by this inode. It should also assume that there are
   * no links to the inode and nlinks is 0.
   *
   * @param sb The superblock to write the inode to.
   * @param inode The inode to be deleted.
   * @return
   */
  int (*sb_delete_inode)(struct super_block *sb, struct inode *inode);
} super_block_ops_t;

//
//
// MARK: Inode
//
//

// inode mode flags
#define I_TYPE_MASK 0x1FFF0000
#define I_PERM_MASK 0x0000FFFF
#define I_FILE_MASK (S_IFREG | S_IFDIR | S_IFLNK)
#define I_MKNOD_MASK (S_IFFBF | S_IFIFO | S_IFCHR | S_IFDIR | S_IFBLK | S_IFREG)

// inode flags
#define IFLOADED  0x01  // inode fields have been loaded
#define IFDIRTY   0x04  // inode fields have been modified
#define IFFLLDIR  0x08  // all child entries for inode are loaded (S_IFDIR)
#define IFRAW     0x10  // inode data is raw memory

typedef struct inode {
  /* read-write */
  ino_t ino;                          // inode number
  mode_t mode;                        // mode bits
  uid_t uid;                          // user id of owner
  gid_t gid;                          // group id of owner
  off_t size;                         // file size in bytes
  dev_t rdev;                         // inode owning device
  time_t atime;                       // last access time
  time_t mtime;                       // last modify time
  time_t ctime;                       // last change time
  blksize_t blksize;                  // block size in bytes
  blkcnt_t blocks;                    // file size in blocks
  void *data;                         // private data

  /* read-only */
  uint32_t nlinks;                    // number of links to this inode (set initally with
  uint32_t flags;                     // inode flags
  spinlock_t lock;                    // inode struct lock
  mutex_t data_lock;                  // inode associated data lock

  struct super_block *sb;             // owning superblock
  struct inode_ops *ops;              // inode operations

  // associated data
  union {
    void *i_raw;                      // inode raw data
    struct page *i_pages;             // inode mapped pages
    struct bdev *i_bdev;              // inode block device (S_IFBLK)
    struct cdev *i_cdev;              // inode character device (S_IFCHR)
    char *i_link;                     // inode symlink (S_IFLNK)
    struct inode *i_mount;            // inode shadowed by mount (S_IFMNT)
  };

  LIST_HEAD(struct dentry) links;     // list of dentries linked to inode
  LIST_ENTRY(struct inode) sb_list;   // entry in superblock list of inodes
} inode_t;

/// Describes operations that involve or relate to inodes.
typedef struct inode_ops {
  /**
   * Searches for a dentry by name in a given directory. \b Optional.
   * By default this will use i_loaddir() to load the directory and then search
   * the children for the given name using d_locate_child().
   *
   * This should search for an entry matching the given name in the inode which
   * may only be partially loaded or empty. If the dentry has children, those
   * can be searched through using the d_compare() function. If the IFFLLDIR flag
   * is not set, the function must search through the remaining entries on the
   * device.
   *
   * The function may choose to lazy load the dentries during its search by using
   * the d_alloc() and d_add_child() functions in the same manner as is described
   * for the i_loaddir method.
   *
   * @param inode The directory inode.
   * @param dentry The directory dentry.
   * @param name The name of the child dentry.
   * @return The dentry if found or NULL.
   */
  struct dentry *(*i_locate)(struct inode *inode, struct dentry *dentry, const char *name);

  /**
   * Loads the directory entries for a given inode. \b Required.
   *
   * This should load the entries for the given directory inode and create children
   * under the provided dentry. The dentry comes pre-populated with '.' and '..'
   * entries which are owned by the vfs. For each entry this function should allocate
   * a new dentry with d_alloc(), fill in the read-write fields, then add it under the
   * parent with d_add_child().
   *
   * It is possible that the directory already has some of the children loaded.
   * The function should handle this by skipping the existing entries and loading
   * just the remaining ones.
   *
   * \note The inode IFFLLDIR flag is set after this function.
   *
   * @param inode The directory inode.
   * @param dentry [in,out] The directory dentry we attach the children to.
   * @return
   */
  int (*i_loaddir)(struct inode *inode, struct dentry *dentry);

  /**
   * Creates a regular file associated with the given inode. \b Optional.
   * Needed to support file creation.
   *
   * This should create a regular file entry in the parent directory. The inode and
   * dentry are both filled in and linked before this is called. If the blocks field
   * of the inode is non-zero, this function may want to preallocate some or all of
   * the requested blocks.
   *
   * @param dir The parent directory inode.
   * @param inode The regular file inode.
   * @param dentry The dentry for the inode.
   * @return
   */
  int (*i_create)(struct inode *dir, struct inode *inode, struct dentry *dentry);

  /**
   * Creates a special device file. \b Optional.
   * Needed to support device file creation.
   *
   * This should create a special device file entry with dev in the parent directory.
   * The dentry is filled and linked to the inode before this is called. The filesystem
   * is not involved when the device file is opened.
   *
   * @param dir The parent directory inode.
   * @param dentry The dentry for the inode.
   * @param dev The device number.
   * @return
   */
  int (*i_mknod)(struct inode *dir, struct dentry *dentry, dev_t dev);

  /**
   * Creates a hard link to the given inode. \b Optional.
   * Needed to support hard links (not needed for '.' and '..').
   *
   * This should create a hard link entry to the given inode in the parent directory.
   * The dentry is filled and linked to the inode before this is called.
   *
   * @param dir The parent directory inode.
   * @param inode The inode to link to.
   * @param dentry The dentry for the inode.
   * @return
   */
  int (*i_link)(struct inode *dir, struct inode *inode, struct dentry *dentry);

  /**
   * Unlinks a dentry from its inode. \b Optional.
   * Needed to support file deletion (unlink).
   *
   * This should remove the given dentry from the parent directory. The dentry is
   * unlinked from the inode before this is called.
   *
   * @param dir The parent directory inode.
   * @param dentry The dentry to unlink.
   * @return
   */
  int (*i_unlink)(struct inode *dir, struct dentry *dentry);

  /**
   * Creates a symbolic link. \b Optional.
   * Needed to support symbolic links.
   *
   * This should create a symbolic link entry in the parent directory. The inode and
   * dentry are both filled in and linked before this is called.
   *
   * @param dir The parent directory inode.
   * @param dentry The dentry for the inode.
   * @param path The path to link to.
   * @return
   */
  int (*i_symlink)(struct inode *dir, struct inode *dentry, const char *path);

  /**
   * Reads the contents of a symbolic link. \b Optional.
   * Needed to support symbolic links.
   *
   * This should read the contents of the symbolic link into the given buffer.
   * The buffer is guaranteed to be at least PATH_MAX bytes long.
   *
   * @param dentry The dentry for the inode.
   * @param buflen The length of the buffer.
   * @param buffer [in,out]The buffer to read into.
   * @return
   */
  int (*i_readlink)(struct dentry *dentry, size_t buflen, char *buffer);

  /**
   * Creates a directory. \b Optional.
   * Needed to support directory creation.
   *
   * This should create a directory entry in the parent directory. The inode and
   * dentry are both filled in, and the directory has both the '.' and '..' entries
   * prepopulated before this is called.
   *
   * @param dir The parent directory inode.
   * @param dentry The dentry for the inode.
   * @return
   */
  int (*i_mkdir)(struct inode *dir, struct dentry *dentry);

  /**
   * Removes a directory. \b Optional.
   * Needed to support directory deletion.
   *
   * This should remove the given dentry from the parent directory. The dentry is
   * unlinked from the inode before this is called. The directory must be empty.
   *
   * @param dir The parent directory inode.
   * @param dentry The dentry to unlink.
   * @return
   */
  int (*i_rmdir)(struct inode *dir, struct dentry *dentry);

  /**
   * Renames a dentry. \b Optional.
   * Needed to support file renaming.
   *
   * This should rename the given dentry in the parent directory. The old dentry
   * is still linked to the inode and the new dentry is filled but unlinked before
   * this is called.
   *
   * The relinking will be done after this function.
   *
   * @param o_dir The parent directory inode of the old dentry.
   * @param o_dentry The old dentry.
   * @param n_dir The parent directory inode of the new dentry.
   * @param n_dentry The new dentry.
   * @return
   */
  int (*i_rename)(struct inode *o_dir, struct dentry *o_dentry, struct inode *n_dir, struct dentry *n_dentry);

  // TODO: permissions, atrributes, etc.
  // int (*i_permission)(struct inode *inode, int mask);
  // int (*i_setattr)(struct inode *inode, struct iattr *attr);
  // int (*i_getattr)(struct inode *inode, struct iattr *attr);
} inode_ops_t;

// inode helper macros
#define IS_IFFBF(inode) ((inode)->mode & S_IFFBF)
#define IS_IFMNT(inode) ((inode)->mode & S_IFMNT)
#define IS_IFCHR(inode) ((inode)->mode & S_IFCHR)
#define IS_IFIFO(inode) ((inode)->mode & S_IFIFO)
#define IS_IFLNK(inode) ((inode)->mode & S_IFLNK)
#define IS_IFSOCK(inode) ((inode)->mode & S_IFSOCK)
#define IS_IFBLK(inode) ((inode)->mode & S_IFBLK)
#define IS_IFDIR(inode) ((inode)->mode & S_IFDIR)
#define IS_IFREG(inode) ((inode)->mode & S_IFREG)

#define IS_LOADED(inode) ((inode)->flags & IFLOADED)
#define IS_DIRTY(inode) ((inode)->flags & IFDIRTY)

// ============ Inode API ============
// The functions below operate on inode objects and most of them are interfaces
// to the underlying inode ops and handle the setup/teardown of the calls. These
// should almost never be called from any *_ops functions unless otherwise noted.

/// Allocates an empty inode object. The inode is not a part of any filesystem.
struct inode *i_alloc();
/// Frees an inode object. The inode should be unlinked and have no associated data.
void i_free(struct inode *inode);
/// Binds an inode object to an owning filesystem. The inode should be empty and
/// not be a part of any other filesystem. The inode number should be set to a
/// unique and valid value for the filesystem before calling this.
void i_takeown(struct inode *inode, struct super_block *sb);
/// Adds the given inode to the filesystem inode table.
int i_table_add(struct inode *inode);
/// Removes the given inode from the filesystem inode table.
int i_table_remove(struct inode *inode);

struct dentry *i_locate(struct inode *inode, struct dentry *dentry, const char *name);
int i_loaddir(struct inode *inode, struct dentry *dentry);
int i_create(struct inode *dir, struct inode *inode, struct dentry *dentry);
int i_mknod(struct inode *dir, struct dentry *dentry, dev_t dev);
int i_link(struct inode *dir, struct inode *inode, struct dentry *dentry);
int i_unlink(struct inode *dir, struct dentry *dentry);
int i_symlink(struct inode *dir, struct inode *dentry, const char *path);
int i_readlink(struct dentry *dentry, size_t buflen, char *buffer);
int i_mkdir(struct inode *dir, struct dentry *dentry);
int i_rmdir(struct inode *dir, struct dentry *dentry);
int i_rename(struct inode *o_dir, struct dentry *o_dentry, struct inode *n_dir, struct dentry *n_dentry);

//
//
// MARK: Dentry
//
//

typedef struct dentry {
  /* read-write */
  ino_t ino;                        // inode number
  mode_t mode;                      // dentry mode
  char *name;                       // dentry name
  size_t namelen;                   // dentry name length
  uint64_t hash;                    // dentry hash

  /* read-only */
  struct inode *inode;              // associated inode
  struct dentry *parent;            // parent dentry
  struct dentry_ops *ops;           // dentry operations

  LIST_ENTRY(struct dentry) links;  // inode->links list
  LIST_ENTRY(struct dentry) bucket; // dcache hash bucket
  LIST_ENTRY(struct dentry) list;   // sibling dentries

  union {
    LIST_HEAD(struct dentry) d_children; // child dentries (S_IFDIR)
  };
} dentry_t;

struct dentry_out {
  /* read-write */
  ino_t ino;    // inode number
  mode_t mode;  // dentry mode
  off_t off;    // offset in directory
  char name[NAME_MAX];
};

typedef struct dentry_ops {
  /**
   * Hashes a dentry. \b Optional.
   * By default, dentries are hashed by name
   *
   * @param dentry The dentry to hash.
   * @param [out] hash The hash of the dentry.
   */
  void (*d_hash)(const struct dentry *dentry, uint64_t *hash);

  /**
   * Compares a dentry against a name. \b Optional.
   * By default, dentries are compared by hash.
   *
   * \default
   *
   * @param dentry The dentry to compare.
   * @param name The name to compare.
   * @param namelen The length of the name.
   * @return 0 if the dentry matches the name, non-zero otherwise.
   */
  int (*d_compare)(const struct dentry *dentry, const char *name, size_t namelen);
} dentry_ops_t;

/// ============ Dentry API ============
/// The functions below operate on the vfs only and do not make any changes to the
/// underlying filesystem. The only functions which might call into the dentry ops
/// are d_compare() and d_hash(), which have default implementations.

/// Allocates an empty dentry.
struct dentry *d_alloc(ino_t ino, mode_t mode, const char *name);
/// Frees a dentry. It must not be linked to an inode and must not have any children.
void d_free(struct dentry *dentry);
/// Links the dentry to the inode and increments nlink by one.
void d_attach(struct dentry *dentry, struct inode *inode);
/// Unlinks the dentry from the inode and decrements nlink by one.
void d_detach(struct dentry *dentry);
/// Adds a child dentry to the parent dentry. The parent must be a directory.
void d_add_child(struct dentry *parent, struct dentry *child);
/// Removes a child dentry from the parent dentry. The parent must be a directory.
void d_remove_child(struct dentry *parent, struct dentry *child);
/// Finds a child dentry in the parent dentry using d_compare(). The parent must
/// be a directory, and this assumes that all children are loaded.
struct dentry *d_locate_child(struct dentry *parent, const char *name);
/// Hashes a dentry and sets the hash field.
void d_hash(struct dentry *dentry);
/// Compares a dentry to a name. Returns 0 if they match.
int d_compare(struct dentry *dentry, const char *name, size_t len);

//
//
// MARK: File
//
//

typedef struct file {
  /* read-only */
  int fd;               // file descriptor
  int fd_flags;         // file descriptor flags
  int flags;            // flags specified on open
  mode_t mode;          // file access mode
  off_t pos;            // file offset
  uid_t uid;            // user id
  gid_t gid;            // group id

  const char *path;     // path to file (not always same as path used to open)
  struct inode *inode;  // associated inode
  struct file_ops *ops; // file operations
} file_t;

typedef struct file_ops {
  /**
   * Reads from a file. \b Required.
   *
   * This function should read bytes from the file into the buffers specified by
   * the iov array. The read should start at the given offset and read the data
   * into the buffers one at a time until the buffers are full or the end of the
   * file is reached.
   *
   * \note Both f_read() and f_write() follow the same rules as the preadv() and
   * pwritev() functions.
   *
   * @param file The file to read from.
   * @param iov The array of iov buffers to read into.
   * @param iovcnt The number of iov entries.
   * @param offset The offset to start reading from.
   * @return
   */
  ssize_t (*f_read)(struct file *file, struct iovec *iov, int iovcnt, off_t offset);

  /**
   * Writes to a file. \b Required if not read-only.
   *
   * This function should write bytes from the buffers specified by the iov array
   * into the file. The write should start at the given offset and write the data
   * from the buffers one at a time until all the buffers have been written.
   *
   * \note Both f_read() and f_write() follow the same rules as the preadv() and
   * pwritev() functions.
   *
   * @param file The file to write to.
   * @param iov The array of iov buffers to write from.
   * @param iovcnt The number of iov entries.
   * @param offset The offset to start write from.
   * @return
   */
  ssize_t (*f_write)(struct file *file, const struct iovec *iov, int iovcnt, off_t offset);

  // ============ Overrides ============
  // The functions below are all optional and each have a default implementation.
  // In other words, things will work without them but they may not be optimal for
  // the filesystem.

  /**
   * Opens a file. \b Optional.
   *
   * This is called on file open(). All fields in the file object are set before
   * this is called. The file object is not added to the file table until after
   * the function.
   *
   * \default Does nothing and returns F_OK.
   *
   * @param file The file object.
   * @return
   */
  int (*f_open)(struct file *file);

  /**
   * Synchronizes a file. \b Optional.
   *
   * This is called when close() and fsync() are called on a file. It should
   * synchronize the file with the underlying filesystem.
   *
   * \default Does nothing and returns F_OK.
   *
   * @param file The file to synchronize.
   * @return
   */
  int (*f_sync)(struct file *file);

  /**
   * Reads a directory entry. \b Optional.
   *
   * This is called when readdir() is called on the directory. It should read the
   * next directory entry and fill in the dentry_out fields of outp.
   *
   * \default The directory children are loaded with i_loaddir() (if needed) and
   * the entries are returned one-by-one until the end of the directory is reached.
   *
   * @param file The file to read from.
   * @param outp The dentry_out struct to fill.
   * @return
   */
  int (*f_readdir)(struct file *file, struct dentry_out *outp);

  // TODO: mmap, poll, ioctl, etc.
} file_ops_t;

//
// MARK: File System Errors
//

#define F_OK      0 // 'success' value
#define F_ERROR  -1 // generic error

#endif
