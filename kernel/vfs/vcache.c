//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#include <kernel/vfs/vcache.h>
#include <kernel/vfs/ventry.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/sbuf.h>
#include <kernel/str.h>
#include <kernel/kio.h>

#include <rb_tree.h>

typedef struct vcache {
  struct ventry *root; // root reference
  spinlock_t lock;
  size_t size;
  size_t capacity;
  struct rb_tree *dir_map;
  LIST_HEAD(struct vcache_entry) *entries;
} vcache_t;

struct vcache_entry {
  str_t path;   // path string
  hash_t hash;  // hash of the path
  ventry_t *ve; // ventry reference
  LIST_ENTRY(struct vcache_entry) list;
};

struct vcache_dir {
  size_t count;     // number of children
  size_t capacity;  // capacity of children array
  hash_t *children; // array of child (full path) hashes
};

#define VCACHE_LOCK(vcache) SPIN_LOCK(&(vcache)->lock)
#define VCACHE_UNLOCK(vcache) SPIN_UNLOCK(&(vcache)->lock)

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("vcache: %s: " fmt, __func__, ##__VA_ARGS__)
// #define DPRINTF(str, args...)

#define VCACHE_INITIAL_SIZE 1024

static const char *vtype_to_str[] = {
  [V_NONE] = "none",
  [V_REG] = "file",
  [V_DIR] = "dir",
  [V_LNK] = "lnk",
  [V_CHR] = "chr",
  [V_BLK] = "blk",
  [V_FIFO] = "fifo",
  [V_SOCK] = "sock",
};


static inline struct vcache_entry *vcache_entry_alloc(cstr_t path, hash_t hash, ventry_t *ve) {
  struct vcache_entry *entry = kmallocz(sizeof(struct vcache_entry));
  entry->path = str_copy_cstr(path);
  entry->hash = hash;
  entry->ve = ve_getref(ve); // take a reference
  return entry;
}

static inline void vcache_entry_free(struct vcache_entry *entry) {
  str_free(&entry->path);
  ve_release(&entry->ve);
  kfree(entry);
}

static inline struct vcache_dir *vcache_dir_alloc() {
  struct vcache_dir *dir = kmallocz(sizeof(struct vcache_dir));
  dir->capacity = 8;
  dir->children = kmallocz(sizeof(hash_t) * dir->capacity);
  return dir;
}

static inline void vcache_dir_free(struct vcache_dir *dir) {
  kfree(dir->children);
  kfree(dir);
}

static inline void vcache_dir_add(struct vcache_dir *dir, hash_t hash) {
  if (dir->count == dir->capacity) {
    dir->capacity *= 2;
    hash_t *children = kmallocz(sizeof(hash_t) * dir->capacity);
    memcpy(children, dir->children, sizeof(hash_t) * dir->count);
    kfree(dir->children);
    dir->children = children;
  }
  dir->children[dir->count++] = hash;
}

static inline bool vcache_dir_remove(struct vcache_dir *dir, hash_t hash) {
  for (size_t i = 0; i < dir->count; i++) {
    if (dir->children[i] == hash) {
      memmove(dir->children+i, dir->children+i+1, sizeof(hash_t) * (dir->count-i-1));
      dir->count--;
      return true;
    }
  }
  return false;
}

static inline struct vcache_entry *vcache_find_entry(vcache_t *vcache, cstr_t path, hash_t hash) {
  LIST_FOR_IN(e, &vcache->entries[hash % vcache->capacity], list) {
    if (e->hash == hash && cstr_eq(cstr_from_str(e->path), path)) {
      return e;
    }
  }
  return NULL;
}

static inline void vcache_insert_entry(vcache_t *vcache, struct vcache_entry *entry) {
  LIST_ADD(&vcache->entries[entry->hash % vcache->capacity], entry, list);
  vcache->size++;
}

static inline void vcache_remove_entry(vcache_t *vcache, struct vcache_entry *entry) {
  LIST_REMOVE(&vcache->entries[entry->hash % vcache->capacity], entry, list);
  vcache->size--;
}

static inline int vcache_invalidate_nolock(vcache_t *vcache, cstr_t path) {
  DPRINTF("invalidating path {:cstr}\n", &path);
  hash_t hash = ve_hash_cstr(vcache->root, path);
  struct vcache_entry *entry = vcache_find_entry(vcache, path, hash);
  if (!entry) {
    return -1;
  }

  ventry_t *ve = entry->ve;
  if (ve->type == V_DIR) {
    // if entry is a directory, recursively invalidate all children before
    // removing the vcache_dir itself
    rb_node_t *rb_node = rb_tree_find(vcache->dir_map, ve_unique_id(ve));
    ASSERT(rb_node != NULL);
    struct vcache_dir *direntry = rb_node->data;
    // invalidate children recursively and do it in a while loop because
    // the array will be modified as we go
    while (direntry->count > 0) {
      struct vcache_entry *child_entry = vcache_find_entry(vcache, path, direntry->children[0]);
      if (child_entry) {
        vcache_invalidate_nolock(vcache, cstr_from_str(child_entry->path));
      } else {
        DPRINTF("missing child entry %llu\n", &direntry->children[0]);
        vcache_dir_remove(direntry, direntry->children[0]);
      }
    }

    rb_tree_delete_node(vcache->dir_map, rb_node);
    vcache_dir_free(direntry);
  }

  // invalidate the vcache_entry itself and free it
  vcache_remove_entry(vcache, entry);
  vcache_entry_free(entry);

  if (cstr_eq(path, cstr_make("/"))) {
    return 0; // root entry
  }

  // finally remove it from the parent's directory entry
  cstr_t parent_path = cstr_dirname(path);
  hash_t parent_hash = ve_hash_cstr(vcache->root, parent_path);
  struct vcache_entry *parent = vcache_find_entry(vcache, parent_path, parent_hash);
  if (parent) {
    // DPRINTF("removing entry %u from parent dir (id=%u,%u)\n", ve->id, parent->ve->vfs_id, parent->ve->id);
    struct vcache_dir *direntry = rb_tree_get(vcache->dir_map, ve_unique_id(parent->ve));
    vcache_dir_remove(direntry, hash);
  }

  return 0;
}

static inline int vcache_put_nolock(vcache_t *vcache, cstr_t path, ventry_t *ve) {
  hash_t hash = ve_hash_cstr(ve, path);
  struct vcache_entry *entry = vcache_find_entry(vcache, path, hash);
  if (entry) {
    if (entry->ve == ve) {
      return 0; // already exists
    }
    // invalidate the old entry
    vcache_invalidate_nolock(vcache, path);
  }

  if (ve->type == V_DIR) {
    // if entry is a directory, add it to the directory map by its vnode id
    if (rb_tree_get(vcache->dir_map, ve_unique_id(ve)) != NULL) {
      panic("vcache: directory already exists in dir_map (id=%u,%u)\n", ve->vfs_id, ve->id);
      return -1;
    }

    struct vcache_dir *dir = vcache_dir_alloc();
    rb_tree_insert(vcache->dir_map, ve_unique_id(ve), dir);
  }

  // now add the ventry to the vcache
  entry = vcache_entry_alloc(path, hash, ve);
  vcache_insert_entry(vcache, entry);
  if (cstr_eq(path, cstr_make("/"))) {
    return 0; // root entry
  }

  // add the entry to its parent directory
  cstr_t parent_path = cstr_dirname(path);
  hash_t parent_hash = ve_hash_cstr(vcache->root, parent_path);
  struct vcache_entry *parent = vcache_find_entry(vcache, parent_path, parent_hash);
  if (parent) {
    // DPRINTF("adding entry %d to parent dir (id=%u,%u)\n", ve->id, parent->ve->vfs_id, parent->ve->id);
    struct vcache_dir *direntry = rb_tree_get(vcache->dir_map, ve_unique_id(parent->ve));
    vcache_dir_add(direntry, hash);
  }
  return 0;
}

//

vcache_t *vcache_alloc(ventry_t *root) {
  vcache_t *vcache = kmallocz(sizeof(vcache_t));
  vcache->root = ve_getref(root);
  vcache->capacity = VCACHE_INITIAL_SIZE;
  vcache->dir_map = create_rb_tree();
  vcache->entries = kmallocz(sizeof(*vcache->entries) * vcache->capacity);
  spin_init(&vcache->lock);
  return vcache;
}

void vcache_free(vcache_t *vcache) {
  ASSERT(vcache->size == 0);
  rb_tree_free(vcache->dir_map);
  ve_release(&vcache->root);
  kfree(vcache->entries);
}

ventry_t *vcache_get_root(vcache_t *vcache) {
  return vcache->root;
}

ventry_t *vcache_get(vcache_t *vcache, cstr_t path) __move {
  VCACHE_LOCK(vcache);
  hash_t hash = ve_hash_cstr(vcache->root, path);
  struct vcache_entry *entry = vcache_find_entry(vcache, path, hash);
  if (entry) {
    if (entry->ve->state == V_DEAD) {
      // invalidate the entry if its marked as dead
      vcache_invalidate_nolock(vcache, path);
      VCACHE_UNLOCK(vcache);
      return NULL;
    }

    VCACHE_UNLOCK(vcache);
    return ve_getref(entry->ve); // return new reference
  }
  VCACHE_UNLOCK(vcache);
  return NULL;
}

int vcache_put(vcache_t *vcache, cstr_t path, ventry_t *ve) {
  if (ve->state == V_DEAD)
    return -1;

  ve_hash(ve);
  VCACHE_LOCK(vcache);
  DPRINTF("caching {:cstr} [id=%u,%u]\n", &path, ve->vfs_id, ve->id);
  int ret = vcache_put_nolock(vcache, path, ve);
  VCACHE_UNLOCK(vcache);
  return ret;
}

int vcache_invalidate(vcache_t *vcache, cstr_t path) {
  VCACHE_LOCK(vcache);
  DPRINTF("invalidating {:cstr}\n", &path);
  int res = vcache_invalidate_nolock(vcache, path);
  // DPRINTF("done\n", &path);
  VCACHE_UNLOCK(vcache);
  return res;
}

void vcache_dump(vcache_t *vcache) {
  VCACHE_LOCK(vcache);
  kprintf("{:$=<21} vcache dump {:$=>20}\n");
  kprintf(" idx   | id       | type | path \n");
  kprintf("-------+----------+------+{:$-<30}\n");
  {
    for (size_t i = 0; i < vcache->capacity; i++) {
      struct vcache_entry *entry = LIST_FIRST(&vcache->entries[i]);
      while (entry) {
        ventry_t *ve = entry->ve;
        const char *type = vtype_to_str[ve->type];
        const char *state = V_ISALIVE(ve) ? "alive" : "dead";

        char unique_id[32] = {0};
        ksnprintf(unique_id, sizeof(unique_id)-1, "%u,%u", ve->vfs_id, ve->id);

        char entrystr[256] = {0};
        ksnprintf(entrystr, sizeof(entrystr)-1, "",
                  i, ve->vfs_id, ve->id, type, str_cptr(entry->path));
        kio_t kio = kio_new_writeonly(entrystr, sizeof(entrystr)-1);
        kio_sprintf(&kio, " {:>5zu} | {:>8s} | {:4s} | {:29s} ", i, unique_id, type, str_cptr(entry->path));

        if (V_ISDIR(ve)) {
          struct vcache_dir *direntry = rb_tree_get(vcache->dir_map, ve_unique_id(ve));
          ASSERT(direntry != NULL);
          kio_sprintf(&kio, "(%zu", direntry->count);
          if (direntry->count == 1) {
            kio_sprintf(&kio, " entry)");
          } else {
            kio_sprintf(&kio, " entries)");
          }
        }

        if (V_ISDEAD(ve))
          kio_sprintf(&kio, " DEAD");
        kio_write_ch(&kio, 0); // null terminate

        kprintf("%s\n", entrystr);
        entry = LIST_NEXT(entry, list);
      }
    }
  }
  kprintf("{:$-<56}\n");
  VCACHE_UNLOCK(vcache);
}
