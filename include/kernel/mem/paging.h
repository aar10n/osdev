//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#ifndef KERNEL_MEM_PAGE_H
#define KERNEL_MEM_PAGE_H

#include "mm.h"

#define PE_PRESENT 0x01
#define PE_READ_WRITE 0x02
#define PE_USER_SUPERVISOR 0x04
#define PE_WRITE_THROUGH 0x08
#define PE_CACHE_DISABLED 0x16
#define PE_PAGE_SIZE_4MB 0x64

void paging_init();
void map_page(page_t *page);
void remap_page(page_t *page);
void unmap_page(page_t *page);

#endif // KERNEL_MEM_PAGE_H
