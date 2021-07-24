//
// Created by Aaron Gill-Braun on 2021-07-14.
//

#include <dcache.h>
#include <super.h>
#include <inode.h>
#include <process.h>
#include <thread.h>
#include <path.h>
#include <murmur3.h>
#include <queue.h>
#include <printf.h>
#include <panic.h>

#define DCACHE_SIZE 512

dentry_cache_t *dcache = NULL;


uint32_t hash(const char *str) {
  size_t len = strlen(str);
  uint32_t out;
  murmur_hash_x86_32(str, len, 0xDEADBEEF, &out);
  return out;
}

//

/** Initializes the dentry cache. */
void dcache_init() {
  dcache = kmalloc(sizeof(dentry_cache_t));
  dcache->pages = alloc_zero_pages(SIZE_TO_PAGES(DCACHE_SIZE * sizeof(dentry_t *)), PE_WRITE);
  dcache->buckets = (void *) dcache->pages->addr;
  dcache->nbuckets = DCACHE_SIZE;
  dcache->count = 0;
  spin_init(&dcache->lock);
}

//

/** Retrieves an entry from the dcache. */
dentry_t *dcache_get(const char *name, uint32_t base_hash) {
  uint32_t idx = hash(name) % DCACHE_SIZE;
  spin_lock(&dcache->lock);
  dentry_t *dentry = dcache->buckets[idx];
  while (dentry && dentry->hash != base_hash) {
    dentry = LIST_NEXT(dentry, bucket);
  }
  spin_unlock(&dcache->lock);
  return dentry;
}

/** Adds an entry to the dcache. */
void dcache_add(const char *name, dentry_t *dentry) {
  path_t path = str_to_path(name);
  uint32_t base_hash = path_to_hash(path_basename(path));
  dentry_t *existing = dcache_get(name, base_hash);
  if (existing != NULL) {
    dcache_remove(name, existing);
  }

  uint32_t idx = path_to_hash(path) % DCACHE_SIZE;
  spin_lock(&dcache->lock);
  RLIST_ADD_FRONT(&dcache->buckets[idx], dentry, bucket);
  dcache->count++;
  spin_unlock(&dcache->lock);
}

/** Removes an entry from the dcache. */
dentry_t *dcache_remove(const char *name, dentry_t *dentry) {
  uint32_t idx = hash(name) % DCACHE_SIZE;
  spin_lock(&dcache->lock);
  RLIST_REMOVE(&dcache->buckets[idx], dentry, bucket);
  dcache->count--;
  spin_unlock(&dcache->lock);
}

/**
 * Writes the complete path to a dentry into a buffer.
 * @param dentry The dentry.
 * @param buf A char buffer.
 * @param len The length of the buffer.
 * @param depth An int pointer which is updated with the depth of the dentry.
 * @return The number of characters in the path, or -1 should it fail.
 *         If the result is -1, errno shall be set to indicate the error.
 */
int get_dentry_path(dentry_t *dentry, char *buf, size_t len, int *depth) {
  if (len <= 1) {
    ERRNO = ENOBUFS;
    return -1;
  } else if (dentry == fs_root) {
    buf[0] = '/';
    buf[1] = '\0';
    return 1;
  }

  char temp[MAX_PATH + 1];
  int index = MAX_PATH;
  int d = 0;

  temp[index--] = '\0';
  while (dentry->parent != dentry) {
    size_t l = strlen(dentry->name);
    if (index - l <= 0) {
      ERRNO = ENAMETOOLONG;
      return -1;
    }

    index -= l;
    memcpy(temp + index, dentry->name, l);
    index--;
    temp[index--] = '/';
    d++;
    dentry = dentry->parent;
  }

  int plen = MAX_PATH - index;
  if (plen + 1 >= len) {
    ERRNO = ENOBUFS;
    return -1;
  }

  strcpy(buf, temp + index);
  if (depth != NULL) {
    *depth = d;
  }
  return plen;
}

