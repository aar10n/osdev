//
// Created by Aaron Gill-Braun on 2022-11-21.
//

#include <kernel/debug/dwarf.h>
#include <kernel/init.h>
#include <kernel/mm.h>

#include <kernel/queue.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <libdwarf-0/dwarf.h>
#include <libdwarf-0/libdwarf.h>

#define DEALLOC_DIE(die) ({ if ((die) != NULL) dwarf_dealloc(dwarf_debug, die, DW_DLA_DIE); 0; })
#define DEALLOC_ERR(err) ({ if ((err) != NULL) dwarf_dealloc(dwarf_debug, err, DW_DLA_ERROR); 0; })
#define DEALLOC_STR(str) ({ if ((die) != NULL) dwarf_dealloc(dwarf_debug, die, DW_DLA_STRING); 0; })

#define CHECK_ERROR(expr, label_err) ({ if ((result = (expr)) != DW_DLV_OK) goto label_err; })

#define FREE_FUNC_LIST(first) \
  ({                         \
    dwarf_function_t *func = first;  \
    while (func != NULL) {   \
      dwarf_function_t *next = func->next; \
      kfree(func);           \
      func = next;           \
    }                        \
    (first) = NULL;          \
    NULL;                    \
  })

LOAD_SECTION(__eh_frame_section, ".eh_frame");
LOAD_SECTION(__debug_info_section, ".debug_info");
LOAD_SECTION(__debug_abbrev_section, ".debug_abbrev");
LOAD_SECTION(__debug_aranges_section, ".debug_aranges");
LOAD_SECTION(__debug_line_section, ".debug_line");
LOAD_SECTION(__debug_str_section, ".debug_str");
LOAD_SECTION(__debug_line_str_section, ".debug_line_str");

static loaded_section_t *debug_sections[] = {
  NULL, // libdwarf requires that the first section be empty
  &__eh_frame_section,
  &__debug_info_section,
  &__debug_abbrev_section,
  &__debug_aranges_section,
  &__debug_line_section,
  &__debug_str_section,
  &__debug_line_str_section,
};
static size_t num_sections = sizeof(debug_sections) / sizeof(loaded_section_t *);

// debugging info
static bool has_debugging_info;
static Dwarf_Debug dwarf_debug;

static Dwarf_Cie *dwarf_cie_list;
static Dwarf_Signed dwarf_cie_count;
static Dwarf_Fde *dwarf_fde_list;
static Dwarf_Signed dwarf_fde_count;

static Dwarf_Arange *dwarf_aranges;
static Dwarf_Signed dwarf_aranges_count;

// MARK: DWARF Object Interface

static int get_section_info(unused void *obj, Dwarf_Half section_index,
                            Dwarf_Obj_Access_Section_a *return_section, int *error) {
  *error = 0;
  if (section_index >= num_sections) {
    return DW_DLV_NO_ENTRY;
  }

  loaded_section_t *section = debug_sections[section_index];
  const char *name;
  uintptr_t data;
  size_t size;
  if (section == NULL) {
    name = "";
    data = 0;
    size = 0;
  } else {
    name = section->name;
    data = section->virt_addr;
    size = section->size;
  }

  return_section->as_name = name;
  return_section->as_type = 0;
  return_section->as_flags = 0;
  return_section->as_addr = (uintptr_t) data;
  return_section->as_offset = 0;
  return_section->as_size = size;
  return_section->as_link = 0;
  return_section->as_info = 0;
  return_section->as_addralign = 0;
  return_section->as_entrysize = 1;
  return DW_DLV_OK;
}

static Dwarf_Small get_byte_order(unused void *obj) {
  return DW_END_little;
}

static Dwarf_Small get_length_size(unused void *obj) {
  return sizeof(size_t);
}

static Dwarf_Small get_pointer_size(unused void *obj) {
  return sizeof(uintptr_t);
}

