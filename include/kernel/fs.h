//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_FS_H
#define KERNEL_FS_H

#include <kernel/vfs_types.h>
#include <kernel/str.h>

#include <abi/mman.h>
#include <abi/poll.h>

struct proc;
struct page;
struct vm_file;

void fs_init();
void fs_setup_mounts();

int fs_register_type(fs_type_t *fs_type);
fs_type_t *fs_get_type(const char *type);
__ref ventry_t *fs_root_getref();

int fs_mount(cstr_t source, cstr_t mount, const char *fs_type, int flags);
int fs_replace_root(cstr_t new_root);
int fs_unmount(cstr_t path);

int fs_proc_alloc_fd(struct proc *proc);
void fs_proc_free_fd(struct proc *proc, int fd);
__ref fd_entry_t *fs_proc_get_fdentry(struct proc *proc, int fd);
void fs_proc_add_fdentry(struct proc *proc, __ref fd_entry_t *fde);

int fs_proc_open(struct proc *proc, int fd, cstr_t path, int flags, mode_t mode);
int fs_proc_close(struct proc *proc, int fd);
int fs_open(cstr_t path, int flags, mode_t mode);
int fs_close(int fd);
struct vm_file *fs_get_vmfile(int fd, size_t off, size_t len, int mmap_flags, int prot);
__ref struct page *fs_getpage(int fd, off_t off);
__ref struct page *fs_getpage_cow(int fd, off_t off);
ssize_t fs_kread(int fd, kio_t *kio);
ssize_t fs_kwrite(int fd, kio_t *kio);
ssize_t fs_read(int fd, void *buf, size_t len);
ssize_t fs_write(int fd, const void *buf, size_t len);
ssize_t fs_readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t fs_writev(int fd, const struct iovec *iov, int iovcnt);
ssize_t fs_pread(int fd, void *buf, size_t len, off_t offset);
ssize_t fs_pwrite(int fd, const void *buf, size_t len, off_t offset);
ssize_t fs_readdir(int fd, void *dirp, size_t len);
off_t fs_lseek(int fd, off_t offset, int whence);
int fs_ioctl(int fd, unsigned int request, void *argp);
int fs_fcntl(int fd, int cmd, unsigned long arg);
int fs_ftruncate(int fd, off_t length);
int fs_fstat(int fd, struct stat *stat);
int fs_dup(int fd);
int fs_dup2(int fd, int newfd);
int fs_pipe(int pipefd[2]);
int fs_pipe2(int pipefd[2], int flags);
int fs_poll(struct pollfd *fds, size_t nfds, struct timespec *timeout);
int fs_utimensat(int dirfd, cstr_t filename, struct timespec *utimes, int flags);

int fs_stat(cstr_t path, struct stat *stat);
int fs_lstat(cstr_t path, struct stat *stat);
int fs_create(cstr_t path, mode_t mode);
int fs_truncate(cstr_t path, off_t length);
int fs_mknod(cstr_t path, mode_t mode, dev_t dev);
int fs_symlink(cstr_t target, cstr_t linkpath);
int fs_link(cstr_t oldpath, cstr_t newpath);
int fs_unlink(cstr_t path);
int fs_chdir(cstr_t path);
int fs_mkdir(cstr_t path, mode_t mode);
int fs_rmdir(cstr_t path);
int fs_rename(cstr_t oldpath, cstr_t newpath);
ssize_t fs_readlink(cstr_t path, char *buf, size_t bufsiz);
ssize_t fs_realpath(cstr_t path, kio_t *buf);

void fs_print_debug_vcache();

#endif
