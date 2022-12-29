//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#ifndef FS_FS_H
#define FS_FS_H

#include <fs_types.h>

#define MAP_FAILED ((void *)F_ERROR)


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

/* ----- API ----- */

/**
 * Initializes the file system module.
 *
 * This function should be called before any other file system functions are used.
 */
void fs_init();

/**
 * Registers a new file system type.
 *
 * @param fs_type The file system type to register.
 * @return
 */
int fs_register_type(fs_type_t *fs_type);

//
// MARK: Mounting and unmounting
//

/**
 * Mounts a file system at the specified mount point.
 *
 * @param source The source device or file to mount.
 * @param mount The mount point.
 * @param fs_type The file system type to use.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_mount(const char *source, const char *mount, const char *fs_type);

/**
 * Unmounts a file system at the specified mount point.
 *
 * @param path The mount point of the file system to unmount.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_unmount(const char *path);

//
// MARK: File and directory metadata
//

/**
 * Gets the metadata for a file or directory.
 *
 * @param path The path to the file or directory.
 * @param stat A pointer to a `struct stat` where the metadata will be stored.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_stat(const char *path, struct stat *stat);

/**
 * Gets the metadata for an open file.
 *
 * @param fd The file descriptor of the open file.
 * @param stat A pointer to a `struct stat` where the metadata will be stored.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_fstat(int fd, struct stat *stat);

//
// MARK: File and directory manipulation
//

/**
 * Opens a file.
 *
 * @param path The path to the file.
 * @param flags Flags indicating how the file should be opened.
 * @param mode The file permissions to use if the file is being created.
 * @return A file descriptor on success, F_ERROR on failure.
 */
int fs_open(const char *path, int flags, mode_t mode);

/**
 * Creates a new file.
 *
 * If the file already exists, it is truncated to zero length.
 *
 * @param path The path to the file to create.
 * @param mode The file permissions to use.
 * @return A file descriptor on success, F_ERROR on failure.
 */
int fs_creat(const char *path, mode_t mode);

/**
 * Creates a new directory.
 *
 * @param path The path to the directory to create.
 * @param mode The directory permissions to use.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_mkdir(const char *path, mode_t mode);

/**
 * Creates a new file or special file.
 *
 * @param path The path to the file to create.
 * @param mode The file permissions to use.
 * @param dev The device identifier for a special file.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_mknod(const char *path, mode_t mode, dev_t dev);

/**
 * Closes an open file.
 *
 * @param fd The file descriptor of the open file.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_close(int fd);

//
// MARK: File read and write operations
//

/**
 * Reads data from an open file.
 *
 * @param fd The file descriptor of the open file.
 * @param buf A buffer to store the read data.
 * @param nbytes The maximum number of bytes to read.
 * @return The number of bytes read on success, F_ERROR on failure.
 */
ssize_t fs_read(int fd, void *buf, size_t nbytes);

/**
 * Writes data to an open file.
 *
 * @param fd The file descriptor of the open file.
 * @param buf A buffer containing the data to write.
 * @param nbytes The number of bytes to write.
 * @return The number of bytes written on success, F_ERROR on failure.
 */
ssize_t fs_write(int fd, void *buf, size_t nbytes);

/**
 * Changes the read/write position of an open file.
 *
 * @param fd The file descriptor of the open file.
 * @param offset The new position relative to the whence parameter.
 * @param whence The reference point for the offset parameter.
 * @return The new position on success, F_ERROR on failure.
 */
off_t fs_lseek(int fd, off_t offset, int whence);

/**
 *
 * Reads data from an open file using multiple buffers.
 * @param fd The file descriptor of the open file.
 * @param iov An array of struct iovec buffers to store the read data.
 * @param iovcnt The number of buffers in the iov array.
 * @return The number of bytes read on success, F_ERROR on failure.
 */
ssize_t fs_readv(int fd, const struct iovec *iov, int iovcnt);

/**
 * Writes data to an open file using multiple buffers.
 *
 * @param fd The file descriptor of the open file.
 * @param iov An array of struct iovec buffers containing the data to write.
 * @param iovcnt The number of buffers in the iov array.
 * @return The number of bytes written on success, F_ERROR on failure.
 */
ssize_t fs_writev(int fd, const struct iovec *iov, int iovcnt);

/**
 * Reads data from a specific position in an open file using multiple buffers.
 *
 * @param fd The file descriptor of the open file.
 * @param iov An array of `struct iovec` buffers to store the read data.
 * @param iovcnt The number of buffers in the `iov` array.
 * @param offset The position in the file to read from.
 * @return The number of bytes read on success, F_ERROR on failure.
 */
ssize_t fs_preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);

