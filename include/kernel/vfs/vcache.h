//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#ifndef KERNEL_VFS_VCACHE_H
#define KERNEL_VFS_VCACHE_H

#include <kernel/vfs_types.h>
#include <kernel/vfs/path.h>

typedef struct vcache vcache_t;

// vcache api
vcache_t *vcache_alloc(__ref ventry_t *root);
void vcache_free(vcache_t *vcache);
__ref ventry_t *vcache_get_root(vcache_t *vcache);
__ref ventry_t *vcache_get(vcache_t *vcache, cstr_t path);
int vcache_put(vcache_t *vcache, cstr_t path, ventry_t *ve);
int vcache_invalidate(vcache_t *vcache, cstr_t path);
int vcache_invalidate_all(vcache_t *vcache);
void vcache_dump(vcache_t *vcache);

#endif