static Dwarf_Unsigned get_file_size(unused void *obj) {
  // this is used to prealloc memory for internal libdwarf data structures.
  // it divides the value by 30
  size_t size = 50000;
  return size;
}

static Dwarf_Unsigned get_section_count(unused void *obj) {
  return num_sections;
}

static int load_section(unused void *obj, Dwarf_Half section_index, Dwarf_Small **return_data, int *error) {
  *error = 0;
  if (section_index >= num_sections) {
    return DW_DLV_NO_ENTRY;
  } else if (section_index == 0) {
    *return_data = NULL;
    return DW_DLV_OK;
  }

  loaded_section_t *section = debug_sections[section_index];
  *return_data = (void *) section->virt_addr;
  return DW_DLV_OK;
}

Dwarf_Obj_Access_Methods_a dwarf_methods = {
  .om_get_section_info = get_section_info,
  .om_get_byte_order = get_byte_order,
  .om_get_length_size = get_length_size,
  .om_get_pointer_size = get_pointer_size,
  .om_get_filesize = get_file_size,
  .om_get_section_count = get_section_count,
  .om_load_section = load_section,
};

Dwarf_Obj_Access_Interface_a dwarf_interface = {
  .ai_methods = &dwarf_methods,
  .ai_object = NULL,
};

void debug_dwarf_error_handler(Dwarf_Error dw_error, Dwarf_Ptr dw_errarg) {
  kprintf("debug: handling dwarf error\n");
}

// MARK: DWARF Helpers

static int dwarf_attrval_addr(Dwarf_Die die, Dwarf_Half attrnum, Dwarf_Addr *return_val, Dwarf_Error *error) {
  Dwarf_Attribute attr;

  int result = dwarf_attr(die, attrnum, &attr, error);
  if (result == DW_DLV_OK) {
    result = dwarf_formaddr(attr, return_val, error);
    dwarf_dealloc_attribute(attr);
  }
  return result;
}

static int dwarf_attrval_string(Dwarf_Die die, Dwarf_Half attrnum, char **return_val, Dwarf_Error *error) {
  Dwarf_Attribute attr;

  int result = dwarf_attr(die, attrnum, &attr, error);
  if (result == DW_DLV_OK) {
    // NOTE: the string returned by this must not be dealloc'd!
    result = dwarf_formstring(attr, return_val, error);
    dwarf_dealloc_attribute(attr);
  }
  return result;
}

static int dwarf_attrval_exprloc(
  Dwarf_Die die,
  Dwarf_Half attrnum,
  Dwarf_Unsigned *return_exprlen,
  Dwarf_Ptr *return_block_ptr,
  Dwarf_Error *error
) {
  Dwarf_Attribute attr;

  int result = dwarf_attr(die, attrnum, &attr, error);
  if (result == DW_DLV_OK) {
    result = dwarf_formexprloc(attr, return_exprlen, return_block_ptr, error);
    dwarf_dealloc_attribute(attr);
  }
  return result;
}

static int dwarf_die_pc_range(Dwarf_Die die, Dwarf_Addr *out_lopc, Dwarf_Addr *out_hipc, Dwarf_Error *error) {
  int result;
  Dwarf_Addr lo;
  Dwarf_Addr hi;
  Dwarf_Half hi_form;
  enum Dwarf_Form_Class hi_class;

  *error = NULL;
  *out_lopc = 0;
  *out_hipc = 0;

  result = dwarf_lowpc(die, &lo, error);
  if (result != DW_DLV_OK) {
    result = dwarf_attrval_addr(die, DW_AT_low_pc, &lo, error);
    if (result != DW_DLV_OK) {
      return result;
    }
  }

  result = dwarf_highpc_b(die, &hi, &hi_form, &hi_class, error);
  if (result != DW_DLV_OK) {
    if (result == DW_DLV_NO_ENTRY) {
      kprintf("dwarf: no high pc\n");
      // assume lo == hi if no hi entry exists
      *out_lopc = lo;
      *out_hipc = hi;
      return DW_DLV_OK;
    }
    return result;
  }

  if (hi_class == DW_FORM_CLASS_CONSTANT) {
    // `hi` is a relative offset from `lo`
    *out_lopc = lo;
    *out_hipc = lo + hi;
  } else {
    // `hi` should be an address
    kassert(hi_class == DW_FORM_CLASS_ADDRESS);
    *out_lopc = lo;
    *out_hipc = hi;
  }
  return DW_DLV_OK;
}

