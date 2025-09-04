//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#include <kernel/vfs/path.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/string.h>


#define ASSERT(x) kassert(x)


// MARK: Path API

path_t path_make(const char *str) {
  if (str == NULL) {
    return NULL_PATH;
  }

  size_t len = strlen(str);
  ASSERT(len <= UINT16_MAX);
  return path_new(str, len);
}

path_t path_new(const char *str, size_t len) {
  if (str == NULL || len == 0) {
    return NULL_PATH;
  } else if (len > MAX_PATH_LEN) {
    len = MAX_PATH_LEN;
  }

  return (path_t){
    .storage = { .str = str, .len = len },
    .view = { .off = 0, .len = len },
  };
}

char *path2str(path_t path) {
  if (path_is_null(path)) {
    return NULL;
  }

  size_t len = path_len(path);
  char *str = kmalloc(len + 1);
  memcpy(str, path_start(path), len);
  str[len] = '\0';
  return str;
}

size_t path_copy(char *dest, size_t size, path_t path) {
  if (path_is_null(path) || size == 0) {
    return 0;
  }

  size_t len = min(size - 1, path_len(path));
  memcpy(dest, path_start(path), len);
  dest[len] = '\0';
  return len;
}

//

bool path_eq(path_t path1, path_t path2) {
  if (path_len(path1) != path_len(path2)) {
    return false;
  }
  return memcmp(path_start(path1), path_start(path2), path_len(path1)) == 0;
}

bool path_eq_charp(path_t path, const char *str) {
  if (path_is_null(path)) {
    return str == NULL;
  }

  size_t len = strlen(str);
  if (path_len(path) != len) {
    return false;
  }
  return memcmp(path_start(path), str, len) == 0;
}

bool path_eq_charpn(path_t path, const char *str, uint16_t len) {
  if (path_is_null(path)) {
    return str == NULL && len == 0;
  }

  if (path_len(path) != len) {
    return false;
  }
  return memcmp(path_start(path), str, len) == 0;
}

int path_count_char(path_t path, char c) {
  const char *ptr = path_start(path);
  const char *eptr = path_end(path);

  int count = 0;
  while (ptr < eptr) {
    if (*ptr == c) {
      count++;
    }
    ptr++;
  }
  return count;
}

bool path_is_subpath(path_t path1, path_t path2) {
  if (path_is_null(path1) || path_is_null(path2)) {
    return false;
  }

  const char *ptr1 = path_start(path1);
  const char *ptr2 = path_start(path2);
  const char *eptr1 = path_end(path1);
  const char *eptr2 = path_end(path2);

  while (ptr1 < eptr1 && ptr2 < eptr2) {
    if (*ptr1 != *ptr2) {
      return false;
    }
    ptr1++;
    ptr2++;
  }

  if (ptr1 == eptr1) {
    return ptr2 == eptr2 || *ptr2 == '/';
  } else {
    return *ptr1 == '/';
  }
}

bool path_starts_with_charp(path_t path, const char *str) {
  if (path_is_null(path)) {
    return str == NULL || *str == '\0';
  }

  size_t len = strlen(str);
  if (path_len(path) < len) {
    return false;
  }
  return memcmp(path_start(path), str, len) == 0;
}

// MARK: Path Manipulation

path_t path_drop_first(path_t path) {
  uint16_t len = path_len(path);
  uint16_t off = path.view.off;
  if (len > 0) {
    len--;
    off++;
  }
  return (path_t){
    .storage = path.storage,
    .view = { .off = off, .len = len },
  };
}

path_t path_strip_leading(path_t path, char c) {
  const char *ptr = path_start(path);
  const char *eptr = path_end(path);
  while (ptr < eptr && *ptr == c) {
    ptr++;
    path.view.off++;
    path.view.len--;
  }
  return path;
}

path_t path_strip_trailing(path_t path, char c) {
  const char *ptr = path_start(path);
  const char *eptr = path_end(path);
  while (eptr > ptr && *(eptr - 1) == c) {
    eptr--;
    path.view.len--;
  }
  return path;
}

path_t path_remove_until(path_t path, char c) {
  const char *ptr = path_start(path);
  const char *eptr = path_end(path);
  while (ptr < eptr && *ptr != c) {
    ptr++;
    path.view.off++;
    path.view.len--;
  }
  return path;
}

path_t path_remove_until_reverse(path_t path, char c) {
  const char *ptr = path_start(path);
  const char *eptr = path_end(path);
  while (eptr > ptr && *(eptr - 1) != c) {
    eptr--;
    path.view.len--;
  }
  return path;
}

