//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#define __STR__
#ifndef KERNEL_STR_H
#define KERNEL_STR_H

#include <base.h>
#include <string.h>
#include <mm/heap.h>

/**
 * cstr represents a constant fixed-length string.
 */
typedef struct cstr {
  const char *str;
  size_t len;
} cstr_t;

#define cstr_null ((cstr_t){ NULL, 0 })

static inline cstr_t cstr_new(const char *str, size_t len) {
  if (!str)
    return cstr_null;

  return (cstr_t) {
    .str = str,
    .len = len,
  };
}

static inline cstr_t cstr_make(const char *str) {
  if (!str)
    return cstr_null;
  return cstr_new(str, strlen(str));
}

static inline const char *cstr_ptr(cstr_t str) {
  return str.str;
}

static inline size_t cstr_len(cstr_t str) {
  return str.len;
}

static inline bool cstr_cmp(cstr_t str1, cstr_t str2) {
  size_t n = min(cstr_len(str1), cstr_len(str2));
  return strncmp(cstr_ptr(str1), cstr_ptr(str2), n);
}

static inline bool cstr_eq(cstr_t str1, cstr_t str2) {
  return cstr_len(str1) == cstr_len(str2) && cstr_cmp(str1, str2) == 0;
}

/**
 * str represents an owned mutable string.
 */
typedef struct str {
  char *str;
  size_t len;
} str_t;

#define str_null ((str_t) { NULL, 0 })

static inline cstr_t cstr_from_str(str_t str) {
  return cstr_new(str.str, str.len);
}

static inline str_t str_alloc(size_t len) {
  char *buf = kmalloc(len + 1);
  buf[len] = '\0';
  return (str_t) {
    .str = buf,
    .len = len,
  };
}

static inline str_t str_new(const char *str) {
  if (!str)
    return str_null;

  size_t len = strlen(str);
  char *buf = kmalloc(len + 1);
  memcpy(buf, str, len);
  buf[len] = '\0';
  return (str_t) {
    .str = buf,
    .len = len,
  };
}

static inline str_t str_make(const char *str, size_t len) {
  if (!str)
    return str_null;

  char *buf = kmalloc(len + 1);
  memcpy(buf, str, len);
  buf[len] = '\0';
  return (str_t) {
    .str = buf,
    .len = len,
  };
}

static inline str_t str_copy_cstr(cstr_t str) {
  return str_make(str.str, str.len);
}

static inline void str_free(str_t *str) {
  kfree(str->str);
  str->str = NULL;
  str->len = 0;
}

static inline bool str_isnull(str_t str) {
  return str.str == NULL;
}

static inline const char *str_cptr(str_t str) {
  return str.str;
}

static inline size_t str_len(str_t str) {
  return str.len;
}

static inline char str_get(str_t str, size_t index) {
  return str.str[index];
}

static inline bool str_eq_c(str_t str1, cstr_t str2) {
  return strcmp(str1.str, str2.str) == 0;
}

// TODO: rest of string functions

#endif

#ifdef __PATH__
// is available if str.h is included
#ifndef STR_PATH
#define STR_PATH
static inline cstr_t cstr_from_path(path_t path) {
  return cstr_new(path_start(path), path_len(path));
}

static inline path_t path_from_cstr(cstr_t str) {
  return path_new(str.str, str.len);
}

static inline cstr_t cstr_basename(cstr_t str) {
  path_t path = path_from_cstr(str);
  return cstr_from_path(path_basename(path));
}

static inline cstr_t cstr_dirname(cstr_t str) {
  path_t path = path_from_cstr(str);
  return cstr_from_path(path_dirname(path));
}


static inline path_t path_from_str(str_t str) {
  return path_new(str.str, str.len);
}

static inline str_t str_from_path(path_t path) {
  return str_make(path_start(path), path_len(path));
}
#endif
#endif

#ifdef __KIO__
// is available if path.h is included
#ifndef STR_KIO
#define STR_KIO
static inline kio_t kio_readonly_from_cstr(cstr_t str) {
  return kio_new_readonly(cstr_ptr(str), cstr_len(str));
}

static inline kio_t kio_readonly_from_str(str_t str) {
  return kio_new_readonly(str.str, str.len);
}

static inline kio_t kio_writeonly_from_str(str_t str) {
  return kio_new_writeonly(str.str, str.len);
}
#endif
#endif

#ifdef __SBUF__
// is available if sbuf.h is included
#ifndef STR_SBUF
#define STR_SBUF
static inline cstr_t cstr_from_sbuf(sbuf_t *buf) {
  return cstr_new(sbuf_cptr(buf), sbuf_len(buf));
}
#endif
#endif

