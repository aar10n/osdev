//
// Created by Aaron Gill-Braun on 2025-07-26.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>
#include <glib.h>

#define _used __attribute__((used))
#define _unused __attribute__((unused))

QEMU_PLUGIN_EXPORT _used int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define USER_SPACE_END      0x800000000000ULL
#define KERNEL_SPACE_START  0xffff800000000000ULL

// register names
#define REG_RBP_NAME "rbp"  // frame pointer
#define REG_RSP_NAME "rsp"  // stack pointer
#define REG_RIP_NAME "rip"  // instruction pointer
#define REG_CS_NAME "cs"    // code segment (for privilege level)

// register handles
static struct qemu_plugin_register *reg_rbp_handle = NULL;
static struct qemu_plugin_register *reg_rsp_handle = NULL;
static struct qemu_plugin_register *reg_rip_handle = NULL;
static struct qemu_plugin_register *reg_cs_handle = NULL;

typedef struct {
  uint64_t sample_period;      // instructions between samples
  const char *output_file;     // profile output file
  bool kernel_only;            // only profile kernel space
  bool user_only;              // only profile user space
  uint64_t kernel_base;        // kernel/user boundary
  int max_stack_depth;         // maximum stack depth to capture
  bool resolve_symbols;        // attempt symbol resolution
  const char *symbol_prefix;   // prefix for unresolved symbols
  GArray *vcpu_filter;         // array of vCPU IDs to profile (NULL = all)
} Config;

static Config config = {
  .sample_period = 10000,
  .output_file = "profile",
  .kernel_only = false,
  .user_only = false,
  .kernel_base = 0xffff800000100000,
  .max_stack_depth = 64,
  .resolve_symbols = true,
  .symbol_prefix = "func",
  .vcpu_filter = NULL,
};

// per-vCPU profiling state
typedef struct {
  uint64_t insn_count;
  uint64_t last_sample;
  GArray *stack_trace;
  bool registers_initialized;
  GArray *saved_stacks;
} VCPUProfile;

// stack sample entry
typedef struct {
  char *folded_stack;
  uint64_t count;
} StackSample;

// global state
static struct qemu_plugin_scoreboard *profiles;
static GMutex output_lock;
static GHashTable *stack_lookup;   // folded_stack -> index in stack_samples
static GArray *stack_samples;      // ordered array of StackSample
static uint64_t total_samples = 0;
static uint64_t failed_unwinds = 0;

static inline bool is_kernel_address(uint64_t addr) {
  return addr >= config.kernel_base;
}

static inline bool is_valid_stack_addr(uint64_t addr) {
  // basic sanity checks
  if (addr == 0 || (addr & 0x7) != 0 || addr < 0x1000) {
    return false;
  }

  if (is_kernel_address(addr)) {
    return addr >= 0xffff800000000000ULL && addr < 0xfffffffffffff000ULL;
  }
  
  // user stacks
  return addr < 0x800000000000ULL;
}

static inline bool is_valid_code_addr(uint64_t addr) {
  // reject only the most obvious garbage patterns
  if (addr == 0 || addr == 0xffffffffffffffff) return false;
  if (addr == 0xafafafafafafafaf || addr == 0xdeadbeefdeadbeef) return false;
  if (addr == 0x5555555555555555 || addr == 0xaaaaaaaaaaaaaaaa) return false;
  
  // for now, be very permissive, accept any address that looks remotely reasonable

  if (addr >= KERNEL_SPACE_START) {
    return true;
  }

  // dont include non-canonical addresses
  return (addr < USER_SPACE_END && addr >= 0x1000);
}

static inline bool should_profile_vcpu(unsigned int vcpu_idx) {
  if (!config.vcpu_filter) {
    return true;  // profile all vCPUs if no filter specified
  }
  
  for (int i = 0; i < (int)config.vcpu_filter->len; i++) {
    unsigned int filtered_vcpu = g_array_index(config.vcpu_filter, unsigned int, i);
    if (filtered_vcpu == vcpu_idx) {
      return true;
    }
  }
  
  return false;
}