//

path_t path_basename(path_t path) {
  if (path_is_null(path)) {
    return DOT_PATH;
  }

  // remove any trailing slashes and count remaining slashes
  path = path_strip_trailing(path, '/');
  int slashes = path_count_char(path, '/');
  if (path_len(path) == 0) {
    return SLASH_PATH;
  } else if (slashes == 0) {
    return path;
  }

  // remove characters up to and including any last slashes
  while (slashes > 0 && path_len(path) > 0) {
    path = path_remove_until(path, '/');
    while (path_first_char(path) == '/') {
      path = path_drop_first(path);
      slashes--;
    }
  }
  return path;
}

path_t path_dirname(path_t path) {
  if (path_is_null(path) || path_len(path) == 0) {
    return DOT_PATH;
  }

  // remove any trailing slashes and count remaining slashes
  path = path_strip_trailing(path, '/');
  int slashes = path_count_char(path, '/');
  if (path_len(path) == 0) {
    return SLASH_PATH;
  } else if (slashes == 0) {
    return DOT_PATH;
  }

  // remove basename and then any trailing slashes
  path = path_remove_until_reverse(path, '/');
  path = path_strip_trailing(path, '/');
  if (path_is_null(path)) {
    return SLASH_PATH;
  }
  return path;
}


// on call with a regular path, it will return the first component
// with path.view.iter set to 1. subsequent calls will return the
// next component until the end of the path is reached, at which
// point it will return a null path. the parts do not include any
// leading or trailing slashes.
path_t path_next_part(path_t path) {
  if (path_is_null(path)) {
    return path;
  }

  // first call returns the first part
  if (path.iter.valid == 0) {
    path.iter.valid = 1;
    path.iter.orig_len = path.view.len;

    path.view.off = 0;
    path.view.len = path.storage.len;
    path = path_strip_leading(path, '/');

    uint16_t off = path.view.off;
    uint16_t len = path_remove_until(path, '/').view.off - off;
    path.view.off = off;
    path.view.len = len;
    return path;
  }

  path.view.off += path.view.len;
  path.view.len = path.iter.orig_len - path.view.off;
  path = path_strip_leading(path, '/');

  uint16_t off = path.view.off;
  uint16_t len = path_remove_until(path, '/').view.off - off;
  path.view.off = off;
  path.view.len = len;
  return path;
}

// on call with a regular path, it will return the first subpath
// with path.view.iter set to 1. subsequent calls will return the
// next full subpath until the end of the path is reached, at which
// point it will return a null path. for example, given the path
// /a/b/c, the first call will return /a, the second /a/b, and
// the third /a/b/c.
path_t path_next_subpath(path_t path) {
  if (path_is_null(path)) {
    return path;
  }

  // first call - initialize iterator and return first subpath
  if (path.iter.valid == 0) {
    path.iter.valid = 1;
    // store the original full length
    path.iter.orig_len = path.storage.len;
    
    // find first component
    const char *ptr = path.storage.str;
    const char *end = path.storage.str + path.storage.len;
    
    // skip leading slashes
    while (ptr < end && *ptr == '/') {
      ptr++;
    }
    
    // if path was all slashes, return single slash
    if (ptr >= end) {
      path.view.off = 0;
      path.view.len = 1;
      return path;
    }
    
    // find end of first component
    while (ptr < end && *ptr != '/') {
      ptr++;
    }
    
    // return view from start to end of first component
    path.view.off = 0;
    path.view.len = ptr - path.storage.str;
    return path;
  }

  // subsequent calls - extend view to include next component
  
  // check if we've already returned the full path
  if (path.view.len >= path.iter.orig_len) {
    return NULL_PATH;
  }
  
  // find where we currently end
  const char *current_end = path.storage.str + path.view.len;
  const char *full_end = path.storage.str + path.iter.orig_len;
  
  // skip any slashes at current position
  while (current_end < full_end && *current_end == '/') {
    current_end++;
  }
  
  // if no more components, we're done
  if (current_end >= full_end) {
    return NULL_PATH;
  }
  
  // find end of next component
  while (current_end < full_end && *current_end != '/') {
    current_end++;
  }
  
  // extend view to include the next component
  // view always starts at 0 and extends to current_end
  path.view.off = 0;
  path.view.len = current_end - path.storage.str;
  
  return path;
}

bool path_iter_end(path_t path) {
  return path_is_null(path_next_part(path));
}
