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

static intvl_tree_t *debug_files;
static intvl_tree_t *debug_functions;
static bool has_debug_info = false;

static dwarf_file_t *locate_or_load_dwarf_file(uintptr_t addr) {
  intvl_node_t *node = intvl_tree_find(debug_files, intvl(addr, addr + 1));
  if (node == NULL) {
    return NULL;
  }

  dwarf_file_t *file = node->data;
  if (file->lines == NULL) {
    if (dwarf_file_load_lines(file) < 0) {
      kprintf("debug: failed to load lines for %s\n", file->name);
      return NULL;
    }

    if (dwarf_file_load_funcs(file) < 0) {
      kprintf("debug: failed to load functions for %s\n", file->name);
      return NULL;
    }

    SLIST_FOR_IN(func, file->functions, next) {
      // kprintf("debug: function %s [%p]\n", func->name, func->addr_lo);
      intvl_tree_insert(debug_functions, intvl(func->addr_lo, func->addr_hi), func);
    }
  }

  return file;
}

static dwarf_function_t *locate_or_load_dwarf_function(uintptr_t addr) {
  intvl_node_t *node = intvl_tree_find(debug_functions, intvl(addr, addr + 1));
  if (node == NULL) {
    dwarf_file_t *file = locate_or_load_dwarf_file(addr);
    if (file == NULL) {
      return NULL;
    }

    node = intvl_tree_find(debug_functions, intvl(addr, addr + 1));
    if (node) {
      return node->data;
    }
    return NULL;
  }
  return node->data;
}

static dwarf_line_t *get_line_by_addr(dwarf_file_t *file, uintptr_t addr) {
  dwarf_line_t *prev = &file->lines[0];
  for (uint32_t i = 0; i < file->line_count; i++) {
    if ((file->lines[i].addr == addr) ||
        (prev && (addr > prev->addr) && (addr < file->lines[i].addr))) {
      return &file->lines[i];
    }
  }
  return NULL;
}

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

  has_debug_info = true;
  debug_files = create_intvl_tree();
  debug_functions = create_intvl_tree();
  RLIST_FOR_IN(file, files, list) {
    intvl_tree_insert(debug_files, intvl(file->addr_lo, file->addr_hi), file);
  }

  kprintf("debug: files loaded\n");

  // preload things
  static const char *prefix = "/Users/aaron/Projects/osdev/kernel";
  RLIST_FOR_IN(file, files, list) {
    if (strncmp(prefix, file->name, strlen(prefix)) == 0) {
      kprintf("debug: preloading %s\n", file->name);
      locate_or_load_dwarf_file(file->addr_lo);
    }
  }

  // kprintf("debug: loaded functions\n");
  kprintf("debug: initialized\n");
}

//

const char *debug_function_name(uintptr_t addr) {
  if (!has_debug_info) {
    return NULL;
  }

  dwarf_function_t *func = locate_or_load_dwarf_function(addr);
  if (func == NULL) {
    kprintf("debug: failed to find function name for address %p\n", addr);
    return NULL;
  }

  return func->name;
}

char *debug_addr2line(uintptr_t addr) {
  if (addr == 0) {
    return kasprintf("<null>");
  } else if (!mm_is_kernel_code_ptr(addr)) {
    return kasprintf("<invalid>");
  } else if (!has_debug_info) {
    goto INVALID;
  }

  dwarf_file_t *file = locate_or_load_dwarf_file(addr);
  if (file == NULL) {
    goto INVALID;
  }

  dwarf_line_t *line = get_line_by_addr(file, addr);
  if (line == NULL) {
    return kasprintf("%s", file->name);
  }
  return kasprintf("%s:%d", file->name, line->line_no);

LABEL(INVALID);
  return kasprintf("??");
}

int debug_unwind(uintptr_t rip, uintptr_t rbp) {
  if (!has_debug_info) {
    return -1;
  }

  kprintf("backtrace\n");

  stackframe_t *frame = (void *) rbp;
  while (mm_is_kernel_code_ptr((uintptr_t) frame->rip)) {
    dwarf_function_t *func = locate_or_load_dwarf_function(rip);
    if (func == NULL) {
      kprintf("    ?? %018p\n", rip);
    } else {
      dwarf_line_t *line = get_line_by_addr(func->file, rip);
      if (line == NULL) {
        kprintf("    %s %018p\n", func->name, rip);
      } else {
        kprintf("    %s %018p [%s:%d]\n", func->name, rip, func->file->name, line->line_no);
      }
    }

    if (_vm_virt_to_phys((uintptr_t) frame->rbp) == 0) {
      kprintf("    ?? %018p\n", frame->rip);
      break;
    }

    rip = frame->rip;
    frame = frame->rbp;
  }

  return 0;
}
