//
// Created by Aaron Gill-Braun on 2023-06-04.
//

#include <kernel/vfs/vresolve.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vcache.h>

#include <kernel/panic.h>
#include <kernel/sbuf.h>
#include <kernel/str.h>
#include <kernel/printf.h>

#define MAX_LOOP 32 // resolve depth limit

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("vresolve: %s: " fmt, __func__, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)

#define goto_res(err) do { res = err; goto error; } while (0)


static size_t vresolve_get_ve_path(ventry_t *ve, sbuf_t *sb) {
  size_t res = 0;
  ventry_t *parent = ve->parent;
  if (!VE_ISFSROOT(ve) && parent && (ve != parent)) {
    res += vresolve_get_ve_path(parent, sb);
    res += sbuf_write_char(sb, '/');
  }

  if (ve != parent) {
    res += sbuf_write_str(sb, ve->name);
  }
  return res;
}

static int vresolve_internal(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, int depth, __out sbuf_t *fullpath, __move ventry_t **result) {
  if (depth > MAX_LOOP) {
    return -ELOOP;
  }

  // try the cache first
  if (vresolve_cache(vcache, path, flags, depth, result) == 0) {
    DPRINTF("cache hit: {:cstr} -> {:ve}\n", &path, *result);

    if (fullpath != NULL) // path is already a full path
      sbuf_write_cstr(fullpath, path);
    return 0;
  }
  // otherwise walk the full path (either due to VR_FULLWALK or a cache miss)
  return vresolve_fullwalk(vcache, at, path, flags, depth, fullpath, result);
}

static int vresolve_validate_result(ventry_t *ve, int flags) {
  if ((flags & VR_NOTDIR) && V_ISDIR(ve)) {
    return -EISDIR;
  }
  if ((flags & VR_DIR) && !V_ISDIR(ve)) {
    return -ENOTDIR;
  }
  if ((flags & VR_BLK) && !V_ISBLK(ve)) {
    return -ENOTBLK;
  }
  if ((flags & VR_LNK) && !V_ISLNK(ve)) {
    return -ENOLINK;
  }
  return 0;
}

static int vresolve_follow(vcache_t *vc, __move ventry_t **veref, int flags, bool islast, int depth, sbuf_t *fullpath, __move ventry_t **realve) {
  // ve must have lock held prior to calling this function
  ventry_t *ve = moveref(*veref);
  ventry_t *at_ve = ve_getref(ve->parent);
  ventry_t *next_ve = NULL; // ref
  int res;

  if (depth > MAX_LOOP)
    goto_res(-ELOOP);

  if (V_ISLNK(ve)) { // handle symlinks
    if (islast && (flags & VR_NOFOLLOW)) {
      // return the symlink ventry reference
      goto ret;
    }

    vnode_t *vn = VN(ve);
    if (vn->size > PATH_MAX)
      goto_res(-ENAMETOOLONG);

    char linkbuf[PATH_MAX+1] = {0};
    // READ BEGIN
    vn_begin_data_read(vn);
    kio_t kio = kio_new_writable(linkbuf, vn->size);
    if ((res = vn_readlink(vn, &kio)) < 0) {
      vn_end_data_read(vn);
      goto error;
    }
    vn_end_data_read(vn);
    // READ END

    // follow the symlink (and get locked result)
    if ((res = vresolve_internal(vc, at_ve, cstr_new(linkbuf, vn->size), 0, depth++, fullpath, &next_ve)) < 0) {
      DPRINTF("failed to follow symlink: %s\n", linkbuf, res);
      goto error;
    }

    // unlock the symlink ventry and swap refs with the target
    ve_unlock(ve);
    ve_putref_swap(&ve, &next_ve);
  } else if (VE_ISMOUNT(ve)) { // handle mount points
    ASSERT(V_ISDIR(ve));

    if (islast && (flags & VR_NOFOLLOW)) {
      // return the mount ventry reference
      goto ret;
    }
    // follow the mount point
    next_ve = ve_getref(ve->mount);
    if (!ve_lock(next_ve))
      goto_res(-ENOENT);

    // unlock the mount ventry and swap refs with the mount root
    ve_unlock(ve);
    ve_putref_swap(&ve, &next_ve);
  }

LABEL(ret);
  // return locked reference
  ve_putref(&at_ve);
  *realve = moveref(ve);
  return 0;
LABEL(error);
  ve_unlock(ve);
  ve_putref(&ve);
  ve_putref(&at_ve);
  ve_putref(&next_ve);
  return res;
}

//
//
//

int vresolve_cache(vcache_t *vc, cstr_t path, int flags, int depth, __move ventry_t **result) {
  ventry_t *ve = NULL; // ref
  int res;
  if (!cstr_starts_with(path, '/')) {
    return -EINVAL;
  }

  ve = vcache_get(vc, path);
  if (ve == NULL)
    return -ENOENT;
  // lock the ventry
  if (!ve_lock(ve)) {
    vcache_invalidate(vc, path);
    ve_putref(&ve);
    return -ENOENT;
  }

  if (flags & VR_EXCLUSV)
    goto_res(-EEXIST);

  // follow the symlink or mount point if needed
  if ((res = vresolve_follow(vc, &ve, flags, true, depth, NULL, &ve)) < 0)
    goto error;

  if ((res = vresolve_validate_result(ve, flags)) < 0)
    goto error;

  // success
  if (flags & VR_UNLOCKED)
    ve_unlock(ve);
  *result = moveref(ve);
  return 0;

LABEL(error);
  ve_unlock(ve);
  ve_putref(&ve);
  return res;
}

