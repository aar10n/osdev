//
// Created by Aaron Gill-Braun on 2023-06-04.
//

#include <vfs/vresolve.h>
#include <vfs/vnode.h>
#include <vfs/ventry.h>
#include <vfs/vcache.h>

#include <panic.h>
#include <sbuf.h>
#include <str.h>

#define MAX_LOOP 32 // resolve depth limit

#define ASSERT(x) kassert(x)
#define goto_error(err) do { res = err; goto error; } while (0)


static size_t vresolve_get_ve_path(ventry_t *ve, sbuf_t *sb) {
  size_t res = 0;
  ventry_t *parent = ve_getref(ve->parent);
  if (parent) {
    res += vresolve_get_ve_path(parent, sb);
    res += sbuf_write_char(sb, '/');
    ve_release(&parent);
  }

  if (parent && VN_ISROOT(VN(parent))) {
    // skip writing '/' for mount root
    return res;
  }
  res += sbuf_write_str(sb, ve->name);
  return res;
}

static int vresolve_internal(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, int depth, __move ventry_t **result) {
  if (depth > MAX_LOOP) {
    return -ELOOP;
  }

  // try the cache first
  if (vresolve_cache(vcache, path, flags, depth, result) == 0) {
    return 0;
  }
  // then the full walk if needed (either because of VR_FULLWALK or because of a cache miss)
  return vresolve_fullwalk(vcache, at, path, flags, depth, result);
}

