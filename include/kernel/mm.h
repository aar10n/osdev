//
// Created by Aaron Gill-Braun on 2021-03-24.
//

#ifndef INCLUDE_KERNEL_MM_H
#define INCLUDE_KERNEL_MM_H

#include <base.h>
#include <mm_types.h>
#include <mm/heap.h>
#include <mm/pmalloc.h>
#include <mm/vmalloc.h>

// #include <mm/mm.h>
// #include <mm/vm.h>
// #include <cpu/cpu.h>

#define kernel_phys_to_virt(x) (KERNEL_OFFSET + (x))

#define virt_to_phys(ptr) vm_virt_to_phys((uintptr_t)(ptr))
#define virt_to_phys_ptr(ptr) ((void *) vm_virt_to_phys((uintptr_t)(ptr)))




#endif