int vresolve_fullwalk(vcache_t *vc, ventry_t *at, cstr_t path, int flags, int depth, sbuf_t *fullpath, __move ventry_t **result) {
  ventry_t *ve = NULL; // ref
  int res;

  // keep track of the current path as we walk
  // it so we can cache the intermediate paths
  char tmp[PATH_MAX + 1] = {0};
  sbuf_t curpath = sbuf_init(tmp, PATH_MAX + 1);

  // get starting directory
  path_t part = path_from_cstr(path);
  if (path_is_absolute(part)) {
    ve = vcache_get_root(vc);
  } else {
    vresolve_get_ve_path(at, &curpath);
    ve = ve_getref(at);
  }

  // if we are at a mount point, walk from the mount root
  if (VE_ISMOUNT(ve)) {
    ventry_t *root = ve_getref(ve->mount);
    ve_putref_swap(&ve, &root);
  }

  // lock starting entry
  if (!ve_lock(ve)) {
    ve_putref(&ve);
    return -ENOENT;
  }

  // ======================== walk loop ========================
  // we should start and end each iteration with a locked ventry
  while (!path_is_null(part = path_next_part(part))) {
    vnode_t *vn = VN(ve); // non-ref
    if (!V_ISDIR(ve))
      goto_res(-ENOTDIR);
    if (path_len(part) > NAME_MAX)
      goto_res(-ENAMETOOLONG);

    bool is_last = path_iter_end(part);
    ventry_t *next_ve = NULL; // ref

    if (path_is_dot(part)) {
      continue;
    } else if (path_is_dotdot(part)) {
      next_ve = ve_getref(ve->parent);
      goto lock_next;
    }
    // ld-musl-x86_64.so.1
    // /lib/ld-musl-x86_64.so.1

    vn_begin_data_read(vn);
    res = vn_lookup(ve, vn, cstr_from_path(part), &next_ve);
    vn_end_data_read(vn);
    if (res < 0) {
      if (is_last && res == -ENOENT) {
        if (flags & VR_EXCLUSV) {
          // exclusive create, last entry is missing so return parent with success
          goto success;
        }
        if (flags & VR_PARENT) { // last entry is missing so return parent with error
          if (flags & VR_UNLOCKED)
            ve_unlock(ve);
          // return the parent ventry reference
          *result = moveref(ve);
          return -ENOENT;
        }
      }
      goto error;
    }

  LABEL(lock_next);
    if (!ve_lock(next_ve)) {
      ve_putref(&next_ve);
      goto_res(-ENOENT);
    }

    // unlock the current ventry and swap refs with the next
    ve_unlock(ve);
    ve_putref_swap(&ve, &next_ve);

    // write the resolved path part
    sbuf_write_char(&curpath, '/');
    sbuf_write(&curpath, path_start(part), path_len(part));
    // cache the current path
    vcache_put(vc, cstr_from_sbuf(&curpath), ve);

    // follow the symlink or mount point if needed
    if ((res = vresolve_follow(vc, &ve, flags, is_last, depth, fullpath, &ve)) < 0)
      goto error;

    // continue
  }

  // ==============================================================

  // we have an entry
  if ((res = vresolve_validate_result(ve, flags)) < 0)
    goto error;

LABEL(success);
  if (VN_ISROOT(VN(ve)) && (flags & VR_NOFOLLOW)) {
    // if the NOFOLLOW flag is set make sure we're returning the containing
    // mount ventry instead of the root ventry
    if (!VE_ISFSROOT(ve)) {
      ventry_t *parent = ve_getref(ve->parent);
      ve_lock(parent);
      ve_unlock(ve);
      ve_putref_swap(&ve, &parent);
    }
  }

  // load the vnode if needed
  vnode_t *vn = VN(ve);
  if (!VN_ISLOADED(vn)) {
    vn_lock(vn);
    res = vn_load(vn);
    vn_unlock(vn);
    if (res < 0)
      goto error;
  }

  if (flags & VR_UNLOCKED)
    ve_unlock(ve);

  if (fullpath != NULL) sbuf_transfer(&curpath, fullpath); // return the fullpath
  *result = moveref(ve); // return the ventry reference
  return 0;

LABEL(error);
  if (ve)
    ve_unlock(ve);
  ve_putref(&ve);
  return res;
}

int vresolve_fullpath(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, __inout sbuf_t *fullpath, __move ventry_t **result) {
  return vresolve_internal(vcache, at, path, flags, 0, fullpath, result);
}

int vresolve(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, __move ventry_t **result) {
  return vresolve_internal(vcache, at, path, flags, 0, NULL, result);
}
