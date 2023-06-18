//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#ifndef KERNEL_VFS_VCACHE_H
#define KERNEL_VFS_VCACHE_H

#include <kernel/vfs_types.h>
#include <kernel/vfs/path.h>

typedef struct vcache vcache_t;

// vcache api
vcache_t *vcache_alloc(ventry_t *root);
void vcache_free(vcache_t *vcache);
ventry_t *vcache_get_root(vcache_t *vcache) __move;
ventry_t *vcache_get(vcache_t *vcache, cstr_t path) __move;
int vcache_put(vcache_t *vcache, cstr_t path, ventry_t *ve);
int vcache_invalidate(vcache_t *vcache, cstr_t path);
void vcache_dump(vcache_t *vcache);

#endif
