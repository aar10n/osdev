//
// Created by Aaron Gill-Braun on 2024-10-24.
//

#include <kernel/mm/file.h>
#include <kernel/mm/pmalloc.h>
#include <kernel/mm/pgcache.h>
#include <kernel/vfs/vnode.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)


static __ref page_t *anon_getpage_missing(vm_file_t *file, size_t off) {
  return alloc_pages_size(1, file->pg_size);
}

static __ref page_t *vnode_getpage_missing(vm_file_t *file, size_t off) {
  int res;
  page_t *page;
  vnode_t *vn = file->vnode;
  if ((res = vn_getpage(vn, (off_t) off, /*cached=*/false, &page)) < 0) {
    return NULL;
  }
  return page;
}

//
//

vm_file_t *vm_file_alloc_vnode(__ref struct vnode *vn, size_t off, size_t size) {
  ASSERT(vn != NULL);
  vm_file_t *file = kmallocz(sizeof(vm_file_t));
  file->size = size;
  file->off = off;
  file->pg_size = PAGE_SIZE;

  if (!vn_lock(vn))
    panic("vnode is dead");

  file->vnode = vn_moveref(&vn);
  file->pgcache = vn_get_pgcache(file->vnode);
  file->missing_page = vnode_getpage_missing;
  return file;
}

vm_file_t *vm_file_alloc_anon(size_t size, size_t pg_size) {
  vm_file_t *file = kmallocz(sizeof(vm_file_t));
  file->size = size;
  file->off = 0;
  file->pg_size = pg_size;

  file->vnode = NULL;
  file->pgcache = pgcache_alloc(pgcache_size_to_order(size, pg_size), pg_size);
  file->missing_page = anon_getpage_missing;
  return file;
}

vm_file_t *vm_file_fork(vm_file_t *file) {
  vm_file_t *new_file = kmallocz(sizeof(vm_file_t));
  new_file->size = file->size;
  new_file->off = file->off;
  new_file->pg_size = file->pg_size;

  new_file->vnode = getref(file->vnode);
  new_file->pgcache = getref(file->pgcache);
  new_file->missing_page = file->missing_page;
  return new_file;
}

void vm_file_free(vm_file_t **fileref) {
  vm_file_t *file = moveref(*fileref);
  if (file->vnode != NULL) {
    vn_release(&file->vnode);
    file->pgcache = NULL;
  } else {
    pgcache_free(&file->pgcache);
  }
}

__ref page_t *vm_file_getpage(vm_file_t *file, size_t off) {
  if (off >= file->size) {
    return NULL;
  }

  off += file->off;
  page_t *page = pgcache_lookup(file->pgcache, off);
  if (page == NULL) {
    page = file->missing_page(file, off);
    if (page != NULL) {
      pgcache_insert(file->pgcache, off, getref(page), NULL);
    }
  }
  return moveref(page);
}

uintptr_t vm_file_getpage_phys(vm_file_t *file, size_t off) {
  page_t *page = vm_file_getpage(file, off);
  ASSERT(page != NULL);
  uintptr_t phys = page->address;
  drop_pages(&page);
  return phys;
}

void vm_file_visit_pages(vm_file_t *file, size_t start_off, size_t end_off, pgcache_visit_t fn, void *data) {
  pgcache_visit_pages(file->pgcache, start_off, end_off, fn, data);
}


vm_file_t *vm_file_split(vm_file_t *file, size_t off) {
  ASSERT(off % file->pg_size == 0);
  if (off >= file->size) {
    return NULL;
  }


  vm_file_t *new_file = kmallocz(sizeof(vm_file_t));
  new_file->size = file->size - off;
  new_file->off = file->off + off;
  new_file->pg_size = file->pg_size;

  new_file->vnode = getref(file->vnode);
  new_file->pgcache = getref(file->pgcache);
  new_file->missing_page = file->missing_page;

  file->size = off;
  return new_file;
}

void vm_file_merge(vm_file_t *file, vm_file_t **otherref) {
  vm_file_t *other = moveref(*otherref);
  ASSERT(file->pg_size == other->pg_size);
  ASSERT(file->vnode == other->vnode);
  ASSERT(file->pgcache == other->pgcache);

  file->size += other->size;
  vm_file_free(&other);
}