static bool read_guest_u64(unsigned int vcpu_idx __attribute__((unused)), uint64_t vaddr, uint64_t *value) {
  GByteArray *data = g_byte_array_sized_new(8);
  bool success = false;

  if (qemu_plugin_read_memory_vaddr(vaddr, data, 8)) {
    if (data->len == 8) {
      memcpy(value, data->data, 8);
      success = true;
    }
  }

  g_byte_array_free(data, TRUE);
  return success;
}

static int unwind_guest_stack(unsigned int vcpu_idx, GArray *stack, uint64_t initial_pc, uint64_t rbp, uint64_t rsp) {
  VCPUProfile *prof = qemu_plugin_scoreboard_find(profiles, vcpu_idx);
  int depth = 0;
  uint64_t fp = rbp;

  g_array_set_size(stack, 0);

  // add current PC
  if (is_valid_code_addr(initial_pc)) {
    g_array_append_val(stack, initial_pc);
    depth++;
  }

  // x86_64 stack frame layout:
  //   [fp+0] = saved rbp (next frame pointer)
  //   [fp+8] = return address

  // normal frame pointer unwinding
  while (depth < config.max_stack_depth && is_valid_stack_addr(fp)) {
    uint64_t ret_addr, next_fp;

    // read next frame pointer and return address
    if (!read_guest_u64(vcpu_idx, fp, &next_fp)) {
      break;
    }
    if (!read_guest_u64(vcpu_idx, fp + 8, &ret_addr)) {
      break;
    }

    if (!is_valid_code_addr(ret_addr)) {
      break;
    }

    // apply filtering
    if (config.kernel_only && !is_kernel_address(ret_addr)) {
      break;
    }
    if (config.user_only && is_kernel_address(ret_addr)) {
      break;
    }

    g_array_append_val(stack, ret_addr);
    depth++;

    // prevent loops
    if (next_fp <= fp || next_fp == fp || !is_valid_stack_addr(next_fp)) {
      break;
    }

    fp = next_fp;
  }

  return depth;
}

static void format_address(GString *str, uint64_t addr) {
  if (config.resolve_symbols) {
    // add symbol hints based on address ranges
    if (is_kernel_address(addr)) {
      g_string_append_printf(str, "kernel`%s+0x%" PRIx64,
                             config.symbol_prefix, addr);
    } else {
      g_string_append_printf(str, "user`%s+0x%" PRIx64,
                             config.symbol_prefix, addr);
    }
  } else {
    g_string_append_printf(str, "0x%" PRIx64, addr);
  }
}

static char *create_folded_stack(GArray *stack) {
  if (stack->len == 0) return NULL;

  GString *folded = g_string_new(NULL);

  // build from root (last element) to leaf (first element)
  for (int i = stack->len - 1; i >= 0; i--) {
    if (i < (int)stack->len - 1) {
      g_string_append_c(folded, ';');
    }

    uint64_t pc = g_array_index(stack, uint64_t, i);
    format_address(folded, pc);
  }

  return g_string_free(folded, FALSE);
}

static void record_stack_sample(GArray *stack) {
  char *folded = create_folded_stack(stack);
  if (!folded) return;

  g_mutex_lock(&output_lock);

  gpointer index_ptr = g_hash_table_lookup(stack_lookup, folded);
  if (index_ptr) {
    // existing stack - increment count
    size_t index = GPOINTER_TO_SIZE(index_ptr);
    StackSample *sample = &g_array_index(stack_samples, StackSample, index);
    sample->count++;
    g_free(folded);
  } else {
    // new stack - add to ordered array
    StackSample new_sample = {
      .folded_stack = folded,
      .count = 1
    };
    g_array_append_val(stack_samples, new_sample);
    
    // record index in lookup table
    size_t index = stack_samples->len - 1;
    g_hash_table_insert(stack_lookup, g_strdup(folded), GSIZE_TO_POINTER(index));
  }

  total_samples++;

  g_mutex_unlock(&output_lock);
}