static int dwarf_find_die_in_siblings(
  Dwarf_Die in_die,
  Dwarf_Half die_tag,
  Dwarf_Bool free_in_die,
  Dwarf_Die *out_die,
  Dwarf_Error *error
) {
  int result;
  Dwarf_Die die = in_die;
  Dwarf_Die first_die = in_die; // save in_die in case in_die == out_die
  Dwarf_Half tag;

  *error = NULL;

  int count = 0;
  while (true) {
    Dwarf_Die temp_die;
    result = dwarf_siblingof_b(dwarf_debug, die, true, &temp_die, error);
    if (result != DW_DLV_OK) {
      return result;
    }

    // free previous die
    if (count > 0) {
      DEALLOC_DIE(die);
    }

    die = temp_die;
    count++;

    result = dwarf_tag(die, &tag, error);
    if (result != DW_DLV_OK) {
      return result;
    }

    if (tag == die_tag) {
      *out_die = die;
      if (free_in_die)
        DEALLOC_DIE(first_die);
      return DW_DLV_OK;
    }
  }
}

static int dwarf_find_die_in_children(
  Dwarf_Die in_parent,
  Dwarf_Die in_child,
  Dwarf_Half die_tag,
  Dwarf_Die *out_die,
  Dwarf_Error *error
) {
  int result;
  Dwarf_Die die = in_child;

  *error = NULL;
  *out_die = NULL;

  if (in_child == NULL) {
    // load the first child
    if ((result = dwarf_child(in_parent, &die, error)) != DW_DLV_OK) {
      return result;
    }

    Dwarf_Half tag;
    if ((result = dwarf_tag(die, &tag, error)) != DW_DLV_OK) {
      return result;
    }

    // check it against tag
    if (tag == die_tag) {
      *out_die = die;
      return DW_DLV_OK;
    }
  }
  return dwarf_find_die_in_siblings(die, die_tag, true, out_die, error);
}

//
// MARK: DWARF API
//

void remap_dwarf_sections(void *data) {
  loaded_section_t *section = data;
  size_t size = PAGES_TO_SIZE(SIZE_TO_PAGES(section->size)); // align to page
  if (section->virt_addr != 0) {
    return;
  }

  char name[64] = {0};
  ksnprintf(name, 63, "elf %s", section->name);
  section->virt_addr = vmap_phys(section->phys_addr, 0, size, VM_READ, name);
}

void dwarf_early_init() {
  for (int i = 1; i < num_sections; i++) {
    loaded_section_t *section = debug_sections[i];
    if (section->phys_addr == 0) {
      // TODO: support partial functionality while missing some optional sections
      // no debug info
      has_debugging_info = false;
      return;
    }
  }

  // register callbacks to remap the sections into the virtual address space
  has_debugging_info = true;
  for (int i = 1; i < num_sections; i++) {
    loaded_section_t *section = debug_sections[i];
    register_init_address_space_callback(remap_dwarf_sections, section);
  }
}

