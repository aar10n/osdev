//
// Created by Aaron Gill-Braun on 2025-06-06.
//

#ifndef KERNEL_PARAMS_H
#define KERNEL_PARAMS_H

#include <kernel/base.h>
#include <kernel/str.h>

#define PARAM_NAME_MAX 64 /* maximum length of a kernel parameter name */

#define KERNEL_PARAM(_name, _type, _var, _default) \
  _Static_assert( \
    ARRAY_SIZE(_name) <= PARAM_NAME_MAX, \
    "Kernel parameter name too long (> " __param_tostring(PARAM_NAME_MAX) ")" \
  ); \
  _type _var = _default; \
  static _used struct kernel_param __param_ ## _var = { \
    .name = (_name), \
    .addr = &(_var), \
    .type = _Generic(_var, \
      str_t: KERNEL_STR_PARAM, \
      int: KERNEL_INT_PARAM, \
      bool: KERNEL_BOOL_PARAM \
    ) \
  }; \
  static _used __attribute__((section(".kernel_params"))) void * __param_ptr_ ## _var = &__param_ ## _var; \

// kernel_param types
#define KERNEL_STR_PARAM   1 /* str_t */
#define KERNEL_INT_PARAM   2 /* int */
#define KERNEL_BOOL_PARAM  3 /* bool */

struct kernel_param {
  const char *name;
  void *addr;
  int type;
};

void init_kernel_params();
int param_parse_int(cstr_t str, int *out);
int param_parse_bool(cstr_t str, bool *out);

#define __param_stringify(x) #x
#define __param_tostring(x) __param_stringify(x)

#endif
