//
// Created by Aaron Gill-Braun on 2021-07-14.
//

#include <dcache.h>
#include <dentry.h>
#include <inode.h>

#include <mm.h>
#include <panic.h>
#include <printf.h>

#include <sbuf.h>


#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("dcache: %s: " fmt, __func__, ##__VA_ARGS__)
// #define DPRINTF(str, args...)

#define DCACHE_SIZE 4096
#define DIR_HASHES_SIZE 16

static dcache_dir_t *dcache_dir_alloc(dentry_t *dentry) {
  dcache_dir_t *dir = kmallocz(sizeof(dcache_dir_t));
  dir->count = 0;
  dir->size = DIR_HASHES_SIZE;
  dir->hashes = kmallocz(dir->size * sizeof(hash_t));
  dir->dentry = dentry;
  LIST_ENTRY_INIT(&dir->list);
  return dir;
}

static void dcache_dir_free(dcache_dir_t *dir) {
  kfree(dir->hashes);
}

static void dcache_dir_add(dcache_dir_t *dir, hash_t hash) {
  if (dir->count == dir->size) {
    dir->size += DIR_HASHES_SIZE;
    hash_t *hashes = kmallocz(dir->size * sizeof(hash_t));
    memcpy(hashes, dir->hashes, dir->count * sizeof(hash_t));
    kfree(dir->hashes);
    dir->hashes = hashes;
  }
  dir->hashes[dir->count++] = hash;
}

static void dcache_dir_remove(dcache_dir_t *dir, hash_t hash) {
  for (size_t i = 0; i < dir->count; i++) {
    if (dir->hashes[i] == hash) {
      // swap with last element and shrink
      dir->hashes[i] = dir->hashes[--dir->count];
      break;
    }
  }
}

//
// MARK: Dcache API
//

dcache_t *dcache_create(struct dentry **root) {
  ASSERT(root != NULL);

  dcache_t *dcache = kmallocz(sizeof(struct dcache));
  dcache->size = DCACHE_SIZE;
  dcache->root = root;
  mutex_init(&dcache->lock, MUTEX_REENTRANT);

  size_t dentry_table_size = dcache->size * sizeof(struct dentry *);
  size_t dir_infos_size = dcache->size * sizeof(struct dcache_dir);
  size_t count = SIZE_TO_PAGES(dentry_table_size + dir_infos_size);
  dcache->pages = valloc_zero_pages(count, PG_WRITE);
  if (dcache->pages == NULL) {
    panic("failed to allocate dcache pages");
  }

  // setup the hash tables
  dcache->buckets = (void *) PAGE_VIRT_ADDR(dcache->pages);
  dcache->dirs = (void *) offset_addr(dcache->buckets, dentry_table_size);
  return dcache;
}

void dcache_destroy(dcache_t *dcache) {
  ASSERT(dcache->count == 0);
  vfree_pages(dcache->pages);
  kfree(dcache);
}

int dcache_get(dcache_t *dcache, path_t path, dentry_t **dentry) {
  hash_t hash = d_hash_path(D_OPS(*dcache->root), path);
  uint64_t index = d_hash_index(hash, dcache->size);

  DCACHE_LOCK(dcache);
  dentry_t *d = dcache->buckets[index];
  while (d != NULL) {
    if (d_compare_path(d, path)) {
      if (d != NULL) {
        *dentry = d;
      }
      DCACHE_UNLOCK(dcache);
      return 0;
    }
    d = LIST_NEXT(d, bucket);
  }
  DCACHE_UNLOCK(dcache);
  return -1;
}

int dcache_put(dcache_t *dcache, dentry_t *dentry) {
  char tmp[PATH_MAX+1] = {0};
  sbuf_t buf = sbuf_init(tmp, PATH_MAX+1);
  int res;

  // get path for this dentry
  if ((res = get_dentry_path(*dcache->root, dentry, &buf, NULL)) < 0) {
    return res;
  }
  return dcache_put_path(dcache, strn2path(tmp, res), dentry);
}