int dwarf_init_debug() {
  int result;
  Dwarf_Error error = 0;

  if (has_debugging_info == false) {
    // if there's no debugging info in the kernel image or if some sections
    // were unable to be loaded then we'll continue without rich debugging
    // facilities.
    kprintf("dwarf: no debugging info available\n");
    return -1;
  }

  result = dwarf_object_init_b(&dwarf_interface, debug_dwarf_error_handler, 0, DW_GROUPNUMBER_ANY, &dwarf_debug, &error);
  if (result != DW_DLV_OK) {
    if (result == DW_DLV_NO_ENTRY) {
      kprintf("dwarf: malformed debugging info\n");
    } else {
      kprintf("dwarf: failed to initialize libdwarf\n");
      kprintf("       %s\n", dwarf_errmsg(error));
    }
    DEALLOC_ERR(error);
    dwarf_debug = NULL;
    return -1;
  }

  kassert(dwarf_debug != NULL);
  return 0;
}

int dwarf_collect_debug_info() {
  int result;
  Dwarf_Error error = 0;
  // ----- aranges -----
  if (dwarf_get_aranges(dwarf_debug, &dwarf_aranges, &dwarf_aranges_count, &error) != DW_DLV_OK) {
    kprintf("dwarf: failed to get aranges\n");
    kprintf("       %s\n", dwarf_errmsg(error));
    DEALLOC_ERR(error);
    return -1;
  }
  // ----- stack frame info -----
  result = dwarf_get_fde_list_eh(dwarf_debug, &dwarf_cie_list, &dwarf_cie_count, &dwarf_fde_list, &dwarf_fde_count, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get fde list\n");
    kprintf("       %s\n", dwarf_errmsg(error));
    DEALLOC_ERR(error);
    return -1;
  }

  //
  // setup special frame registers
  // https://www.prevanders.net/libdwarfdoc/index.html#frameregs

  dwarf_set_frame_undefined_value(dwarf_debug, DW_FRAME_UNDEFINED_VAL);
  dwarf_set_frame_same_value(dwarf_debug, DW_FRAME_SAME_VAL);

  dwarf_set_frame_cfa_value(dwarf_debug, DW_FRAME_CFA_COL);
  dwarf_set_frame_rule_initial_value(dwarf_debug, DW_FRAME_UNDEFINED_VAL);
  dwarf_set_frame_rule_table_size(dwarf_debug, DW_FRAME_LAST_REG_NUM);

  // -------------------
  return 0;
}

//

int dwarf_debug_load_files(dwarf_file_t **out_file) {
  int result;
  Dwarf_Error error = 0;
  Dwarf_Bool is_info = true;
  Dwarf_Die die = NULL;
  Dwarf_Addr addr_lo = 0;
  Dwarf_Addr addr_hi = 0;
  Dwarf_Half version = 0;
  Dwarf_Off off = 0;
  char *name = NULL;
  LIST_HEAD(dwarf_file_t) files = {0};

  // iterate through the headers and locate each compilation unit (cu)
  while (true) {
    die = NULL;
    result = dwarf_next_cu_header_d(dwarf_debug, is_info,
                                    NULL, &version, NULL, NULL, NULL,
                                    NULL, NULL, NULL, NULL, NULL,
                                    &error);
    if (result != DW_DLV_OK) {
      break;
    }

    // get the cu die
    result = dwarf_find_die_in_siblings(NULL, DW_TAG_compile_unit, false, &die, &error);
    if (result != DW_DLV_OK)
      goto NEXT;

    // load information
    if ((result = dwarf_CU_dieoffset_given_die(die, &off, &error)) != DW_DLV_OK) goto NEXT;
    if ((result = dwarf_attrval_string(die, DW_AT_name, &name, &error)) != DW_DLV_OK) goto NEXT;
    if ((result = dwarf_die_pc_range(die, &addr_lo, &addr_hi, &error)) != DW_DLV_OK) goto NEXT;

    // kprintf("dwarf:     %s\n", name);
    // kprintf("dwarf:     %016p - %016p\n", addr_lo, addr_hi);

    dwarf_file_t *file = kmalloc(sizeof(dwarf_file_t));
    file->name = name; // non-owning
    file->addr_lo = addr_lo;
    file->addr_hi = addr_hi;
    file->die_off = off;
    file->version = version;
    LIST_ENTRY_INIT(&file->list);

    file->lines = NULL;
    file->line_count = 0;
    file->functions = NULL;

    LIST_ADD(&files, file, list);

  LABEL(NEXT);
    if (die != NULL)
      DEALLOC_DIE(die);

    if (result == DW_DLV_ERROR)
      break;
  }


  if (result == DW_DLV_ERROR) {
    kprintf("dwarf: failed to load files\n");
    kprintf("        %s\n", dwarf_errmsg(error));
    DEALLOC_DIE(error);
    return -1;
  }
  *out_file = LIST_FIRST(&files);
  return 0;
}