static int vresolve_validate_result(ventry_t *ve, int flags) {
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

static int vresolve_follow(vcache_t *vc, __move ventry_t **veref, int flags, bool islast, int depth, __move ventry_t **realve) {
  // ve must have lock held prior to calling this function
  ventry_t *ve = ve_moveref(veref);
  ventry_t *next_ve = NULL; // ref
  int res;

  if (depth > MAX_LOOP)
    goto_error(-ELOOP);

  if (V_ISLNK(ve)) { // handle symlinks
    if (islast && (flags & VR_NOFOLLOW)) {
      // return the symlink ventry reference
      *realve = ve_moveref(&ve);
      return 0;
    }

    vnode_t *vn = VN(ve);
    if (vn->size > PATH_MAX)
      goto_error(-ENAMETOOLONG);

    char linkbuf[PATH_MAX+1] = {0};
    // READ BEGIN
    vn_begin_data_read(vn);
    kio_t kio = kio_new_writeonly(linkbuf, vn->size);
    if ((res = vn_readlink(vn, &kio)) < 0) {
      vn_end_data_read(vn);
      goto error;
    }
    vn_end_data_read(vn);
    // READ END

    // follow the symlink (and get locked result)
    if ((res = vresolve_internal(vc, ve, cstr_new(linkbuf, vn->size), 0, depth++, &next_ve)) < 0) {
      goto error;
    }

    // unlock the symlink ventry and swap refs with the target
    ve_unlock(ve);
    ve_release_swap(&ve, &next_ve);
  } else if (VN_ISMOUNT(VN(ve))) { // handle mount points
    ASSERT(V_ISDIR(ve));
    if (islast && (flags & VR_NOFOLLOW)) {
      // return the mount ventry reference
      *realve = ve_moveref(&ve);
      return 0;
    }

    // follow the mount point
    vfs_t *vfs = VN(ve)->vfs;
    next_ve = ve_getref(vfs->root_ve);
    if (!ve_lock(next_ve))
      goto_error(-ENOENT);

    // unlock the mount ventry and swap refs with the mount root
    ve_unlock(ve);
    ve_release_swap(&ve, &next_ve);
  }

  // return locked reference
  *realve = ve_moveref(&ve);
  return 0;
LABEL(error);
  ve_unlock(ve);
  ve_release(&ve);
  ve_release(&next_ve);
  return res;
}

//
//
//

int vresolve_cache(vcache_t *vc, cstr_t path, int flags, int depth, __move ventry_t **result) {
  ventry_t *ve = NULL; // ref
  int res;

  ve = vcache_get(vc, path);
  if (ve == NULL)
    return -ENOENT;
  // lock the ventry
  if (!ve_lock(ve)) {
    vcache_invalidate(vc, path);
    ve_release(&ve);
    return -ENOENT;
  }

  if (flags & VR_EXCLUSV)
    goto_error(-EEXIST);

  // follow the symlink or mount point if needed
  if ((res = vresolve_follow(vc, &ve, flags, true, depth, &ve)) < 0)
    goto error;

  if ((res = vresolve_validate_result(ve, flags)) < 0)
    goto error;

  // success
  if (flags & VR_UNLOCKED)
    ve_unlock(ve);
  *result = ve_moveref(&ve);
  return 0;

LABEL(error);
  ve_unlock(ve);
  ve_release(&ve);
  return res;
}

int vresolve_fullwalk(vcache_t *vc, ventry_t *at, cstr_t path, int flags, int depth, __move ventry_t **result) {
  ventry_t *ve = NULL; // ref
  int res;

  // keep track of the current path as we walk
  // it so we can cache the intermediate paths
  char tmp[PATH_MAX + 1] = {0};
  sbuf_t curpath = sbuf_init(tmp, PATH_MAX + 1);

  // starting directory
  path_t part = path_from_cstr(path);
  if (path_is_absolute(part)) {
    ve = ve_getref(vcache_get_root(vc));
    sbuf_write_char(&curpath, '/');
  } else {
    ve = ve_getref(at);
    vresolve_get_ve_path(ve, &curpath); // get as absolute path
  }

  // lock starting entry
  if (!ve_lock(ve)) {
    ve_release(&ve);
    return -ENOENT;
  }

  // ======================== walk loop ========================
  // we should start and end each iteration with a locked ventry
  while (!path_is_null(part = path_next_part(part))) {
    vnode_t *vn = VN(ve); // non-ref
    if (!V_ISDIR(at))
      goto_error(-ENOTDIR);
    if (path_len(part) > NAME_MAX)
      goto_error(-ENAMETOOLONG);

    bool is_last = path_iter_end(part);
    ventry_t *next_ve = NULL; // ref

    if (path_is_dot(part)) {
      continue;
    } else if (path_is_dotdot(part)) {
      next_ve = ve_getref(ve->parent);
      goto lock_next;
    }

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
          *result = ve_moveref(&ve);
          return -ENOENT;
        }
      }
      goto error;
    }

  LABEL(lock_next);
    if (!ve_lock(next_ve)) {
      ve_release(&next_ve);
      goto_error(-ENOENT);
    }

    // unlock the current ventry and swap refs with the next
    ve_unlock(ve);
    ve_release_swap(&ve, &next_ve);

    // write the resolved path part
    sbuf_write_char(&curpath, '/');
    sbuf_write(&curpath, path_start(part), path_len(part));
    // cache the intermediate path
    vcache_put(vc, cstr_from_sbuf(&curpath), next_ve);

    // follow the symlink or mount point if needed
    if ((res = vresolve_follow(vc, &ve, flags, is_last, depth, &ve)) < 0)
      goto error;

    // continue
  }

  // ==============================================================
  // we have an entry
  if ((res = vresolve_validate_result(ve, flags)) < 0)
    goto error;

LABEL(success);
  // load the vnode if needed
  vnode_t *vn = VN(ve);
  if (!VN_ISLOADED(vn)) {
    vn_lock(vn);
    res = vn_load(vn);
    vn_unlock(vn);
    if (res < 0)
      goto error;
  }

  vcache_put(vc, path, ve);

  if (flags & VR_UNLOCKED)
    ve_unlock(ve);
  // return the ventry reference
  *result = ve_moveref(&ve);
  return 0;

LABEL(error);
  ve_unlock(ve);
  ve_release(&ve);
  return res;
}

int vresolve(vcache_t *vcache, ventry_t *at, cstr_t path, int flags, __move ventry_t **result) {
  return vresolve_internal(vcache, at, path, flags, 0, result);
}