// vcpu initialization callback
static void vcpu_init(_unused qemu_plugin_id_t id, unsigned int vcpu_idx) {
  VCPUProfile *prof = qemu_plugin_scoreboard_find(profiles, vcpu_idx);
  if (should_profile_vcpu(vcpu_idx)) {
    fprintf(stderr, "Profiling vCPU %u\n", vcpu_idx);
  } else {
    // not profiling this vCPU, skip initialization
    return;
  }

  // initialize the stack trace array if not already done
  if (!prof->stack_trace) {
    prof->stack_trace = g_array_new(FALSE, FALSE, sizeof(uint64_t));
  }
  
  // initialize saved stacks array for interrupt/syscall tracking
  if (!prof->saved_stacks) {
    prof->saved_stacks = g_array_new(FALSE, FALSE, sizeof(GArray*));
  }
  
  if (prof->registers_initialized) {
    return;
  }
  
  // get register list
  GArray *regs = qemu_plugin_get_registers();
  if (!regs) {
    return;
  }
  
  // find register handles
  for (int i = 0; i < (int)regs->len; i++) {
    qemu_plugin_reg_descriptor *desc = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
    if (strcmp(desc->name, REG_RBP_NAME) == 0) {
      reg_rbp_handle = desc->handle;
    } else if (strcmp(desc->name, REG_RSP_NAME) == 0) {
      reg_rsp_handle = desc->handle;
    } else if (strcmp(desc->name, REG_RIP_NAME) == 0) {
      reg_rip_handle = desc->handle;
    } else if (strcmp(desc->name, REG_CS_NAME) == 0) {
      reg_cs_handle = desc->handle;
    }
  }
  
  g_array_free(regs, TRUE);
  prof->registers_initialized = true;
}

// instruction execution with register access callback
static void sample_vcpu_stack(unsigned int vcpu_idx, void *userdata) {
  VCPUProfile *prof = qemu_plugin_scoreboard_find(profiles, vcpu_idx);
  uint64_t pc = (uint64_t)userdata;  // TB PC passed as userdata

  prof->insn_count++;

  // check if this vCPU should be profiled
  if (!should_profile_vcpu(vcpu_idx)) {
    return;
  }

  // check if it's time to sample
  if (prof->insn_count - prof->last_sample < config.sample_period) {
    return;
  }

  prof->last_sample = prof->insn_count;

  // apply basic filtering
  if (config.kernel_only && !is_kernel_address(pc)) {
    return;
  }
  if (config.user_only && is_kernel_address(pc)) {
    return;
  }

  // get frame pointer
  if (!reg_rbp_handle) {
    return;
  }
  
  GByteArray *reg_buf = g_byte_array_sized_new(8);
  uint64_t rbp = 0;

  int reg_count = qemu_plugin_read_register(reg_rbp_handle, reg_buf);
  if (reg_count == sizeof(uint64_t)) {
    memcpy(&rbp, reg_buf->data, sizeof(uint64_t));
  }

  g_byte_array_free(reg_buf, TRUE);

  // get stack pointer for enhanced unwinding
  uint64_t rsp = 0;
  if (reg_rsp_handle) {
    GByteArray *rsp_buf = g_byte_array_sized_new(8);
    if (qemu_plugin_read_register(reg_rsp_handle, rsp_buf) == sizeof(uint64_t)) {
      memcpy(&rsp, rsp_buf->data, sizeof(uint64_t));
    }
    g_byte_array_free(rsp_buf, TRUE);
  }

  // unwind the stack with enhanced tracking
  int depth = unwind_guest_stack(vcpu_idx, prof->stack_trace, pc, rbp, rsp);

  if (depth > 0) {
    record_stack_sample(prof->stack_trace);
  } else {
    failed_unwinds++;
  }
}