int dwarf_file_load_lines(dwarf_file_t *file) {
  int result;
  Dwarf_Error error = 0;
  Dwarf_Bool is_info = true;
  Dwarf_Unsigned version = 0;
  Dwarf_Small table_type = 0;
  Dwarf_Line_Context line_ctx = NULL;
  Dwarf_Signed base_index = 0;
  Dwarf_Signed end_index = 0;
  Dwarf_Signed file_count = 0;
  Dwarf_Signed dir_count = 0;
  Dwarf_Die die = NULL;
  dwarf_line_t *lines = NULL;

  // load the cu die from the file offset
  result = dwarf_offdie_b(dwarf_debug, file->die_off, is_info, &die, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: dwarf_file_load_lines() called with invalid file\n");
    return -1;
  }

  result = dwarf_srclines_b(die, &version, &table_type, &line_ctx, &error);
  if (result != DW_DLV_OK) {
    DEALLOC_DIE(die);
    if (result == DW_DLV_ERROR) {
      kprintf("dwarf: dwarf_srclines_b() failed\n");
      kprintf("       %s", dwarf_errmsg(error));
      DEALLOC_ERR(error);
    }
    return result;
  }

  // kprintf("dwarf: version = %d\n", version);
  // kprintf("       table type = %d\n", table_type);

  // table type:
  //   type 0 - no source lines just file names
  //   type 1 - includes source lines and file names
  //   type 2 - experimental two-level table
  if (table_type != 1) {
    // no source line info in this cu
    kprintf("dwarf: line table type not supported\n");
    dwarf_srclines_dealloc_b(line_ctx);
    return DW_DLV_NO_ENTRY;
  }

  // get file count and index bounds
  result = dwarf_srclines_files_indexes(line_ctx, &base_index, &file_count, &end_index, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: dwarf_srclines_files_indexes() failed\n");
    goto FAIL;
  }
  // get directory count
  result = dwarf_srclines_include_dir_count(line_ctx, &dir_count, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: dwarf_srclines_include_dir_count() failed\n");
    goto FAIL;
  }

  // kprintf("       base_index = %d\n", base_index);
  // kprintf("       end_index = %d\n", end_index);
  // kprintf("       file_count = %d\n", file_count);
  // kprintf("       dir_count = %d\n", dir_count);

  Dwarf_Unsigned dir_index;
  const char *file_name;
  const char *dir_name;

  CHECK_ERROR(dwarf_srclines_files_data_b(line_ctx, base_index, &file_name, &dir_index, NULL, NULL, NULL, &error), FAIL);
  CHECK_ERROR(dwarf_srclines_include_dir_data(line_ctx, dir_index, &dir_name, &error), FAIL);

  // kprintf("\n");
  // kprintf("       name = %s/%s\n", dir_name, file_name);

  Dwarf_Line *dwarf_lines = 0;
  Dwarf_Signed line_count = 0;

  result = dwarf_srclines_from_linecontext(line_ctx, &dwarf_lines, &line_count, &error);
  if (result != DW_DLV_OK) goto FAIL;

  lines = kmalloc(line_count * sizeof(dwarf_line_t));
  // kprintf("       line_count = %d\n", line_count);

  // char *full_name = kasprintf("%s/%s", dir_name, file_name);
  // source_file_t *file = alloc_source_file(full_name, line_count, lopc, hipc);
  for (Dwarf_Signed i = 0; i < line_count; i++) {
    Dwarf_Addr line_addr;
    Dwarf_Unsigned line_no;
    Dwarf_Unsigned column_no;
    Dwarf_Unsigned file_num;
    char *src_file_name;

    CHECK_ERROR(dwarf_lineaddr(dwarf_lines[i], &line_addr, &error), FAIL);
    CHECK_ERROR(dwarf_lineno(dwarf_lines[i], &line_no, &error), FAIL);
    CHECK_ERROR(dwarf_lineoff_b(dwarf_lines[i], &column_no, &error), FAIL);
    CHECK_ERROR(dwarf_linesrc(dwarf_lines[i], &src_file_name, &error), FAIL);
    CHECK_ERROR(dwarf_line_srcfileno(dwarf_lines[i], &file_num, &error), FAIL);

    // kprintf("    %s:%d:%d\t%p\n", src_file_name, line_no, column_no, line_addr);

    dwarf_line_t *line = &lines[i];
    line->line_no = line_no;
    line->column_no = column_no;
    line->addr = line_addr;

    DEALLOC_STR(src_file_name);
  }

  file->lines = lines;
  file->line_count = line_count;
  dwarf_srclines_dealloc_b(line_ctx);
  return DW_DLV_OK;

LABEL(FAIL);
  kfree(lines);
  dwarf_srclines_dealloc_b(line_ctx);
  kprintf("dwarf: failed to get source lines\n");
  kprintf("       %s\n", dwarf_errmsg(error));
  DEALLOC_ERR(error);
  return result;
}

