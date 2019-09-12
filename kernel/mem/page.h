//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#ifndef KERNEL_MEM_PAGE_H
#define KERNEL_MEM_PAGE_H

#include "mm.h"

void init_paging();
void map_page(page_t *page);
void remap_page(page_t *page);
void unmap_page(page_t *page);

#endif // KERNEL_MEM_PAGE_H
