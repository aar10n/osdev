//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#ifndef FS_FS_H
#define FS_FS_H

#include <base.h>
#include <blkdev.h>
#include <chrdev.h>
#include <framebuf.h>
#include <mutex.h>
#include <queue.h>
#include <mm.h>
#include <rb_tree.h>

#define MAX_PATH 256
#define MAX_SYMLINKS 8
#define MAX_FILE_NAME 32

#define is_root(node) ((node)->parent == (node))

typedef struct file_system file_system_t;
typedef struct device device_t;
typedef struct device_ops device_ops_t;
typedef struct super_block super_block_t;
typedef struct super_block_ops super_block_ops_t;
typedef struct inode inode_t;
typedef struct inode_ops inode_ops_t;
typedef struct dentry dentry_t;
typedef struct dentry_ops dentry_ops_t;
typedef struct file file_t;
typedef struct file_ops file_ops_t;

extern dentry_t *fs_root;

/* ----- Filesystem ----- */

// filesystem flags
#define FS_READONLY 0x001 // read-only
#define FS_NO_ROOT  0x002 // no root inode

typedef struct file_system {
  char *name;                // filesystem name
  uint32_t flags;            // filesystem flags

  super_block_t *(*mount)(file_system_t *fs, blkdev_t *dev, dentry_t *mount);
  int (*post_mount)(file_system_t *fs, super_block_t *sb);

  super_block_ops_t *sb_ops; // superblock operations
  inode_ops_t *inode_ops;    // inode operations
  dentry_ops_t *dentry_ops;  // dentry operations
  file_ops_t *file_ops;      // file operations
  void *data;                // filesystem setup data
} file_system_t;

/* ----- Device ----- */

#define MAX_DEVICES 64

#define DEVICE_BLKDEV  1  // block device
#define DEVICE_CHRDEV  2  // character device
#define DEVICE_FB      3  // framebuffer

static inline dev_t makedev(uint8_t maj, uint8_t min, uint8_t unit) {
  return maj | (min << 8) | (unit << 16);
}
static inline uint8_t major(dev_t dev) {
  return dev & 0xFF;
}
static inline uint16_t minor(dev_t dev) {
  return (dev >> 8) & 0xFF;
}
static inline uint16_t unit(dev_t dev) {
  return (dev >> 16) & 0xFF;
}

typedef struct device {
  dev_t dev;                    // device id (major + minor)
  void *device;                 // device driver (blkdev, chrdev)
  device_ops_t *ops;            // common device operations
  LIST_ENTRY(device_t) devices; // devices list
} device_t;

typedef struct device_ops {
  void (*fill_inode)(device_t *device, inode_t *inode);
} device_ops_t;

/* ----- Superblock ----- */

typedef struct super_block {
  char id[32];               // volume id
  uint32_t flags;            // mount flags
  blksize_t blksize;         // block size in bytes
  dentry_t *root;            // mount dentry
  blkdev_t *dev;             // associated block device
  LIST_HEAD(inode_t) inodes; // all inodes from this fs
  file_system_t *fs;         // filesystem type
  super_block_ops_t *ops;    // superblock operations
  rb_tree_t *inode_cache;    // inode cache
  void *data;                // filesystem specific data
} super_block_t;

typedef struct super_block_ops {
  inode_t *(*alloc_inode)(super_block_t *sb);
  int (*destroy_inode)(super_block_t *sb, inode_t *inode);
  int (*read_inode)(super_block_t *sb, inode_t *inode);
  int (*write_inode)(super_block_t *sb, inode_t *inode);
} super_block_ops_t;

/* ----- Inode ----- */

// fcntl flags
#define AT_SYMLINK_FOLLOW   0x00000
#define AT_SYMLINK_NOFOLLOW 0x00001

// inode mode flags
#define I_TYPE_MASK 0x1FFF0000
#define I_PERM_MASK 0x0000FFFF
#define I_FILE_MASK (S_IFREG | S_IFDIR | S_IFLNK)
#define I_MKNOD_MASK (S_IFFBF | S_IFIFO | S_IFCHR | S_IFDIR | S_IFBLK | S_IFREG)

#define S_ISFLL  0x8000000 // Dentry is full.
#define S_ISDTY  0x4000000 // Inode is dirty.
#define S_ISLDD  0x2000000 // Inode is loaded.

#define S_IFFBF  0x1000000 // Framebuffer special.
#define S_IFMNT  0x0800000 // Filesystem mount.
#define S_IFCHR  0x0400000 // Character special (tty).
#define S_IFIFO  0x0200000 // FIFO special (pipe).
#define S_IFLNK  0x0100000 // Symbolic link.
#define S_IFSOCK 0x0080000 // Socket.
#define S_IFBLK  0x0040000 // Block special.
#define S_IFDIR  0x0020000 // Directory.
#define S_IFREG  0x0010000 // Regular.