int dcache_put_path(dcache_t *dcache, path_t path, dentry_t *dentry) {
  hash_t dhash = d_hash_path(D_OPS(*dcache->root), path);
  uint64_t index = d_hash_index(dhash, dcache->size);
  DCACHE_LOCK(dcache);
  {
    // (1) add the path to the hash table
    D_LOCK(dentry);
    {
      dentry->dhash = dhash;
      if (dcache->buckets[index] == NULL) {
        dcache->buckets[index] = dentry;
        dcache->count++;
      } else {
        // check if the dentry already exists
        dentry_t *last;
        RLIST_FOR_IN(d, dcache->buckets[index], bucket) {
          last = d;
          if (d_hash_equal(dhash, d->dhash)) {
            D_UNLOCK(dentry);
            DCACHE_UNLOCK(dcache);
            return -1; // dentry already exists
          }
        }

        // add the dentry to the bucket
        RLIST_ADD(last, dentry, bucket);
        dcache->count++;
      }
    }
    D_UNLOCK(dentry);

    // (2) add this dhash to the parent directory info
    uint64_t parent_index = d_hash_index(dentry->parent->dhash, dcache->size);
    dcache_dir_t *parent_dir = RLIST_FIND(d, dcache->dirs[parent_index], list, d->dentry == dentry->parent);
    if (parent_dir == NULL) {
      // this shouldnt really be NULL since path walking adds entries from the root down
      // but just in case...
      DPRINTF("parent_dir is NULL\n");
    } else {
      dcache_dir_add(parent_dir, dhash);
    }

    // (3) create a directory info [if needed]
    if (IS_IFDIR(dentry)) {
      dcache_dir_t *dir = dcache_dir_alloc(dentry);
      RLIST_ADD_FRONT(&dcache->dirs[index], dir, list);
    }
  }
  DCACHE_UNLOCK(dcache);
  return 0;
}

int dcache_remove(dcache_t *dcache, dentry_t *dentry) {
  DCACHE_LOCK(dcache);
  {
    // (1) remove the dentry from the hash table
    uint64_t index = d_hash_index(dentry->dhash, dcache->size);
    RLIST_REMOVE(&dcache->buckets[index], dentry, bucket);

    // (2) remove this dhash from the parent directory info
    uint64_t parent_index = d_hash_index(dentry->parent->dhash, dcache->size);
    dcache_dir_t *parent_dir = RLIST_FIND(d, dcache->dirs[parent_index], list, d->dentry == dentry->parent);
    if (dentry != dentry->parent) {
      if (parent_dir != NULL) {
        dcache_dir_remove(parent_dir, dentry->dhash);
      } else {
        // this shouldnt really be NULL since path walking adds entries from the root down
        // but just in case...
        DPRINTF("parent_dir is NULL\n");
      }
    }

    if (!IS_IFDIR(dentry)) {
      DCACHE_UNLOCK(dcache);
      return 0;
    }

    /////////////// directories only after here ///////////////

    // (3) remove the associated dcache_dir
    dcache_dir_t *dir = RLIST_FIND(d, dcache->dirs[index], list, d->dentry == dentry);
    if (dir == NULL) {
      DPRINTF("directory has no associated dcache_dir");
      DCACHE_UNLOCK(dcache);
      return -1;
    }
    RLIST_REMOVE(&dcache->dirs[index], dir, list);

    // (4) recursively remove subdirectories by their hashes
    for (size_t i = 0; i < dir->count; i++) {
      hash_t child_hash = dir->hashes[i];
      uint64_t child_index = d_hash_index(child_hash, dcache->size);
      dentry_t *child = RLIST_FIND(d, dcache->buckets[child_index], bucket, d->dhash == child_hash);
      ASSERT(child != NULL);
      if (dcache_remove(dcache, child) < 0) {
        DPRINTF("failed to remove child from dcache");
        // keep going with the other ones
      }
    }

    dcache_dir_free(dir);
  }
  DCACHE_UNLOCK(dcache);

  return -1;
}

//
// MARK: Path Operations
//