int dwarf_file_load_funcs(dwarf_file_t *file) {
  int result;
  Dwarf_Error error = 0;
  Dwarf_Bool is_info = true;
  Dwarf_Die die = NULL;
  Dwarf_Die child = NULL;
  Dwarf_Addr addr_lo = 0;
  Dwarf_Addr addr_hi = 0;
  Dwarf_Off off = 0;
  char *name = NULL;
  LIST_HEAD(dwarf_function_t) functions = {0};

  // load the cu die from the file offset
  result = dwarf_offdie_b(dwarf_debug, file->die_off, is_info, &die, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: dwarf_file_load_funcs() called with invalid file\n");
    return -1;
  }

  // iterate through all function dies
  while (true) {
    result = dwarf_find_die_in_children(die, child, DW_TAG_subprogram, &child, &error);
    if (result != DW_DLV_OK)
      break;

    // load function name
    if ((result = dwarf_dieoffset(child, &off, &error)) != DW_DLV_OK) goto NEXT;
    if ((result = dwarf_attrval_string(child, DW_AT_name, &name, &error)) != DW_DLV_OK) goto NEXT;
    if ((result = dwarf_die_pc_range(child, &addr_lo, &addr_hi, &error)) != DW_DLV_OK) {
      if (result == DW_DLV_NO_ENTRY) {
        // skip it
        continue;
      }
      goto NEXT;
    }

    // kprintf("dwarf: function = %s [%016p - %016p]\n", name, addr_lo, addr_hi);

    dwarf_function_t *func = kmalloc(sizeof(dwarf_function_t));
    func->name = name;
    func->addr_lo = addr_lo;
    func->addr_hi = addr_hi;
    func->die_off = off;
    func->line_start = NULL;
    func->line_end = NULL;
    func->next = NULL;
    // add in reverse order so we end up with a list
    // in increasing order of addr_lo
    SLIST_ADD_FRONT(&functions, func, next);

  LABEL(NEXT);
    if (result != DW_DLV_OK)
      break;
  }

  DEALLOC_DIE(die);
  DEALLOC_DIE(child);
  if (result != DW_DLV_OK) {
    if (result == DW_DLV_ERROR) {
      FREE_FUNC_LIST(LIST_FIRST(&functions));
      kprintf("dwarf: failed to load files\n");
      kprintf("        %s\n", dwarf_errmsg(error));
      DEALLOC_ERR(error);
      return -1;
    }
    // we just hit the end of the list
  }

  // link functions to lines if lines are loaded
  if (file->lines) {
    dwarf_function_t *func = LIST_FIRST(&functions);
    for (size_t i = 0; i < file->line_count; i++) {
      LABEL(try_again);
      dwarf_line_t *line = &file->lines[i];

      if (func) {
        if (line->addr == func->addr_lo) {
          func->line_start = line;
        } else if (line->addr >= func->addr_hi) {
          kassert(i > 0);
          func->line_end = &file->lines[i - 1];
          func->file = file;
          func = func->next;
          goto try_again;
        }
      }
    }
  }

  file->functions = LIST_FIRST(&functions);
  return 0;
}

