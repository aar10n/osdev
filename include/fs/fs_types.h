//
// Created by Aaron Gill-Braun on 2022-12-19.
//

#ifndef FS_FS_TYPES_H
#define FS_FS_TYPES_H
#define __FS_TYPES__

#include <base.h>
#include <queue.h>
#include <spinlock.h>
#include <mutex.h>

#include <abi/iov.h>
#include <abi/fcntl.h>
#include <abi/seek-whence.h>
#include <abi/stat.h>
#include <abi/vm-flags.h>

struct page;
struct vm_mapping;
struct kio;

struct fs_type;
struct super_block;
struct super_block_ops;
struct inode;
struct itable;
struct inode_ops;
struct dentry;
struct dcache;
struct dentry_out;
struct dentry_ops;
struct file;
struct file_ops;
struct device;

typedef uint64_t hash_t;

//
//
// MARK: File System
//
//

// filesystem flags
#define FS_RDONLY  0x01 // filesystem is inherently read-only

#define FS_TYPE_LOCK(fs_type) __type_checked(fs_type_t *, fs_type, SPIN_LOCK(&(fs_type)->lock))
#define FS_TYPE_UNLOCK(fs_type) __type_checked(fs_type_t *, fs_type, SPIN_UNLOCK(&(fs_type)->lock))

typedef struct fs_type {
  const char *name;                      // filesystem name
  uint32_t flags;                        // filesystem flags

  const struct super_block_ops *sb_ops;  // superblock operations
  const struct inode_ops *inode_ops;     // inode operations
  const struct dentry_ops *dentry_ops;   // dentry operations
  const struct file_ops *file_ops;       // file operations

  spinlock_t lock;                       // filesystem lock
  LIST_HEAD(struct super_block) mounts;  // mounted filesystems

  LIST_ENTRY(struct fs_type) list;       // filesystem list
} fs_type_t;

//
//
// MARK: Super Block
//
//

// helper macros
#define S_OPS(sb) __const_type_checked(super_block_t *, sb, (sb)->ops)
#define S_LOCK(sb) __type_checked(super_block_t *, sb, mutex_lock(&(sb)->lock))
#define S_UNLOCK(sb) __type_checked(super_block_t *, sb, mutex_unlock(&(sb)->lock))

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
  uint32_t mount_flags;           // mount flags (same as filesystem flags but possibly more restricted)
  mutex_t lock;                   // superblock lock
  struct fs_type *fs;             // filesystem type
  const struct super_block_ops *ops; // superblock operations

  struct dentry *mount;           // the mount point dentry
  struct itable *itable;          // inode table
  struct dcache *dcache;          // dentry cache
  struct device *device;          // block device containing the filesystem

  LIST_HEAD(struct inode) inodes; // owned inodes
  LIST_ENTRY(struct super_block) list; // superblock list
} super_block_t;

/// Describes operations that involve or relate to the superblock.
struct super_block_ops {
  /**
   * Mounts the superblock for a filesystem. \b Required.
   *
   * This is called when mounting a filesystem. It should load the superblock from
   * the block device (if required), perform any initialization of internal data
   * and fill out the relevent superblock read-write fields. The given mount dentry
   * will already have a linked inode, as well as both '.' and '..' child entries
   * attached.
   *
   * \note All read-only fields shall be initialized prior to this function.
   *
   * @param sb The superblock to be filled in.
   * @param mount The mount point dentry.
   * @return
   */
  int (*sb_mount)(struct super_block *sb, struct dentry *mount);

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
   * \note The inode I_LOADED flag will be set after this function.
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
   * \note The inode I_DIRTY flag will be cleared after this function.
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
};

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
#define I_LOADED  0x01  // inode fields have been loaded
#define I_DIRTY   0x04  // inode fields have been modified
#define I_FLLDIR  0x08  // all child entries for inode are loaded (S_IFDIR)
#define I_RAWDAT  0x10  // inode data is raw memory

// helper macros
#define I_OPS(inode) __const_type_checked(inode_t *, inode, (inode)->ops)
#define I_LOCK(inode) __type_checked(inode_t *, inode, mutex_lock(&(inode)->lock))
#define I_UNLOCK(inode) __type_checked(inode_t *, inode, mutex_unlock(&(inode)->lock))
#define I_DATA_LOCK_RO(inode) __type_checked(inode_t *, inode, rw_lock_read(&(inode)->data_lock))
#define I_DATA_UNLOCK_RO(inode) __type_checked(inode_t *, inode, rw_unlock_read(&(inode)->data_lock))
#define I_DATA_LOCK_RW(inode) __type_checked(inode_t *, inode, rw_lock_write(&(inode)->data_lock))
#define I_DATA_UNLOCK_RW(inode) __type_checked(inode_t *, inode, rw_unlock_write(&(inode)->data_lock))

