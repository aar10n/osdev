//
// Created by Aaron Gill-Braun on 2022-11-19.
//

#include <debug/debug.h>
#include <debug/dwarf.h>
#include <mm.h>

#include <queue.h>
#include <string.h>
#include <printf.h>
#include <panic.h>

#include <interval_tree.h>

// typedef struct

// caches dwarf_file_t structures and allows for quickly
// querying by address.
static intvl_tree_t *debug_files;

//

void debug_early_init() {
  dwarf_early_init();
}

void debug_init() {
  if (dwarf_init_debug() < 0) {
    // only returns -1 if there is no debugging information or we
    // failed to initialize libdwarf (possibly bad debug info?)
    kprintf("debug: unable to initialize debugging\n");
    return;
  }
  dwarf_collect_debug_info();

  // TODO: reclaim memory mapped to sections and free structures on error
  dwarf_file_t *files;
  if (dwarf_debug_load_files(&files) < 0) {
    kprintf("debug: failed to load debug files\n");
    return;
  }

  debug_files = create_intvl_tree();
  RLIST_FOR_IN(file, files, list) {
    intvl_tree_insert(debug_files, intvl(file->addr_lo, file->addr_hi), file);
  }

  kprintf("debug: files loaded\n");
  // kprintf("debug: loaded functions\n");
  kprintf("debug: initialized\n");
}

//

char *debug_addr2line(uintptr_t addr) {
  if (addr == 0) {
    return kasprintf("<null>");
  } else if (!mm_is_kernel_code_ptr(addr)) {
    goto INVALID;
  }

  intvl_node_t *node = intvl_tree_find(debug_files, intvl(addr, addr));
  if (node == NULL) {
    goto INVALID;
  }

  dwarf_file_t *file = node->data;
  if (file->lines == NULL) {
    if (dwarf_file_load_lines(file) < 0) {
      return kasprintf("<debug error>");
    }
  }

  dwarf_line_t *line = NULL;
  {
    dwarf_line_t *prev = &file->lines[0];
    for (uint32_t i = 0; i < file->line_count; i++) {
      if ((file->lines[i].addr == addr) ||
          (prev && (addr > prev->addr) && (addr < file->lines[i].addr))) {
        line = &file->lines[i];
        break;
      }
    }
  }

  if (line == NULL) {
    return kasprintf("%s", file->name);
  }
  return kasprintf("%s:%d", file->name, line->line_no);

LABEL(INVALID);
  return kasprintf("<invalid>");
}
