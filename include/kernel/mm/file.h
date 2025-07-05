//
// Created by Aaron Gill-Braun on 2024-10-24.
//

#ifndef KERNEL_MM_FILE_H
#define KERNEL_MM_FILE_H

#include <kernel/mm_types.h>
#include <kernel/mm/pgcache.h>

struct vnode;
struct vm_file;

typedef __ref struct page *(*vm_getpage_t)(struct vm_file *file, size_t off);

/**
 * A virtual memory file.
 */
typedef struct vm_file {
  size_t size;                   // size of the file
  size_t off;                    // offset into the file
  size_t pg_size;                // size of each page

  __ref struct vnode *vnode;     // backing vnode ref (null if anonymous)
  __ref struct pgcache *pgcache; // the page cache (global)
  vm_getpage_t missing_page;     // callback to get a missing page
} vm_file_t;


vm_file_t *vm_file_alloc_vnode(__ref struct vnode *vn, size_t off, size_t size);
vm_file_t *vm_file_alloc_anon(size_t size, size_t pg_size);
vm_file_t *vm_file_alloc_copy(vm_file_t *file);
vm_file_t *vm_file_alloc_clone(vm_file_t *file);
void vm_file_free(vm_file_t **fileref);
__ref page_t *vm_file_getpage(vm_file_t *file, size_t off);
uintptr_t vm_file_getpage_phys(vm_file_t *file, size_t off);
int vm_file_putpage(vm_file_t *file, __ref page_t *page, size_t off, __move page_t **oldpage);
void vm_file_visit_pages(vm_file_t *file, size_t start_off, size_t end_off, pgcache_visit_t fn, void *data);

vm_file_t *vm_file_split(vm_file_t *file, size_t off);
void vm_file_merge(vm_file_t *file, vm_file_t **otherref);


#endif