#define IS_IFMNT(inode) ((inode)->mode & S_IFMNT)
#define IS_IFCHR(inode) ((inode)->mode & S_IFCHR)
#define IS_IFIFO(inode) ((inode)->mode & S_IFIFO)
#define IS_IFLNK(inode) ((inode)->mode & S_IFLNK)
#define IS_IFSOCK(inode) ((inode)->mode & S_IFSOCK)
#define IS_IFBLK(inode) ((inode)->mode & S_IFBLK)
#define IS_IFDIR(inode) ((inode)->mode & S_IFDIR)
#define IS_IFREG(inode) ((inode)->mode & S_IFREG)

#define IS_ILOADED(inode) ((inode)->flags & I_LOADED)
#define IS_IDIRTY(inode) ((inode)->flags & I_DIRTY)
#define IS_IFLLDIR(inode) ((inode)->flags & I_FLLDIR)

typedef struct inode {
  /* read-write */
  ino_t ino;                          // inode number
  mode_t mode;                        // mode bits
  uid_t uid;                          // user id of owner
  gid_t gid;                          // group id of owner
  off_t size;                         // file size in bytes
  dev_t rdev;                         // device owning inode
  time_t atime;                       // last access time
  time_t mtime;                       // last modify time
  time_t ctime;                       // last change time
  blksize_t blksize;                  // block size in bytes
  blkcnt_t blocks;                    // file size in blocks
  void *data;                         // private data

  /* read-only */
  uint32_t nlinks;                    // number of links to this inode (set initally with
  uint32_t flags;                     // inode flags
  mutex_t lock;                       // inode lock
  rw_lock_t data_lock;                // inode associated data lock

  struct super_block *sb;             // owning superblock
  const struct inode_ops *ops;        // inode operations

  // associated data
  union {
    void *i_raw;                      // raw data
    char *i_link;                     // inode symlink (S_IFLNK)
    struct dentry *i_mount;           // mount point (S_IFMNT)
    dev_t i_dev;                      // device number (S_IFCHR, S_IFBLK)
  };

  LIST_HEAD(struct dentry) links;     // list of inodes linked to inode
  LIST_ENTRY(struct inode) sb_list;   // entry in superblock list of inodes
} inode_t;

/// Describes operations that involve or relate to inodes.
struct inode_ops {
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
   * The function may choose to lazy load the inodes during its search by using
   * the d_alloc() and d_add_child() functions in the same manner as is described
   * for the i_loaddir method.
   *
   * @param inode The directory inode.
   * @param dentry The directory dentry.
   * @param name The name of the child dentry.
   * @param name_len The length of the name.
   * @param res The resulting dentry.
   * @return 0 if the dentry was found, -ENOENT if not.
   */
  int (*i_locate)(struct inode *inode, struct dentry *dentry, const char *name, size_t name_len, struct dentry **res);

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
   * \note The inode IFFLLDIR flag is set after this function returns successfully.
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
   * The inode and dentry are already linked and the inode is filled in prior to
   * this function being called. The filesystem should create a regular file entry
   * under the parent directory, and allocate any associated data and attach
   * it to the inode.
   *
   * If the inode is persistent, the filesystem should mark the inode as dirty.
   *
   * @param inode The inode for the regular file.
   * @param dentry The dentry for the inode.
   * @param dir The parent directory inode.
   * @return
   */
  int (*i_create)(struct inode *inode, struct dentry *dentry, struct inode *dir);

  /**
   * Creates a special device file. \b Optional.
   * Needed to support device file creation.
   *
   * The inode and dentry are already linked and the inode is filled in prior to
   * this function being called. The filesystem should create a special device file
   * entry under the parent directory. All file operations on this inode are handled
   * by the device subsystem and do not pass through the filesystem.
   *
   * If the inode is persistent, the filesystem should mark the inode as dirty.
   *
   * @param inode The inode for the device file.
   * @param dentry The dentry for the inode.
   * @param dir The parent directory inode.
   * @param dev The device number.
   * @return
   */
  int (*i_mknod)(struct inode *inode, struct dentry *dentry, struct inode *dir, dev_t dev);

