//
// Created by Aaron Gill-Braun on 2024-10-24.
//

#include <kernel/mm/pgcache.h>
#include <kernel/mm.h>

#include <kernel/panic.h>
#include <math.h>

#define log2(x) (63 - __builtin_clzll(x))

struct pgcache_node {
  void *slots[PGCACHE_FANOUT];
  struct {
    uint16_t count;
    uint16_t dirty[PGCACHE_FANOUT / (8 * sizeof(uint16_t))];
    LIST_ENTRY(struct pgcache_node) list; // leaf nodes
  } leaf;
};

//

#define MAX_STACK_SIZE 64

struct visit_stack_entry {
  struct pgcache_node *node;
  int slot_index;
  uint16_t level;
};

static void internal_visit_pages_iter(
  struct pgcache *cache,
  struct pgcache_node **noderef,
  size_t start_off,
  size_t end_off,
  bool free,
  uint16_t level,
  pgcache_visit_t fn,
  void *data
) {
  size_t max_off = cache->max_capacity;
  if (end_off == 0) {
    end_off = max_off;
  } else {
    end_off = min(end_off, max_off);
  }
  if (start_off >= end_off) {
    return;
  }

  struct pgcache_node *root;
  if (free) {
    root = moveptr(*noderef);
  } else {
    root = *noderef;
  }

  struct visit_stack_entry stack[MAX_STACK_SIZE];
  int top = 0;

  stack[top++] = (struct visit_stack_entry){
    .node = root,
    .slot_index = 0,
    .level = level
  };

  while (top > 0) {
    struct visit_stack_entry *current = &stack[top - 1];
    struct pgcache_node *node = current->node;
    int i = current->slot_index;

    if (i >= PGCACHE_FANOUT) {
      top--;
      continue;
    }

    current->slot_index++;
    size_t slot_start = (i << (cache->bits_per_lvl * (cache->order - current->level)));
    size_t slot_end = slot_start + (1ULL << (cache->bits_per_lvl * (cache->order - current->level)));
    if (slot_end <= start_off || slot_start >= end_off) {
      continue;
    }

    if (node->slots[i]) {
      if (current->level < cache->order) {
        stack[top++] = (struct visit_stack_entry){
          .node = (struct pgcache_node *) node->slots[i],
          .slot_index = 0,
          .level = current->level + 1
        };
      } else {
        fn((page_t **)&node->slots[i], slot_start, data);
        if (free) {
          kassert(node->slots[i] == NULL);
        }
      }
    }
  }
}

static struct pgcache_node *internal_lookup_leaf(struct pgcache *tree, size_t off, bool insert, __out size_t *out_idx) {
  kassert(off < tree->max_capacity);
  struct pgcache_node *node = tree->root;
  size_t bits_per_lvl = tree->bits_per_lvl;
  size_t pg_size = tree->pg_size;
  size_t order = tree->order;
  size_t idx;

  off >>= log2(pg_size); // shift out the page offset
  for (size_t i = 0; i < order; i++) {
    idx = (off & ((1 << bits_per_lvl) - 1));
    kassert(idx < PGCACHE_FANOUT);
    struct pgcache_node *child = node->slots[idx];
    if (child == NULL) {
      if (!insert)
        return NULL;

      // allocate the inner node
      child = kmallocz(sizeof(struct pgcache_node));
      node->slots[idx] = child;
      if (i == order - 1) { // this is a leaf node
        LIST_ADD(&tree->leaf_nodes, child, leaf.list);
      }
    }
    off >>= bits_per_lvl;
    node = child;
  }

  idx = off & ((1 << bits_per_lvl) - 1);
  kassert(idx < PGCACHE_FANOUT);
  *out_idx = idx;
  return node;
}

static void pgcache_clone_cb(page_t **pageref, size_t off, void *data) {
  struct pgcache *copy = data;
  page_t *page = pg_getref(*pageref);
  pgcache_insert(copy, off, page, NULL);
}

static void pgcache_putpage_cb(page_t **pageref, size_t off, void *data) {
  page_t *page = moveref(*pageref);
  pg_putref(&page);
}

//
// MARK: pgcache api
//

__ref struct pgcache *pgcache_alloc(uint16_t order, uint32_t pg_size) {
  struct pgcache *tree = kmallocz(sizeof(struct pgcache));
  tree->order = order;
  tree->bits_per_lvl = log2(PGCACHE_FANOUT);
  tree->pg_size = pg_size;
  tree->max_capacity = (1ULL << ((order + 1) * tree->bits_per_lvl)) * pg_size;
  tree->root = kmallocz(sizeof(struct pgcache_node));
  ref_init(&tree->refcount);

  if (order == 0) {
    LIST_ADD(&tree->leaf_nodes, tree->root, leaf.list);
  }
  return tree;
}

__ref struct pgcache *pgcache_clone(struct pgcache *cache) {
  struct pgcache *copy = pgcache_alloc(cache->order, cache->pg_size);
  pgcache_visit_pages(cache, 0, cache->max_capacity, pgcache_clone_cb, copy);
  return copy;
}

void pgcache_free(struct pgcache **cacheptr) {
  struct pgcache *cache = moveref(*cacheptr);
  if (cache && ref_put(&cache->refcount)) {
    internal_visit_pages_iter(cache, &cache->root, 0, cache->max_capacity, /*free=*/true, 0, (void *) pgcache_putpage_cb, NULL);
    kfree(cache);
  }
}

void pgcache_resize(struct pgcache *cache, uint16_t new_order) {
  todo("pgcache resizing");
}

__ref page_t *pgcache_lookup(struct pgcache *cache, size_t off) {
  if (off >= cache->max_capacity) {
    return NULL;
  }

  size_t idx;
  struct pgcache_node *node = internal_lookup_leaf(cache, off, /*insert=*/false, &idx);
  if (node == NULL) {
    return NULL;
  }
  return pg_getref((page_t *) node->slots[idx]);
}

void pgcache_insert(struct pgcache *cache, size_t off, __ref page_t *page, __move page_t **out_old) {
  if (off >= cache->max_capacity) {
    pg_putref(&page);
    return;
  }

  size_t idx;
  struct pgcache_node *node = internal_lookup_leaf(cache, off, /*insert=*/true, &idx);
  kassert(node != NULL);

  page_t *old = moveref(node->slots[idx]);
  node->slots[idx] = moveref(page);
  node->leaf.count++;

  if (out_old) {
    *out_old = moveref(old);
  } else {
    pg_putref(&old);
  }
}

void pgcache_remove(struct pgcache *cache, size_t off, __move page_t **out_page) {
  if (off >= cache->max_capacity) {
    return;
  }

  size_t idx;
  struct pgcache_node *node = internal_lookup_leaf(cache, off, /*insert=*/false, &idx);
  if (node == NULL || node->slots[idx] == NULL) {
    return;
  }

  node->leaf.count--;
  // TODO: free leaf nodes if they are empty?

  if (out_page) {
    *out_page = moveref(node->slots[idx]);
  } else {
    pg_putref((page_t **) &node->slots[idx]);
  }
}

void pgcache_visit_pages(struct pgcache *cache, size_t start_off, size_t end_off, pgcache_visit_t fn, void *data) {
  internal_visit_pages_iter(cache, &cache->root, start_off, end_off, /*free=*/false, 0, fn, data);
}