/**
 * Expands a path into an absolute path and writes it to a buffer.
 * @param path The path to expand.
 * @param at The dentry from which to resolve relative references.
 * @param buf A char buffer.
 * @param len The length of the buffer.
 * @return The number of characters in the expanded path, or -1 should it fail.
 *         If the result is -1, errno shall be set to indicate the error.
 */
int expand_path(const char *path, dentry_t *at, char *buf, size_t len) {
  if (len == 0) {
    ERRNO = ENOBUFS;
    return -1;
  }

  int index = 0;
  size_t max_len = MAX_PATH + 1;
  char new_path[max_len];

  dentry_t *dentry;
  path_t part = str_to_path(path);
  path_t prefix = path_prefix(part);
  if (p_is_slash(prefix)) {
    dentry = fs_root;
    // skip leading '/'
    part = path_next_part(part);
  } else {
    dentry = at;
  }

  // copy prefix
  int depth;
  int result = get_dentry_path(dentry, new_path, max_len, &depth);
  if (result < 0) {
    ERRNO = ENAMETOOLONG;
    return -1;
  }

  index += result;
  while (!p_is_null(part = path_next_part(part))) {
    if (pathcmp_s(part, ".") == 0) {
      // ignore '.'
      continue;
    } else if (pathcmp_s(part, "..") == 0) {
      if (depth == 0) {
        // ignore '..' in root
        continue;
      }

      // we can 'step out' of the current directory by
      // erasing the last part from the path
      depth--;
      while (new_path[index - 1] != '/') {
        index--;
      }
    } else {
      if (index > 1) {
        new_path[index++] = '/';
      }

      if (index + p_len(part) > max_len) {
        ERRNO = ENAMETOOLONG;
        return -1;
      }
      pathcpy(new_path + index, part);
      index += p_len(part);
      depth++;
    }
  }

  new_path[index] = '\0';
  if (index >= len) {
    ERRNO = ENOBUFS;
    return -1;
  }
  memcpy(buf, new_path, index + 1);
  return index;
}

/**
 * Determines whether the dentry is valid for the given path.
 * Since the dcache can slip out of date, this is nessecary to
 * ensure a cached entry really is valid. This is done by getting
 * the current path to the dentry, and then comparing the up-to-date
 * hash with the one provided.
 *
 * @param dentry The cached dentry.
 * @param path The path associated with the dentry.
 * @return True if the dentry is valid for the path, otherwise false.
 */
bool is_cache_entry_valid(dentry_t *dentry, const char *path) {
  char buf[MAX_PATH + 1];
  int result = get_dentry_path(dentry, buf, MAX_PATH + 1, NULL);
  if (result < 0) {
    return false;
  }

  uint32_t expected_hash = hash(path);
  uint32_t real_hash = hash(buf);
  return expected_hash == real_hash;
}

/**
 * Searches for a child entry with the given name in the specified parent directory.
 * @param parent The parent directory.
 * @param name The name to look for.
 * @return The dentry if it exists, otherwise null.
 */
dentry_t *locate_child(dentry_t *parent, const char *name) {
  int result = sb_read_inode(parent);
  if (result < 0) {
    return NULL;
  }

  dentry_t *child = i_lookup(parent->inode, name);
  if (child == NULL) {
    return NULL;
  }

  result = sb_read_inode(child);
  if (result < 0) {
    return NULL;
  }
  return child;
}

dentry_t *follow_symlink(dentry_t *dentry, int flags, dentry_t **parent) {
  kassert(IS_IFLNK(dentry->mode));
  if (flags & O_NOFOLLOW) {
    ERRNO = ELOOP;
    return NULL;
  }

  int lcount = 1;
  char link[MAX_PATH + 1];
  while (IS_IFLNK(dentry->mode)) {
    if (lcount > MAX_SYMLINKS) {
      ERRNO = ELOOP;
      return NULL;
    }

    int result = sb_read_inode(dentry);
    if (result < 0) {
      return NULL;
    }

    result = dentry->inode->ops->readlink(dentry, link, MAX_PATH + 1);
    if (result < 0) {
      return NULL;
    }

    dentry_t *linked = resolve_path(link, fs_root, flags, parent);
    if (linked == NULL) {
      return NULL;
    }

    dentry = linked;
    lcount++;
  }
  return dentry;
}