  /**
   * Creates a symbolic link. \b Optional.
   * Needed to support symbolic links.
   *
   * The inode and dentry are already linked and the inode is filled in prior to
   * this function being called. The filesystem should create a symbolic link entry
   * under the parent directory and allocate any associated data for the inode.
   *
   * If the inode is persistent, the filesystem should mark the inode as dirty.
   *
   * @param inode The inode for the symlink.
   * @param dentry The dentry for the inode.
   * @param dir The parent directory inode.
   * @param path The path to link to.
   * @param len The length of the path.
   * @return
   */
  int (*i_symlink)(struct inode *inode, struct dentry *dentry, struct inode *dir, const char *path, size_t len);

  /**
  * Reads the contents of a symbolic link. \b Optional.
  * Needed to support symbolic links.
  *
  * This should read the contents of the symbolic link into the given buffer.
  * The buffer is guaranteed to be at least PATH_MAX bytes long and it should
  * not be null terminated.
  *
  * @param inode The symlink inode
  * @param buflen The length of the buffer.
  * @param buffer [in,out] The buffer to read into.
  * @return
  */
  int (*i_readlink)(struct inode *inode, size_t buflen, char *buffer);

  /**
   * Creates a hard link to the given inode. \b Optional.
   * Needed to support hard links (not needed for '.' and '..').
   *
   * This is called when adding a new hard link to an existing regular fileinode.
   * The inode and dentry are already linked and the inode is filled in prior to
   * this function being called. The filesystem should create a regular file entry
   * under the parent directory, and allocate any associated data and attach it to
   * the inode.
   *
   * @param inode The inode to link to.
   * @param dentry The dentry for the inode.
   * @param dir The parent directory inode.
   * @return
   */
  int (*i_hardlink)(struct inode *inode, struct dentry *dentry, struct inode *dir);

  /**
   * Unlinks a dentry from its inode. \b Optional.
   * Needed to support file deletion (unlink).
   *
   * This is called when a dentry is unlinked from its inode. The filesystem should
   * remove the entry from the parent directory and free any associated data. The
   * inode and dentry will be unlinked after this function returns, and the inode
   * will be removed by the kernel if there are no other references to it.
   *
   * If the inode is persistent, the filesystem should mark the inode as dirty.
   *
   * @param inode The inode to unlink.
   * @param dentry The dentry to unlink.
   * @param dir The parent directory inode.
   * @return
   */
  int (*i_unlink)(struct inode *inode, struct dentry *dentry, struct inode *dir);

  /**
   * Creates a directory. \b Optional.
   * Needed to support directory creation.
   *
   * The inode and dentry are already linked and the inode is filled in prior to
   * this function being called, and the dentry is prepopulated with '.' and '..'
   * entries. The filesystem should create a directory entry under the parent directory
   * and and allocate any associated data and attach it to the inode.
   *
   * If the inode is persistent, the filesystem should mark the inode as dirty.
   *
   * @param inode The inode for the directory.
   * @param dentry The dentry for the inode.
   * @param dir The parent directory inode.
   * @return
   */
  int (*i_mkdir)(struct inode *inode, struct dentry *dentry, struct inode *dir);

  /**
   * Removes a directory. \b Optional.
   * Needed to support directory deletion.
   *
   * This should remove the given dentry from the parent directory. The dentry is
   * unlinked from the inode after this function returns, and the inode is removed
   * by the kernel if there are no other references to it.
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
   * This should create a new entry under the new parent directory and remove the old
   * entry from the old parent directory. The inode is still linked to the old dentry
   * until after this function returns.
   *
   * The relinking will be done after this function.
   *
   * @param inode The inode to rename.
   * @param o_dentry The old dentry.
   * @param o_dir The parent directory inode of the old dentry.
   * @param n_dentry The new dentry.
   * @param n_dir The parent directory inode of the new dentry.
   * @return
   */
  int (*i_rename)(struct inode *inode, struct dentry *o_dentry, struct inode *o_dir, struct dentry *n_dentry, struct inode *n_dir);

  // TODO: permissions, atrributes, etc.
  // int (*i_permission)(struct inode *inode, int mask);
  // int (*i_setattr)(struct inode *inode, struct iattr *attr);
  // int (*i_getattr)(struct inode *inode, struct iattr *attr);
};

//
//
// MARK: Dentry
//
//

