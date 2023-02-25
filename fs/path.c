//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#include <path.h>
#include <mm.h>
#include <printf.h>
#include <panic.h>
#include <string.h>
#include <murmur3.h>


#define ASSERT(x) kassert(x)
#define HASH_SEED 0xDEADBEEF


// MARK: Path API

path_t str2path(const char *str) {
  if (str == NULL) {
    return NULL_PATH;
  }

  size_t len = strlen(str);
  ASSERT(len <= UINT16_MAX);
  return strn2path(str, (uint16_t) len);
}

path_t strn2path(const char *str, uint16_t len) {
  if (str == NULL) {
    return NULL_PATH;
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

bool path_eq_str(path_t path, const char *str) {
  if (path_is_null(path)) {
    return str == NULL;
  }

  size_t len = strlen(str);
  if (path_len(path) != len) {
    return false;
  }
  return memcmp(path_start(path), str, len) == 0;
}

bool path_eq_strn(path_t path, const char *str, uint16_t len) {
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
    path.view.iter = 0;
    return path;
  }

  if (path.view.iter == 0) {
    path.view.iter = 1;
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
  path.view.len = path.storage.len - path.view.off;
  path = path_strip_leading(path, '/');

  uint16_t off = path.view.off;
  uint16_t len = path_remove_until(path, '/').view.off - off;
  path.view.off = off;
  path.view.len = len;
  return path;
}