int resolve_path(dentry_t *root, dentry_t *at, path_t path, int flags, dentry_t **result) {
  int res;
  dcache_t *dcache = NULL;
  if (at->inode && at->inode->sb && at->inode->sb->dcache) {
    dcache = at->inode->sb->dcache;
  }

  // first try to get the dentry from the dcache
  if (dcache && (res = dcache_get(dcache, path, result)) == 0) {
    return 0;
  }

  // full walk
  dentry_t *dentry;
  dentry_t *last;
  path_t part = path;
  int depth = 0;

  // keep track of the current path as we walk it
  // so we can cache the intermediate dentries
  char tmp[PATH_MAX+1] = {0};
  sbuf_t curpath = sbuf_init(tmp, PATH_MAX+1);

  // get the starting dentry
  if (path_is_slash(part)) {
    dentry = root;
    part = path_next_part(part);
    sbuf_write_char(&curpath, '/');
  } else if (path_is_dot(part)) {
    dentry = at;
    part = path_next_part(part);
  } else if (path_is_dotdot(part)) {
    dentry = at->parent;
    part = path_next_part(part);
  } else {
    dentry = at;
  }

  // walk the path
  while (!path_is_null(part = path_next_part(part))) {
    last = dentry;
    if (path_len(part) > NAME_MAX) {
      return -ENAMETOOLONG;
    }

    // write each part into the buffer as we iterate
    char name[NAME_MAX+1] = {0};
    size_t len = path_copy(name, NAME_MAX+1, part);
    if (sbuf_write(&curpath, name, len) != len) {
      return -ENOBUFS;
    }

    if (IS_IFDIR(dentry)) {
      // load children if we haven't already
      if (!IS_IFLLDIR(dentry->inode)) {
        if ((res = i_loaddir(dentry->inode, dentry)) < 0) {
          return res;
        }
      }

      dentry = d_get_child(dentry, name, len);
      if (dentry == NULL)
        break;

      // put the intermediate paths into the dcache
      if (dcache) {
        if (dcache_get(dcache, sbuf_to_path(&curpath), NULL) < 0) {
          if (dcache_put_path(dcache, sbuf_to_path(&curpath), dentry) < 0) {
            DPRINTF("failed to add dentry to dcache: {:.*s}\n", tmp, sbuf_len(&curpath));
          }
        }
      }
      continue;
    } else if (IS_IFLNK(dentry)) {
      if (flags & O_NOFOLLOW) {
        return -ELOOP;
      }

      // follow the symlink
      inode_t *inode = dentry->inode;
      char *linkpath = inode->i_link;
      if (linkpath == NULL) {
        // read the link target
        linkpath = kmalloc(inode->size + 1);
        if ((res = I_OPS(inode)->i_readlink(inode, inode->size + 1, linkpath)) < 0) {
          DPRINTF("failed to read symlink: {:.*s} [ino={:d}]\n", tmp, sbuf_len(&curpath), inode->ino);
          kfree(linkpath);
          return res;
        }

        // cache the target path
        inode->i_link = linkpath;
      }

      // resolve the link target
      dentry_t *link = NULL;
      if ((res = resolve_path(root, dentry, strn2path(linkpath, inode->size), flags, &link)) < 0) {
        DPRINTF("failed to resolve symlink: {:.*s} -> {:.*s}\n", tmp, sbuf_len(&curpath), linkpath, inode->size);
        kfree(linkpath);
        return res;
      }

      // cache the link target
      if (dcache) {
        if (dcache_put_path(dcache, sbuf_to_path(&curpath), link) < 0) {
          DPRINTF("failed to add dentry to dcache: {:.*s}\n", tmp, sbuf_len(&curpath));
        }
      }

      // and follow it
      dentry = link;
      continue;
    } else {
      // we can't walk a non-directory
      return -ENOTDIR;
    }

    if (path_end(part) != path_end(path)) {
      // there was more to the path but we're not in a directory
      return -ENOTDIR;
    }
    break;
  }

  if (dentry == NULL) {
    return -ENOENT;
  }

  if (dcache && dcache_put_path(dcache, sbuf_to_path(&curpath), dentry) < 0) {
    DPRINTF("failed to add dentry to dcache: {:.*s}\n", tmp, sbuf_len(&curpath));
  }
  *result = dentry;
  return 0;
}

int get_dentry_path(const dentry_t *root, const dentry_t *dentry, sbuf_t *buf, int *depth) {
  if (sbuf_rem(buf) == 0) {
    return -ENOBUFS;
  } else if (dentry == root) {
    if (sbuf_write_char(buf, '/') == 0) {
      return -ENOBUFS;
    }
    return 1;
  }

  int d = 0;
  while (dentry != root) {
    if ((sbuf_write_reverse(buf, dentry->name, dentry->namelen) == 0) || (sbuf_write_char(buf, '/') == 0)) {
      return -ENOBUFS;
    }

    dentry = dentry->parent;
    d++;
  }

  sbuf_reverse(buf);
  if (depth != NULL) {
    *depth = d;
  }
  return (int)sbuf_len(buf);
}

int expand_path(const dentry_t *root, const dentry_t *at, path_t path, sbuf_t *buf) {
  if (sbuf_rem(buf) == 0) {
    return -ENOBUFS;
  }

  const dentry_t *dentry;
  path_t part = path;
  int depth = 0;

  // get the starting dentry
  if (path_is_absolute(path)) {
    dentry = root;
  } else {
    dentry = at;
  }

  // write the absolute prefix of the path to the buffer
  int result = get_dentry_path(root, dentry, buf, &depth);
  if (result < 0) {
    return result;
  }

  // iterate over the path parts
  while (!path_is_null(part = path_next_part(part))) {
    if (path_is_dot(part)) {
      continue; // '.' is a no-op
    } else if (path_is_dotdot(part)) {
      if (depth == 0) {
        continue; // ignore '..' at the root
      }

      // we can 'step out' of the current directory by
      // erasing the last part from the path
      while (sbuf_peek(buf) != '/')
        sbuf_pop(buf);

      depth--;
    } else {
      // we can 'step in' to the current directory by
      // appending the next part to the path
      if (sbuf_write_char(buf, '/') == 0) {
        return -ENOBUFS;
      }
      if (sbuf_write(buf, path_start(part), path_len(part)) == 0) {
        return -ENOBUFS;
      }
      depth++;
    }
  }

  return (int)sbuf_len(buf);
}