#define D_OPS(dentry) __const_type_checked(dentry_t *, dentry, (dentry)->ops)
#define D_LOCK(dentry) __type_checked(dentry_t *, dentry, mutex_lock(&(dentry)->lock))
#define D_UNLOCK(dentry) __type_checked(dentry_t *, dentry, mutex_unlock(&(dentry)->lock))

#define D_ISEMPTY(dentry) ((dentry)->inode == NULL ? (dentry)->nchildren == 0 : (dentry)->nchildren == 2)

typedef struct dentry {
  /* read-write */
  ino_t ino;                        // inode number
  mode_t mode;                      // dentry mode
  char *name;                       // dentry name
  size_t namelen;                   // dentry name length

  /* read-only */
  hash_t hash;                      // dentry hash
  hash_t dhash;                     // path hash (for dcache)
  mutex_t lock;                     // dentry struct lock

  struct inode *inode;              // associated inode
  struct dentry *parent;            // parent dentry
  const struct dentry_ops *ops;     // dentry operations

  uint32_t nchildren;                // number of children
  LIST_HEAD(struct dentry) children; // child inodes (S_IFDIR)

  LIST_ENTRY(struct dentry) links;  // inode->links list
  LIST_ENTRY(struct dentry) bucket; // dcache hash bucket
  LIST_ENTRY(struct dentry) list;   // sibling inodes
} dentry_t;

struct dentry_out {
  /* read-write */
  ino_t ino;    // inode number
  mode_t mode;  // dentry mode
  off_t off;    // offset in directory
  char name[NAME_MAX];
};

struct dentry_ops {
  /**
   * Compares a dentry against a name. \b Optional.
   *
   * \default If d_hash_str is set, the name is hashed and compared against the dentry hash.
   *          Otherwise, the name is compared against the dentry name.
   *
   * @param dentry The dentry to compare.
   * @param name The name to compare.
   * @param namelen The length of the name.
   * @return 0 if the dentry matches the name, non-zero otherwise.
   */
  int (*d_compare)(const struct dentry *dentry, const char *name, size_t namelen);

  /**
   * Hashes a dentry name. \b Optional.
   *
   * \default The default hasher is used.
   *
   * @param name The dentry to hash.
   * @param namelen The length of the name.
   * @param [out] hash The hash of the dentry.
   */
  void (*d_hash)(const char *name, size_t namelen, hash_t *hash);
};

//
//
// MARK: File
//
//

typedef struct file {
  /* read-only */
  int fd;                     // file descriptor
  int fd_flags;               // file descriptor flags
  int flags;                  // flags specified on open
  mode_t mode;                // file access mode
  off_t pos;                  // file offset
  uid_t uid;                  // user id
  gid_t gid;                  // group id

  char *path;                 // path to file (not always same as path used to open)
  struct inode *inode;        // associated inode
  const struct file_ops *ops; // file operations
  void *data;                 // private data
} file_t;

struct file_ops {
  /**
   * Opens a file. \b Required.
   *
   * @param file The file object.
   * @return 0 on success, -1 on error.
   */
  int (*f_open)(struct file *file);

  /**
   * Closes a file. \b Required.
   *
   * @param file The file to close.
   * @return 0 on success, -1 on error.
   */
  int (*f_close)(struct file *file);

  /**
   * Synchronizes a file. \b Optional.
   * This is called when a file should be flushed to disk.
   *
   * @param file The file to sync.
   */
  void (*f_sync)(struct file *file);

  /**
   * Truncates a file to a given length. \b Optional.
   *
   * @note If len is greater than the current file size, the file is extended with
   *       zeroed bytes.
   *
   * @param file The file to truncate.
   * @param len The length to truncate to.
   * @return
   */
  int (*f_truncate)(struct file *file, size_t len);

  /**
   * Reads from a file.
   *
   * @param file The file to read from.
   * @param off The offset to read from.
   * @param kio The kio object to read into.
   * @return
   */
  ssize_t (*f_read)(struct file *file, off_t off, struct kio *kio);

  /**
   * Writes to a file.
   *
   * @param file The file to write to.
   * @param off The offset to write to.
   * @param kio The kio object to write from.
   * @return
   */
  ssize_t (*f_write)(struct file *file, off_t off, struct kio *kio);

  /**
   * Maps a file into memory.
   *
   * @param file The file to map.
   * @param off The offset to map from.
   * @param vm The vm mapping to map into.
   * @return
   */
  int (*f_mmap)(struct file *file, off_t off, struct vm_mapping *vm);
};

#endif