// translation block callback, called for each TB (translation block)
static void vcpu_tb_trans(qemu_plugin_id_t id __attribute__((unused)), struct qemu_plugin_tb *tb) {
  size_t n_insns = qemu_plugin_tb_n_insns(tb);
  if (n_insns == 0) return;

  // get TB starting address
  struct qemu_plugin_insn *first_insn = qemu_plugin_tb_get_insn(tb, 0);
  uint64_t tb_pc = qemu_plugin_insn_vaddr(first_insn);

  // instrument every instruction for counting, but only first for sampling
  for (size_t i = 0; i < n_insns; i++) {
    struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

    if (i == 0) {
      // sample with register access on first instruction
      qemu_plugin_register_vcpu_insn_exec_cb(
        insn, sample_vcpu_stack, QEMU_PLUGIN_CB_R_REGS, (void *)tb_pc);
    } else {
      // just count other instructions
      qemu_plugin_u64 insn_count_u64 = qemu_plugin_scoreboard_u64_in_struct(
          profiles, VCPUProfile, insn_count);
      qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        insn, QEMU_PLUGIN_INLINE_ADD_U64,
        insn_count_u64, 1);
    }
  }
}

static void write_profile_data(void) {
  FILE *fp = fopen(config.output_file, "w");
  if (!fp) {
    fprintf(stderr, "Error: Cannot open output file %s\n", config.output_file);
    return;
  }

  g_mutex_lock(&output_lock);

  // write stacks in the order they were first captured
  for (size_t i = 0; i < stack_samples->len; i++) {
    StackSample *sample = &g_array_index(stack_samples, StackSample, i);
    fprintf(fp, "%s %" PRIu64 "\n", sample->folded_stack, sample->count);
  }

  g_mutex_unlock(&output_lock);

  fclose(fp);

  // print statistics
  fprintf(stderr, "\nProfiling complete:\n");
  fprintf(stderr, "  Output file: %s\n", config.output_file);
  fprintf(stderr, "  Total samples: %" PRIu64 "\n", total_samples);
  fprintf(stderr, "  Unique stacks: %u\n", (unsigned int)stack_samples->len);
  fprintf(stderr, "  Failed unwinds: %" PRIu64 "\n", failed_unwinds);
}

// exit callback, called when the plugin is unloaded
static void plugin_exit(qemu_plugin_id_t id __attribute__((unused)), void *p __attribute__((unused))) {
  write_profile_data();
  
  // cleanup data structures
  for (size_t i = 0; i < stack_samples->len; i++) {
    StackSample *sample = &g_array_index(stack_samples, StackSample, i);
    g_free(sample->folded_stack);
  }
  g_array_free(stack_samples, TRUE);
  g_hash_table_destroy(stack_lookup);
}