/**
 * Writes data to a specific position in an open file using multiple buffers.
 *
 * @param fd The file descriptor of the open file.
 * @param iov An array of `struct iovec` buffers containing the data to write.
 * @param iovcnt The number of buffers in the `iov` array.
 * @param offset The position in the file to write to.
 *
 * @return The number of bytes written on success, F_ERROR on failure.
 */
ssize_t fs_pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);

//
// MARK: File descriptor manipulation
//

/**
 * Duplicates an open file descriptor.
 *
 * @param fd The file descriptor to duplicate.
 * @return The new file descriptor on success, F_ERROR on failure.
 */
int fs_dup(int fd);

/**
 * Duplicates an open file descriptor to a specific descriptor number.
 *
 * @param fd The file descriptor to duplicate.
 * @param fd2 The descriptor number to use for the duplicate.
 * @return The new file descriptor on success, F_ERROR on failure.
 */
int fs_dup2(int fd, int fd2);

/**
 * Performs file control operations on an open file.
 *
 * @param fd The file descriptor of the open file.
 * @param cmd The operation to perform.
 * @param arg An argument for the operation.
 * @return The result of the file control operation, or F_ERROR on failure.
 */
int fs_fcntl(int fd, int cmd, uint64_t arg);

//
// MARK: Directory iteration
//

/**
 * Reads the next directory entry from an open directory.
 *
 * @param fd The file descriptor of the open directory.
 * @return A pointer to a `dentry_t` structure on success, `NULL` on failure or end of directory.
 */
dentry_t *fs_readdir(int fd);

/**
 * Gets the current position in an open directory.
 *
 * @param fd The file descriptor of the open directory.
 * @return The current position on success, F_ERROR on failure.
 */
long fs_telldir(int fd);

/**
 * Sets the position in an open directory.
 *
 * @param fd The file descriptor of the open directory.
 * @param loc The new position.
 */
void fs_seekdir(int fd, long loc);

/**
 * Resets the position in an open directory to the beginning.
 *
 * @param fd The file descriptor of the open directory.
 */
void fs_rewinddir(int fd);

//
// MARK: File and directory linking
//

/**
 * Creates a hard link to a file.
 *
 * @param path1 The path to the file to link to.
 * @param path2 The path to the new link.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_link(const char *path1, const char *path2);

/**
 * Removes a link to a file.
 * If the file is not a link or has multiple links, it is deleted when all links to it are removed.
 *
 * @param path The path to the link to remove.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_unlink(const char *path);

/**
 * Creates a symbolic link to a file.
 *
 * @param path1 The path to the file to link to.
 * @param path2 The path to the new symbolic link.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_symlink(const char *path1, const char *path2);

/**
 * Renames a file or directory.
 *
 * @param oldfile The current name of the file or directory.
 * @param newfile The new name for the file or directory.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_rename(const char *oldfile, const char *newfile);

/**
 * Reads the target of a symbolic link.
 *
 * @param path The path to the symbolic link.
 * @param buf A buffer to store the target path.
 * @param bufsize The size of the `buf` buffer.
 * @return The number of bytes read on success, F_ERROR on failure.
 */
ssize_t fs_readlink(const char *restrict path, char *restrict buf, size_t bufsize);

/**
 * Removes a directory.
 *
 * The directory must be empty.
 *
 * @param path The path to the directory to remove.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_rmdir(const char *path);

//
// MARK: Current working directory and file system access
//

/**
 * Changes the current working directory.
 *
 * @param path The path to the new current working directory.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_chdir(const char *path);

/**
 * Changes the permissions of a file or directory.
 *
 * @param path The path to the file or directory.
 * @param mode The new permissions to use.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_chmod(const char *path, mode_t mode);

/**
 * Changes the owner and group of a file or directory.
 *
 * @param path The path to the file or directory.
 * @param owner The new owner identifier.
 * @param group The new group identifier.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_chown(const char *path, uid_t owner, gid_t group);

/**
 * Gets the current working directory.
 *
 * @param buf A buffer to store the path.
 * @param size The size of the `buf` buffer.
 * @return A pointer to the `buf` buffer on success, `NULL` on failure.
 */
char *fs_getcwd(char *buf, size_t size);

//
// MARK: Memory mapping
//

/**
 * Maps a file into memory.
 *
 * @param addr A hint for the address to map the file to.
 * @param len The number of bytes to map.
 * @param prot The memory protection to use.
 * @param flags The mapping flags to use.
 * @param fd The file descriptor of the open file.
 * @param off The offset in the file to start mapping from.
 * @return A pointer to the mapped memory on success, `MAP_FAILED` on failure.
 */
void *fs_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);

/**
 * Unmaps a file from memory.
 *
 * @param addr The start address of the mapped memory.
 * @param len The number of bytes to unmap.
 * @return F_OK on success, F_ERROR on failure.
 */
int fs_munmap(void *addr, size_t len);

#endif
