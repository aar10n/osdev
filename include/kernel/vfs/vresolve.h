//
// Created by Aaron Gill-Braun on 2023-06-04.
//

#ifndef KERNEL_VFS_VRESOLVE_H
#define KERNEL_VFS_VRESOLVE_H

#include <kernel/vfs_types.h>
#include <kernel/vfs/vcache.h>
#include <kernel/sbuf.h>

// flags
#define VR_NOFOLLOW 0x1   // do not follow symlinks or mount points at the end of the path (return entry itself)
#define VR_UNLOCKED 0x2   // the result reference will be returned with no ventry lock held
#define VR_PARENT   0x4   // on -ENOENT in the final directory the result is set to parent entry
#define VR_EXCLUSV  0x8   // fail if path exists, on success return the parent entry
#define VR_NOTDIR   0x80  // the path must not be a directory
#define VR_DIR      0x100 // the path must be a directory
#define VR_BLK      0x200 // the path must be a block device
#define VR_LNK      0x400 // the path must be a symlink

int vresolve_cache(vcache_t *vc, cstr_t path, int flags, int depth, __move ventry_t **result);
int vresolve_fullwalk(vcache_t *vc, ventry_t *at, cstr_t path, int flags, int depth, sbuf_t *fullpath, __move ventry_t **result);

int vresolve_fullpath(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, __inout sbuf_t *fullpath, __move ventry_t **result);
int vresolve(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, __move ventry_t **result);

#endif
