//
// Created by Aaron Gill-Braun on 2022-11-21.
//

#ifndef KERNEL_DEBUG_DWARF_H
#define KERNEL_DEBUG_DWARF_H

#include <base.h>
#include <queue.h>

typedef struct dwarf_file dwarf_file_t;
typedef struct dwarf_function dwarf_function_t;
typedef struct dwarf_line dwarf_line_t;

/// a dwarf file (compilation unit)
typedef struct dwarf_file {
  const char *name;
  uintptr_t addr_lo;
  uintptr_t addr_hi;
  size_t die_off;
  uint8_t version;
  LIST_ENTRY(dwarf_file_t) list;

  // not valid until dwarf_file_load_lines() call
  dwarf_line_t *lines;
  size_t line_count;

  // not valid until dwarf_file_load_functions() call
  LIST_HEAD(dwarf_function_t) functions;
} dwarf_file_t;

/// a dwarf function (subprogram)
typedef struct dwarf_function {
  const char *name;
  uintptr_t addr_lo;
  uintptr_t addr_hi;
  size_t die_off;

  dwarf_line_t *line_start;
  dwarf_line_t *line_end;

  SLIST_ENTRY(dwarf_function_t) next;
} dwarf_function_t;

/// a dwarf source line
typedef struct dwarf_line {
  uint32_t line_no;
  uint32_t column_no;
  uintptr_t addr;
} dwarf_line_t;

void dwarf_early_init();
int dwarf_init_debug();
int dwarf_collect_debug_info();

// dwarf_file_t *dwarf_load_file(uintptr_t addr);
// dwarf_function_t *dwarf_load_function(dwarf_file_t *file, uintptr_t addr);

int dwarf_debug_load_files(dwarf_file_t **out_file);
int dwarf_file_load_lines(dwarf_file_t *file);
int dwarf_file_load_functions(dwarf_file_t *file);
void dwarf_free_file(dwarf_file_t *file);

#endif