//

// attempts to resolves a path to a dentry.
// this function accepts any form of path along with a dentry at which to base
// relative path lookups. it first attempts to locate the dentry using the cache
// but will peform a full path walk in the event that it does not exist. the parent
// parameter allows the caller to access the last directory dentry in the
// event that only the last component does not exist.
dentry_t *resolve_path(const char *path, dentry_t *at, int flags, dentry_t **parent) {
  if (flags & O_NOFOLLOW) {
    // in order to fail on a symlink we have to perform the full walk
    goto full_walk;
  }

  dentry_t *dentry = perform_fast_lookup(path, at);
  if (dentry != NULL) {
    if (parent != NULL) {
      *parent = dentry->parent;
    }
    return dentry;
  }
 full_walk:
  return perform_full_walk(path, at, flags, parent);
}

dentry_t *perform_fast_lookup(const char *path, dentry_t *at) {
  if (dcache->count == 0) {
    return NULL;
  } else if (strcmp(path, "/") == 0) {
    return fs_root;
  }

  path_t p = str_to_path(path);
  uint32_t base_hash = path_to_hash(path_basename(p));

  // naively check the path given to us
  dentry_t *dentry = dcache_get(path, base_hash);
  if (dentry && is_cache_entry_valid(dentry, path)) {
    return dentry;
  }

  // now expand the path fully and try again
  char full_path[MAX_PATH + 1];
  int result = expand_path(path, at, full_path, MAX_PATH + 1);
  if (result < 0) {
    return NULL;
  }

  dentry = dcache_get(full_path, base_hash);
  if (dentry == NULL) {
    return NULL;
  }

  if (is_cache_entry_valid(dentry, path)) {
    return dentry;
  }
  dcache_remove(full_path, dentry);
  return NULL;
}

dentry_t *perform_full_walk(const char *path, dentry_t *at, int flags, dentry_t **parent) {
  char real_path[MAX_PATH + 1];
  char curr_path[MAX_PATH + 1];
  int result = expand_path(path, at, real_path, MAX_PATH + 1);
  if (result < 0) {
    return NULL;
  }

  int index = 0;
  dentry_t *last = NULL;
  dentry_t *dentry = fs_root;
  path_t part = str_to_path(real_path);
  part = path_next_part(part);
  while (!p_is_null(part = path_next_part(part))) {
    last = dentry;
    curr_path[index++] = '/';

    if (IS_IFDIR(dentry->mode)) {
      // find the next part in the directory
      char name[MAX_FILE_NAME + 1];
      pathcpy(name, part);
      dentry = locate_child(dentry, name);
      if (dentry == NULL) {
        break;
      }

      pathcpy(curr_path + index, part);
      index += p_len(part);
      curr_path[index] = '\0';
      dcache_add(curr_path, dentry);
      continue;
    } else if (IS_IFLNK(dentry->mode)) {
      // resolve symbolic link
      dentry = follow_symlink(dentry, flags, parent);
      if (dentry == NULL) {
        return NULL;
      }
      continue;
    }

    if (!p_is_null(path_next_part(part))) {
      // there was more to the path but we're not in a directory
      ERRNO = ENOTDIR;
      return NULL;
    }
    break;
  }

  if (dentry == NULL) {
    part = path_next_part(part);
    if (parent != NULL) {
      *parent = p_is_null(part) ? last : NULL;
    }
    ERRNO = ENOENT;
    return NULL;
  }

  dcache_add(real_path, dentry);
  return dentry;
}

