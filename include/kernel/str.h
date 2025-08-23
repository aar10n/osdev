//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#define __STR__
#ifndef KERNEL_STR_H
#define KERNEL_STR_H

#include <kernel/base.h>
#include <kernel/string.h>
#include <macros.h>
#include <stdarg.h>

void *kmalloc(size_t size);
void *kmallocz(size_t size);
void kfree(void *);


/**
 * cstr represents a constant fixed-length string.
 */
typedef struct cstr {
  const char *str;
  size_t len;
} cstr_t;

#define cstr_null ((cstr_t){ NULL, 0 })

#define cstr_move(str) ({ \
  cstr_t _str = (str); \
  (str) = cstr_null; \
  _str; \
})

#define cstr_list(...) ((cstr_t[]) { __VA_ARGS__, cstr_null })
#define charp_list(...) ((const char *[]) { __VA_ARGS__, NULL })

#define _charp_to_cstr(charp) cstr_make(charp)
#define cstr_charp_list(...) ((cstr_t[]){ MACRO_MAP_LIST(_charp_to_cstr, __VA_ARGS__), cstr_null })

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

static inline bool cstr_isnull(cstr_t str) {
  return str.str == NULL;
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

static inline bool cstr_eq_charp(cstr_t str1, const char *cstr2) {
  size_t len = strlen(cstr2);
  return cstr_len(str1) == len && strncmp(cstr_ptr(str1), cstr2, len) == 0;
}

static inline size_t cstr_memcpy(cstr_t str, void *buf, size_t len) {
  size_t n = min(cstr_len(str)+1, len);
  memcpy(buf, cstr_ptr(str), n-1);
  ((char *)buf)[n-1] = '\0';
  return n;
}

static inline bool cstr_in_list(cstr_t str, cstr_t list[]) {
  for (size_t i = 0; !cstr_isnull(list[i]); i++) {
    if (cstr_eq(str, list[i])) {
      return true;
    }
  }
  return false;
}

static inline bool cstr_in_charp_list(cstr_t str, const char *list[]) {
  for (size_t i = 0; list[i] != NULL; i++) {
    if (cstr_eq_charp(str, list[i])) {
      return true;
    }
  }
  return false;
}

/**
 * str represents an owned mutable string.
 */
typedef struct str {
  char *str;
  size_t len;
} str_t;

#define str_null ((str_t) { NULL, 0 })

#define str_move(str) ({ \
  str_t _str = (str); \
  (str) = str_null; \
  _str; \
})

static inline bool str_isnull(str_t str) {
  return str.str == NULL;
}

static inline cstr_t cstr_from_str(str_t str) {
  return cstr_new(str.str, str.len);
}

static inline bool cstr_starts_with(cstr_t str, char c) {
  return cstr_len(str) > 0 && cstr_ptr(str)[0] == c;
}

static inline str_t str_alloc_empty(size_t len) {
  char *buf = kmallocz(len + 1);
  return (str_t) {
    .str = buf,
    .len = len,
  };
}

static inline str_t str_new(const char *str, size_t len) {
  if (!str || len == 0)
    return str_null;

  char *buf = kmalloc(len + 1);
  memcpy(buf, str, len);
  buf[len] = '\0';
  return (str_t) {
    .str = buf,
    .len = len,
  };
}

static inline str_t str_from(const char *str) {
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

static inline str_t str_from_charp(char *str) {
  if (!str)
    return str_null;

  size_t len = strlen(str);
  return (str_t) {
    .str = str,
    .len = len,
  };
}

static inline str_t str_from_cstr(cstr_t str) {
  return str_new(str.str, str.len);
}

static inline str_t str_dup(str_t str) {
  if (str_isnull(str))
    return str_null;

  char *buf = kmalloc(str.len + 1);
  memcpy(buf, str.str, str.len);
  buf[str.len] = '\0';
  return (str_t) {
    .str = buf,
    .len = str.len,
  };
}

static inline void str_free(str_t *str) {
  if (str_isnull(*str))
    return;
  kfree(str->str);
  str->str = NULL;
  str->len = 0;
}



static inline const char *str_cptr(str_t str) {
  return str.str;
}

static inline char *str_mut_ptr(str_t str) {
  return str.str;
}

static inline size_t str_len(str_t str) {
  return str.len;
}

static inline char str_get(str_t str, size_t index) {
  return str.str[index];
}

static inline bool str_eq(str_t str1, str_t str2) {
  return str1.len == str2.len && strncmp(str1.str, str2.str, str1.len) == 0;
}

static inline bool str_eq_cstr(str_t str1, cstr_t str2) {
  return str1.len == str2.len && strncmp(str1.str, str2.str, str1.len) == 0;
}

static inline bool str_eq_charp(str_t str1, const char *str2) {
  size_t len = strlen(str2);
  return str_len(str1) == len && strncmp(str_cptr(str1), str2, len) == 0;
}

// TODO: rest of string functions

#endif

#ifdef __PRINTF__
// is available if printf.h is included
#ifndef STR_PRINTF
#define STR_PRINTF
static inline str_t str_fmt(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *s = kvasprintf(format, args);
  va_end(args);
  return str_from_charp(s);
}
#endif
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
  return str_new(path_start(path), path_len(path));
}
#endif
#endif

#ifdef __KIO__
// is available if path.h is included
#ifndef STR_KIO
#define STR_KIO
static inline kio_t kio_readable_from_cstr(cstr_t str) {
  return kio_new_readable(cstr_ptr(str), cstr_len(str));
}

static inline kio_t kio_readonly_from_str(str_t str) {
  return kio_new_readable(str.str, str.len);
}

static inline kio_t kio_writeonly_from_str(str_t str) {
  return kio_new_writable(str.str, str.len);
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