#define S_ISUID  0x0004000 // Set-user-ID on execution.
#define S_ISGID  0x0002000 // Set-group-ID on execution.
#define S_IRWXU  0x0000700 // Read, write, execute/search by owner.
#define S_IRUSR  0x0000400 // Read permission, owner.
#define S_IWUSR  0x0000200 // Write permission, owner.
#define S_IXUSR  0x0000100 // Execute/search permission, owner.
#define S_IRWXG  0x0000070 // Read, write, execute/search by group.
#define S_IRGRP  0x0000040 // Read permission, group.
#define S_IWGRP  0x0000020 // Write permission, group.
#define S_IXGRP  0x0000010 // Execute/search permission, group.
#define S_IRWXO  0x0000007 // Read, write, execute/search by others.
#define S_IROTH  0x0000004 // Read permission, others.
#define S_IWOTH  0x0000002 // Write permission, others.
#define S_IXOTH  0x0000001 // Execute/search permission, others.

#define IS_IFFBF(mode) ((mode) & S_IFFBF)
#define IS_IFMNT(mode) ((mode) & S_IFMNT)
#define IS_IFCHR(mode) ((mode) & S_IFCHR)
#define IS_IFIFO(mode) ((mode) & S_IFIFO)
#define IS_IFLNK(mode) ((mode) & S_IFLNK)
#define IS_IFSOCK(mode) ((mode) & S_IFSOCK)
#define IS_IFBLK(mode) ((mode) & S_IFBLK)
#define IS_IFDIR(mode) ((mode) & S_IFDIR)
#define IS_IFREG(mode) ((mode) & S_IFREG)

#define IS_LOADED(mode) ((mode) & S_ISLDD)
#define IS_DIRTY(mode) ((mode) & S_ISDTY)
#define IS_FULL(mode) ((mode) & S_ISFLL)

typedef struct inode {
  ino_t ino;                    // inode number
  mode_t mode;                  // access permissions
  nlink_t nlink;                // number of hard links
  uid_t uid;                    // user id of owner
  gid_t gid;                    // group id of owner
  off_t size;                   // file size in bytes
  dev_t dev;                    // inode device
  time_t atime;                 // last access time
  time_t mtime;                 // last modify time
  time_t ctime;                 // last change time
  rw_lock_t lock;               // read/write lock
  blksize_t blksize;            // block size in bytes
  blkcnt_t blocks;              // file size in blocks
  blkdev_t *blkdev;             // block device
  page_t *pages;                // inode data pages
  LIST_HEAD(dentry_t) dentries; // list of associated dentries
  LIST_ENTRY(inode_t) inodes;   // list of inodes from same fs
  super_block_t *sb;            // super block
  inode_ops_t *ops;             // inode operations
  void *data;                   // filesystem data
} inode_t;

typedef struct inode_ops {
  int (*create)(inode_t *dir, dentry_t *dentry, mode_t mode);
  dentry_t *(*lookup)(inode_t *dir, const char *name, bool filldir);
  int (*link)(inode_t *dir, dentry_t *old_dentry, dentry_t *dentry);
  int (*unlink)(inode_t *dir, dentry_t *dentry);
  int (*symlink)(inode_t *dir, dentry_t *dentry, const char *path);
  int (*mkdir)(inode_t *dir, dentry_t *dentry, mode_t mode);
  int (*rmdir)(inode_t *dir, dentry_t *dentry);
  int (*mknod)(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev);
  int (*rename)(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry);
  int (*readlink)(dentry_t *dentry, char *buffer, int buflen);
  // int (*follow_link)(dentry_t *dentry, nameidata_t *nd);
  void (*truncate)(inode_t *inode);
  // int permission(inode_t *inode, int mask);
  // int setattr(dentry_t *dentry, iattr_t *attr);
  // int getattr(vfsmount_t *mnt, dentry_t *dentry, kstat_t *stat);
  // int setxattr(dentry_t *dentry, const char *name, const void *value, size_t size, int flags);
  // ssize_t getxattr(dentry_t *dentry, const char *name, void *value, size_t size);
  // ssize_t listxattr(dentry_t *dentry, char *list, size_t size);
  // int removexattr(dentry_t *dentry, const char *name);
} inode_ops_t;

/* ----- Dentry ----- */

typedef struct dentry {
  ino_t ino;                     // inode number
  mode_t mode;                   // dentry mode
  char name[MAX_FILE_NAME];      // dentry name
  uint32_t hash;                 // dentry hash
  inode_t *inode;                // associated inode
  dentry_t *parent;              // parent dentry
  LIST_HEAD(dentry_t) children;  // child dentries
  LIST_ENTRY(dentry_t) siblings; // sibling dentries
  LIST_ENTRY(dentry_t) dentries; // inode dentries
  LIST_ENTRY(dentry_t) bucket;   // dcache hash bucket
  dentry_ops_t *ops;             // dentry operations
} dentry_t;

typedef struct dentry_ops {
  int (*create)(dentry_t *dentry);
  // int (*delete)(dentry_t *dentry);
  // int (*compare)(dentry_t *dentry, )
} dentry_ops_t;

