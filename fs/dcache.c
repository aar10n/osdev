//
// Created by Aaron Gill-Braun on 2021-07-14.
//

#include <dcache.h>
#include <mm.h>
#include <panic.h>

#define DCACHE_SIZE 4096


struct dcache *dcache_create() {
  struct dcache *dcache = kmalloc(sizeof(struct dcache));
  dcache->size = DCACHE_SIZE;
  dcache->count = 0;
  dcache->buckets = NULL;
  spin_init(&dcache->lock);

  size_t count = SIZE_TO_PAGES(dcache->size * sizeof(struct dentry *));
  dcache->pages = valloc_zero_pages(count, PG_WRITE);
  if (dcache->pages == NULL) {
    panic("failed to allocate dcache pages");
  }

  return dcache;
}

void dcache_destroy(struct dcache *dcache) {
  vfree_pages(dcache->pages);
  kfree(dcache);
}

int dcache_put(struct dcache *dcache, struct dentry *dentry) {

}

int dcache_remove(struct dcache *dcache, struct dentry *dentry) {

}
