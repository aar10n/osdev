//
// Created by Aaron Gill-Braun on 2025-06-06.
//

#include <kernel/params.h>

#define DPRINTF(x, ...) kprintf("params: " x, ##__VA_ARGS__)
// #define DPRINTF(x, ...)
#define EPRINTF(x, ...) kprintf("params: error: " x, ##__VA_ARGS__)
// #define EPRINTF(x, ...)

// the kernel_params hashmap stores all of the configurable kernel parameters.
#define HMAP_TYPE struct kernel_param *
#include <hash_map.h>
hash_map_t *kernel_params;

LOAD_SECTION(__kernel_params_section, ".kernel_params");


void handle_cmdline_param(cstr_t key, cstr_t value) {
  struct kernel_param *param = hash_map_get_cstr(kernel_params, key);
  if (param == NULL) {
    EPRINTF("unknown kernel parameter: {:cstr}\n", &key);
    return;
  }

  DPRINTF("parsed kernel parameter: {:cstr} = {:cstr}\n", &key, &value);
  switch (param->type) {
    case KERNEL_STR_PARAM: {
      str_t *cstr_value = (str_t *) param->addr;
      *cstr_value = str_from_cstr(value);
      break;
    }
    case KERNEL_INT_PARAM: {
      int *int_value = (int *) param->addr;
      if (param_parse_int(value, int_value) < 0) {
        EPRINTF("expected integer value for {:cstr}, got \"{:cstr}\"\n", &key, &value);
      }
      break;
    }
    case KERNEL_BOOL_PARAM: {
      bool *bool_value = (bool *) param->addr;
      if (param_parse_bool(value, bool_value) < 0) {
        EPRINTF("expected boolean value for {:cstr}, got \"{:cstr}\"\n", &key, &value);
      }
      break;
    }
    default:
      EPRINTF("unknown kernel parameter type for {:cstr}\n", &key);
      break;
  }
}

void parse_cmdline_params(const char *cmdline, uint32_t len) {
  const char *p = cmdline;
  const char *end = cmdline + len;

  while (p < end) {
    // skip leading whitespace or newlines
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
      p++;
    if (p >= end)
      break;

    // parse key
    const char *key_start = p;
    while (p < end && *p != '=' && *p != ' ' && *p != '\t' && *p != '\n')
      p++;
    const char *key_end = p;

    // skip '='
    while (p < end && (*p == ' ' || *p == '\t'))
      p++;
    if (p < end && *p == '=')
      p++;
    else
      continue;

    while (p < end && (*p == ' ' || *p == '\t'))
      p++;
    if (p >= end)
      break;

    // parse value
    const char *val_start = NULL;
    const char *val_end = NULL;

    if (*p == '"') {
      // quoted value
      p++;
      val_start = p;
      while (p < end && *p != '"')
        p++;
      val_end = p;
      if (p < end)
        p++;
    } else {
      // unquoted value
      val_start = p;
      while (p < end && *p != ' ' && *p != '\t' && *p != '\n')
        p++;
      val_end = p;
    }

    // user implementation here
    cstr_t key = cstr_new(key_start, key_end-key_start);
    cstr_t value = cstr_new(val_start, val_end-val_start);
    handle_cmdline_param(key, value);
  }
}

//

void init_kernel_params() {
  kernel_params = hash_map_new();

  void **params = (void *) __kernel_params_section.virt_addr;
  size_t num_params = __kernel_params_section.size / sizeof(void *);
  for (size_t i = 0; i < num_params; i++) {
    struct kernel_param *param = (struct kernel_param *) params[i];
    DPRINTF("found kernel parameter: %s\n", param->name);
    hash_map_set(kernel_params, param->name, param);
  }

  // parse the boot command line options
  if (boot_info_v2->cmdline != NULL) {
    parse_cmdline_params(boot_info_v2->cmdline, boot_info_v2->cmdline_len);
  }
}


int param_parse_int(cstr_t str, int *out) {
  int value = 0;
  bool negative = false;
  if (cstr_len(str) == 0) {
    return 0; // empty string
  }

  const char *ptr = cstr_ptr(str);
  size_t len = cstr_len(str);
  if (ptr[0] == '-') {
    negative = true;
    ptr++;
    len--;
  }

  for (size_t i = 0; i < len; i++) {
    char c = ptr[i];
    if (c < '0' || c > '9') {
      EPRINTF("invalid integer parameter: {:cstr}\n", &str);
      return -1;
    }
    value = value * 10 + (c - '0');
  }

  if (negative) {
    value = -value;
  }
  *out = value;
  return 0;
}

int param_parse_bool(cstr_t str, bool *out) {
  if (cstr_eq_charp(str, "true") || cstr_eq_charp(str, "y") || cstr_eq_charp(str, "yes") || cstr_eq_charp(str, "1")) {
    *out = true;
    return 0;
  } else if (cstr_eq_charp(str, "false") || cstr_eq_charp(str, "n") || cstr_eq_charp(str, "no") || cstr_eq_charp(str, "0")) {
    *out = false;
    return 0;
  } else {
    EPRINTF("invalid boolean parameter: {:cstr}\n", &str);
    return -1;
  }
}