void dwarf_free_file(dwarf_file_t *file) {
  if (file == NULL)
    return;

  // free lines (if loaded)
  kfree(file->lines);
  file->lines = NULL;
  // free functions (if loaded)
  FREE_FUNC_LIST(file->functions);
  kfree(file);
}

//

// keeping this here because I want to implement dwarf based backtraces
int dwarf_function_get_frame(dwarf_function_t *func) {
  int result;
  Dwarf_Error error = 0;
  Dwarf_Bool is_info = true;
  Dwarf_Die die = NULL;

  // load the die from the function offset
  result = dwarf_offdie_b(dwarf_debug, func->die_off, is_info, &die, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: dwarf_file_load_funcs() called with invalid file\n");
    return -1;
  }

  Dwarf_Fde fde;
  Dwarf_Addr addr_lo;
  Dwarf_Addr addr_hi;
  result = dwarf_get_fde_at_pc(dwarf_fde_list, func->addr_lo, &fde, &addr_lo, &addr_hi, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get fde for function %s\n", func->name);
    goto FAIL;
  }

  Dwarf_Unsigned expr_len = 0;
  Dwarf_Ptr expr_ptr = NULL;
  result = dwarf_attrval_exprloc(die, DW_AT_frame_base, &expr_len, &expr_ptr, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get frame base\n");
    goto FAIL;
  }

  // ====================
  // CFA REGISTERS
  // ====================
  Dwarf_Small value_type = 0;
  Dwarf_Unsigned off_relevant = 0;
  Dwarf_Unsigned reg = 0;
  Dwarf_Unsigned offset = 0;
  Dwarf_Block block_content = {0};
  Dwarf_Addr row_pc_out = 0;
  Dwarf_Bool has_more_rows = 0;
  Dwarf_Addr subsq_pc = 0;
  result = dwarf_get_fde_info_for_cfa_reg3_b(fde, func->addr_lo, &value_type, &off_relevant,
                                             &reg, &offset, &block_content, &row_pc_out,
                                             &has_more_rows, &subsq_pc, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get cfa register info for fde\n");
    goto FAIL;
  }

  const char *value_name = NULL;
  dwarf_get_FORM_name(value_type, &value_name);

  kprintf("       CFA register\n");
  kprintf("         value type = %d\n", value_name);
  kprintf("         off relevant = %u\n", off_relevant);
  kprintf("         reg = %u\n", reg);
  kprintf("         offset = %u\n", offset);
  kprintf("         block content = %p\n", block_content.bl_data);
  kprintf("         block length = %u\n", block_content.bl_len);
  kprintf("         row pc out = %p\n", row_pc_out);
  kprintf("         has more rows = %d\n", has_more_rows);
  kprintf("         subsq pc = %p\n", subsq_pc);
  kprintf("\n");

  // ======= fde =======
  Dwarf_Addr fde_addr_lo;
  Dwarf_Unsigned fde_func_len;
  Dwarf_Small *fde_bytes;
  Dwarf_Unsigned fde_byte_len;
  Dwarf_Off cie_off;
  Dwarf_Signed cie_index;
  Dwarf_Off fde_off;
  result = dwarf_get_fde_range(fde, &fde_addr_lo, &fde_func_len, &fde_bytes,
                               &fde_byte_len, &cie_off, &cie_index, &fde_off, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get fde range for address %p\n", func->addr_lo);
    goto FAIL;
  }

  kprintf("       fde_addr_lo = %p\n", fde_addr_lo);
  kprintf("       fde_func_len = %u\n", fde_func_len);
  kprintf("       fde_bytes = %p\n", fde_bytes);
  kprintf("       fde_byte_len = %u\n", fde_byte_len);
  kprintf("       cie_off = %u\n", cie_off);
  kprintf("       cie_index = %d\n", cie_index);
  kprintf("       fde_off = %u\n", fde_off);

  // ======= cie =======
  Dwarf_Cie cie;
  result = dwarf_get_cie_of_fde(fde, &cie, &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get cie for address %p\n", func->addr_lo);
    goto FAIL;
  }

  Dwarf_Unsigned bytes_in_cie = 0;
  Dwarf_Small cie_version = 0;
  char *augmenter;
  Dwarf_Unsigned code_alignment_factor = 0;
  Dwarf_Signed data_alignment_factor = 0;
  Dwarf_Half return_address_register_rule;
  Dwarf_Small *initial_instructions = 0;
  Dwarf_Unsigned initial_instructions_length = 0;
  Dwarf_Half offset_size = 0;

  result = dwarf_get_cie_info_b(cie, &bytes_in_cie, &cie_version, &augmenter, &code_alignment_factor, &data_alignment_factor,
                                &return_address_register_rule, &initial_instructions, &initial_instructions_length, &offset_size,
                                &error);
  if (result != DW_DLV_OK) {
    kprintf("dwarf: failed to get cie info\n");
    goto FAIL;
  }

  kprintf("\n");
  kprintf("       cie version = %d\n", cie_version);
  kprintf("       cie augmenter = %s\n", augmenter);
  kprintf("       cie code alignment factor = %u\n", code_alignment_factor);
  kprintf("       cie data alignment factor = %d\n", data_alignment_factor);
  kprintf("       cie return address register rule = %d\n", return_address_register_rule);
  kprintf("       cie initial instructions = %p\n", initial_instructions);
  kprintf("       cie initial instructions length = %d\n", initial_instructions_length);
  kprintf("       cie offset size = %d\n", offset_size);
  kprintf("\n");

  Dwarf_Half address_size = 8;
  Dwarf_Small dwarf_version = func->file->version;


  return 0;

LABEL(FAIL);
  kprintf("       %s\n", dwarf_errmsg(error));
  DEALLOC_ERR(error);
  return 1;
}

//
// MARK: DWARF Library Stubs
//

// libdwarf depends on some standard library functions. to get around this,
// we rewrite some of the symbols to their kernel counterparts using objcopy.
//
// The following functions are required for basic library function:
//     malloc -> kmalloc
//     calloc -> kcalloc
//     free   -> kfree
//     strdup -> strdup (unchanged)
//     strtol -> strtol (unchanged)
//     qsort  -> qsort  (unchanged)
//
// the others are not used in any of the functions we're calling so we remap
// them to a stub to avoid implementing them, as well as to satisfy the linker.

#define LIBDWARF_STUB(funcname) \
  __used void __debug_ ##funcname## _stub() { panic(">> debug stub: " #funcname); }

LIBDWARF_STUB(realloc);
LIBDWARF_STUB(fclose);
LIBDWARF_STUB(getcwd);
LIBDWARF_STUB(do_decompress_zlib);
LIBDWARF_STUB(uncompress);

#undef LIBDWARF_STUB
