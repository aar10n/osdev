//
// Created by Aaron Gill-Braun on 2023-03-21.
//

#include <dentry.h>
#include <mm.h>
#include <panic.h>
#include <murmur3.h>

#define ASSERT(x) kassert(x)

#define MURMUR3_SEED 0xDEADBEEF

static const struct dentry_ops empty_ops;

bool d_hash_equal(hash_t a, hash_t b) {
  return a == b;
}

uint64_t d_hash_index(hash_t hash, size_t size) {
  return hash % size;
}

hash_t d_default_hash_name(const char *name, size_t len) {
  if (len > INT32_MAX)
    len = INT32_MAX;

  uint64_t tmp[2] = {0, 0};
  murmur_hash_x86_128(name, (int)len, MURMUR3_SEED, tmp);
  return tmp[0] ^ tmp[1];
}

//
// MARK: Virtual API
//

dentry_t *d_alloc_empty() {
  dentry_t *dentry = kmallocz(sizeof(dentry_t));
  dentry->ops = &empty_ops;
  mutex_init(&dentry->lock, MUTEX_REENTRANT);
  return dentry;
}

dentry_t *d_alloc(const char *name, size_t namelen, mode_t mode, const struct dentry_ops *ops) {
  if (!ops)
    ops = &empty_ops;

  dentry_t *dentry = d_alloc_empty();
  dentry->name = strdup(name);
  dentry->namelen = namelen;
  dentry->mode = mode;
  dentry->hash = d_hash_str(ops, name, namelen);
  dentry->ops = ops;
  return dentry;
}

void d_free(dentry_t *dentry) {
  ASSERT(dentry->parent == NULL);
  kfree(dentry->name);
  memset(dentry, 0, sizeof(dentry_t));
  kfree(dentry);
}

int d_add_child(dentry_t *parent, dentry_t *child) {
  ASSERT(IS_IFDIR(parent));
  ASSERT(child->parent == NULL);
  D_LOCK(parent);
  D_LOCK(child);
  {
    child->parent = parent;
    LIST_ADD(&parent->children, child, list);
  }
  D_UNLOCK(child);
  D_UNLOCK(parent);
  return 0;
}

int d_remove_child(dentry_t *parent, dentry_t *child) {
  ASSERT(IS_IFDIR(parent));
  ASSERT(child->parent == parent);
  D_LOCK(parent);
  D_LOCK(child);
  {
    child->parent = NULL;
    LIST_REMOVE(&parent->children, child, list);
  }
  D_UNLOCK(child);
  D_UNLOCK(parent);
  return 0;
}

dentry_t *d_get_child(dentry_t *parent, const char *name, size_t len) {
  ASSERT(IS_IFDIR(parent));
  dentry_t *child = NULL;
  D_LOCK(parent);
  {
    LIST_FOR_IN(d, &parent->children, list) {
      if (d_compare(d, name, len)) {
        child = d;
        break;
      }
    }
  }
  D_UNLOCK(parent);
  return child;
}

//
// MARK: Operations
//

hash_t d_hash_str(const struct dentry_ops *ops, const char *name, size_t len) {
  hash_t hash = {0};
  if (ops->d_hash != NULL) {
    ops->d_hash(name, len, &hash);
    return 0;
  }
  return d_default_hash_name(name, len);
}

hash_t d_hash_path(const struct dentry_ops *ops, path_t path) {
  return d_hash_str(ops, path_start(path), path_len(path));
}

bool d_compare(const struct dentry *d, const char *name, size_t len) {
  if (D_OPS(d)->d_compare != NULL) {
    return D_OPS(d)->d_compare(d, name, len);
  } else if (D_OPS(d)->d_hash != NULL) {
    hash_t hash;
    D_OPS(d)->d_hash(name, len, &hash);
    return hash != d->hash;
  }

  if (d->namelen != len) {
    return 1;
  }
  return strncmp(d->name, name, len) == 0;
}

bool d_compare_path(const struct dentry *d, path_t path) {
  return d_compare(d, path_start(path), path_len(path));
}