// parse plugin arguments
static void parse_arguments(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    char *arg = argv[i];

    if (g_str_has_prefix(arg, "period=")) {
      config.sample_period = g_ascii_strtoull(arg + 7, NULL, 10);
    }
    else if (g_str_has_prefix(arg, "output=")) {
      config.output_file = g_strdup(arg + 7);
    }
    else if (strcmp(arg, "kernel") == 0 || strcmp(arg, "kernel=on") == 0 || strcmp(arg, "kernel=true") == 0) {
      config.kernel_only = true;
    }
    else if (strcmp(arg, "user") == 0 || strcmp(arg, "user=on") == 0 || strcmp(arg, "user=true") == 0) {
      config.user_only = true;
    }
    else if (g_str_has_prefix(arg, "depth=")) {
      config.max_stack_depth = (int) g_ascii_strtoull(arg + 6, NULL, 10);
    }
    else if (strcmp(arg, "nosymbols") == 0) {
      config.resolve_symbols = false;
    }
    else if (g_str_has_prefix(arg, "vcpus=")) {
      // parse colon-separated list of vCPU IDs
      const char *vcpu_list = arg + 6;
      
      // check if the list is empty
      if (strlen(vcpu_list) == 0) {
        // empty list means no filter
        if (config.vcpu_filter) {
          g_array_free(config.vcpu_filter, TRUE);
        }
        config.vcpu_filter = NULL;
      } else {
        config.vcpu_filter = g_array_new(FALSE, FALSE, sizeof(unsigned int));

        size_t filter_size = 0;
        gchar **vcpu_strs = g_strsplit(vcpu_list, ":", -1);
        for (int j = 0; vcpu_strs[j] != NULL; j++) {
          // skip empty strings from splitting
          if (strlen(vcpu_strs[j]) > 0) {
            unsigned int vcpu_id = (unsigned int)g_ascii_strtoull(vcpu_strs[j], NULL, 10);
            g_array_append_val(config.vcpu_filter, vcpu_id);
            filter_size++;
          }
        }

        if (filter_size == 0) {
          // clear filter if no valid vCPU IDs were found
          g_array_free(config.vcpu_filter, TRUE);
          config.vcpu_filter = NULL;
        }
        g_strfreev(vcpu_strs);
      }
    }
    else if (g_str_has_prefix(arg, "help")) {
      fprintf(stderr, "QEMU Profiling Plugin Options:\n");
      fprintf(stderr, "  period=N       - Sample every N instructions (default: 10000)\n");
      fprintf(stderr, "  output=FILE    - Output file (default: profile.folded)\n");
      fprintf(stderr, "  kernel         - Profile kernel code only\n");
      fprintf(stderr, "  user           - Profile user code only\n");
      fprintf(stderr, "  depth=N        - Maximum stack depth (default: 64)\n");
      fprintf(stderr, "  nosymbols      - Don't add symbol prefixes\n");
      fprintf(stderr, "  vcpus=N:M:...  - Profile only specified vCPUs (colon-separated, empty=all)\n");
      exit(0);
    }
  }
}

// plugin initialization function
QEMU_PLUGIN_EXPORT _used int qemu_plugin_install(qemu_plugin_id_t id, _unused const qemu_info_t *info, int argc, char **argv) {
  parse_arguments(argc, argv);

  fprintf(stderr, "QEMU Profiling Plugin\n");
  fprintf(stderr, "  Sampling period: %" PRIu64 " instructions\n", config.sample_period);
  fprintf(stderr, "  Max stack depth: %d\n", config.max_stack_depth);
  fprintf(stderr, "  Output file: %s\n", config.output_file);
  if (config.kernel_only) {
    fprintf(stderr, "  Profiling: Kernel code only\n");
  } else if (config.user_only) {
    fprintf(stderr, "  Profiling: User code only\n");
  } else {
    fprintf(stderr, "  Profiling: Both kernel and user code\n");
  }
  
  if (config.vcpu_filter) {
    fprintf(stderr, "  Profiling vCPUs: ");
    for (int i = 0; i < (int)config.vcpu_filter->len; i++) {
      unsigned int vcpu_id = g_array_index(config.vcpu_filter, unsigned int, i);
      fprintf(stderr, "%u", vcpu_id);
      if (i < (int)config.vcpu_filter->len - 1) {
        fprintf(stderr, ":");
      }
    }
    fprintf(stderr, "\n");
  } else {
    fprintf(stderr, "  Profiling: All vCPUs\n");
  }

  // initialize data structures
  g_mutex_init(&output_lock);
  stack_lookup = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  stack_samples = g_array_new(FALSE, FALSE, sizeof(StackSample));

  // allocate per-vcpu profiling state
  profiles = qemu_plugin_scoreboard_new(sizeof(VCPUProfile));

  // register callbacks
  qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
  qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
  qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

  return 0;
}