/* ----- File ----- */

#define OPEN_TYPE_MASK    0x1F
#define OPEN_OPTIONS_MASK 0x1FFE0

#define MAX_PROC_FILES 1024
#define DIR_FILE_FLAGS (O_DIRECTORY | O_RDONLY)

// open file flags
#define O_EXEC       0x000001
#define O_RDONLY     0x000002
#define O_RDWR       0x000004
#define O_SEARCH     0x000008
#define O_WRONLY     0x000010

#define O_APPEND     0x000020
#define O_CLOEXEC    0x000040
#define O_CREAT      0x000080
#define O_DIRECTORY  0x000100
#define O_DSYNC      0x000200
#define O_EXCL       0x000400
#define O_NOCTTY     0x000800
#define O_NOFOLLOW   0x001000
#define O_NONBLOCK   0x002000
#define O_RSYNC      0x004000
#define O_SYNC       0x008000
#define O_TRUNC      0x010000
#define O_TTY_INIT   0x020000

// Seek constants
#define SEEK_SET 1
#define SEEK_CUR 2
#define SEEK_END 3

typedef struct file {
  int fd;              // file descriptor
  dentry_t *dentry;    // associated dentry
  int flags;           // flags specified on open
  mode_t mode;         // file access mode
  off_t pos;           // file offset
  uid_t uid;           // user id
  gid_t gid;           // group id
  file_ops_t *ops;     // file operations
} file_t;

typedef struct file_ops {
  int (*open)(file_t *file, dentry_t *dentry);
  int (*flush)(file_t *file);
  ssize_t (*read)(file_t *file, char *buf, size_t count, off_t *offset);
  ssize_t (*write)(file_t *file, const char *buf, size_t count, off_t *offset);
  off_t (*lseek)(file_t *file, off_t offset, int origin);
  int (*readdir)(file_t *file, dentry_t *dirent, bool fill);
  // unsigned int (*poll)(file_t *file, poll_table_struct_t *poll_table);
  // int (*ioctl)(inode_t *inode, file_t *file, unsigned int cmd, unsigned long arg);
  int (*mmap)(file_t *file, uintptr_t vaddr, size_t len, uint16_t flags);
  // int (*release)(inode_t *inode, file_t *file);
  // int (*fsync)(file_t *file, dentry_t *dentry, int datasync);
  // int (*lock)(file_t *file, int cmd, file_lock_t *lock);
  // ssize_t (*readv)(file_t *file, const iovec_t *vector, unsigned long count, off_t *offset);
  // ssize_t (*writev)(file_t *file, const iovec_t *vector, unsigned long count, off_t *offset);
} file_ops_t;

// stat

typedef struct kstat {
  dev_t dev;
  ino_t ino;
  mode_t mode;
  nlink_t nlink;
  uid_t uid;
  gid_t gid;
  off_t size;
  blksize_t blksize;
  blkcnt_t blkcnt;
  inode_t *inode;
} kstat_t;

/* ----- Mmap ----- */

// prot
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

// flags
#define MAP_SHARED    0
#define MAP_PRIVATE   1
#define MAP_FIXED     2

#define MAP_FAILED NULL


/* ----- API ----- */

void fs_init();
int fs_register(file_system_t *fs);
dev_t fs_register_blkdev(uint8_t minor, blkdev_t *blkdev, device_ops_t *ops);
dev_t fs_register_chrdev(uint8_t minor, chrdev_t *chrdev, device_ops_t *ops);
dev_t fs_register_framebuf(uint8_t minor, framebuf_t *framebuf, device_ops_t *ops);

int fs_mount(const char *path, const char *device, const char *format);
int fs_unmount(const char *path);

int fs_open(const char *path, int flags, mode_t mode);
int fs_creat(const char *path, mode_t mode);
int fs_mkdir(const char *path, mode_t mode);
int fs_mknod(const char *path, mode_t mode, dev_t dev);
int fs_close(int fd);

int fs_stat(const char *path, kstat_t *statbuf);
int fs_fstat(int fd, kstat_t *statbuf);

ssize_t fs_read(int fd, void *buf, size_t nbytes);
ssize_t fs_write(int fd, void *buf, size_t nbytes);
off_t fs_lseek(int fd, off_t offset, int whence);

dentry_t *fs_readdir(int fd);
long fs_telldir(int fd);
void fs_seekdir(int fd, long loc);
void fs_rewinddir(int fd);

int fs_link(const char *path1, const char *path2);
int fs_unlink(const char *path);
int fs_symlink(const char *path1, const char *path2);
int fs_rename(const char *oldfile, const char *newfile);
int fs_rmdir(const char *path);
int fs_chdir(const char *path);
int fs_chmod(const char *path, mode_t mode);
int fs_chown(const char *path, uid_t owner, gid_t group);

char *fs_getcwd(char *buf, size_t size);

void *fs_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int fs_munmap(void *addr, size_t len);

#endif
