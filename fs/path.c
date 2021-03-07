//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#include <path.h>
#include <string.h>
#include <mm/heap.h>
#include <printf.h>

const char *slash = "/";
const char *dot = ".";

// path constants
path_t path_null;
path_t path_slash;
path_t path_dot;


void path_init() {
  path_null = (path_t) { NULL, 0, 0, NULL, NULL };
  path_slash = str_to_path(slash);
  path_dot = str_to_path(dot);
}

// internal helpers

int path_num_occurrences(path_t path, char c) {
  int count = 0;
  char *ptr = path.start;
  while (ptr < path.end) {
    if (*ptr == c) {
      count++;
    }
    ptr++;
  }
  return count;
}

char *path_first_occurence(path_t path, char c) {
  char *ptr = path.start;
  while (*ptr != c && ptr < path.end) {
    ptr++;
  }

  if (*ptr != c) {
    return NULL;
  }
  return ptr;
}

path_t path_skip_over(path_t path, char c) {
  char *ptr = path.start;
  while (ptr < path.end && *ptr == c) {
    ptr++;
  }
  return (path_t){ path.str, path.len, path.count, ptr, path.end };
}

path_t path_skip_until(path_t path, char c) {
  char *ptr = path.start;
  while (ptr < path.end && *ptr != c) {
    ptr++;
  }
  return (path_t){ path.str, path.len, path.count, ptr, path.end };
}

path_t path_skip_over_reverse(path_t path, char c) {
  char *ptr = path.end;
  while (ptr > path.start && *(ptr - 1) == c) {
    ptr--;
  }
  return (path_t){ path.str, path.len, path.count, path.start, ptr };
}

path_t path_skip_until_reverse(path_t path, char c) {
  char *ptr = path.end;
  while (ptr > path.start && *(ptr - 1) != c) {
    ptr--;
  }
  return (path_t){ path.str, path.len, path.count, path.start, ptr };
}

// path_t operations

path_t str_to_path(const char *path) {
  if (path == NULL) {
    return path_null;
  }

  size_t len = strlen(path);
  return (path_t) { path, len, 0, (char *) path, (char *) path + len };
}

char *path_to_str(path_t path) {
  size_t len = path.end - path.start;
  char *str = kmalloc(len + 1);
  memcpy(str, path.start, len);
  str[len] = '\0';
  return str;
}

//

void pathcpy(char *dest, path_t path) {
  size_t len = p_len(path);
  memcpy(dest, path.start, len);
  dest[len] = '\0';
}

int patheq(path_t path1, path_t path2) {
  if (p_is_null(path1)) return 1;
  if (p_is_null(path2)) return -1;

  size_t len1 = p_len(path1);
  size_t len2 = p_len(path2);
  if (len1 < len2) return -1;
  if (len1 > len2) return 1;
  return memcmp(path1.start, path2.start, len1);
}

int pathcmp(path_t path1, path_t path2) {
  if (p_is_null(path1)) return 1;
  if (p_is_null(path2)) return -1;

  size_t len = min(p_len(path1), p_len(path2));
  return memcmp(path1.start, path2.start, len);
}

int pathcmp_s(path_t path, const char *str) {
  if (p_is_null(path)) {
    return 1;
  }

  size_t len = p_len(path);
  return memcmp(path.start, str, len);
}

int patheq_s(path_t path, const char *str) {
  if (p_is_null(path)) {
    return 1;
  }

  size_t len1 = p_len(path);
  size_t len2 = strlen(str);
  if (len1 < len2) return -1;
  if (len1 > len2) return 1;
  return memcmp(path.start, str, len1);
}

//

path_t path_dirname(path_t path) {
  if (p_is_null(path) || path.len == 0 || p_len(path) == 0) {
    return path_dot;
  }

  // remove trailing slashes
  path = path_skip_over_reverse(path, '/');
  // count remaining slashes
  int slashes = path_num_occurrences(path, '/');
  if (p_len(path) == 0 || p_len(path) == slashes) {
    return path_slash;
  } else if (slashes == 0) {
    return path_dot;
  }

  // remove trailing non-slash characters
  path = path_skip_until_reverse(path, '/');
  // remove trailing slashes (again)
  path = path_skip_over_reverse(path, '/');

  if (p_len(path) == 0) {
    return path_slash;
  }

  path.len = path.end - path.start;
  return path;
}

path_t path_basename(path_t path) {
  if (p_is_null(path) || path.len == 0 || p_len(path) == 0) {
    return path_dot;
  }

  // remove any trailing slashes
  path = path_skip_over_reverse(path, '/');
  // count remaining slashes
  int slashes = path_num_occurrences(path, '/');
  if (p_len(path) == 0 || p_len(path) == slashes) {
    return path_slash;
  } else if (slashes == 0) {
    return path;
  }

  // remove characters up to and including any last slashes
  char *ptr = path.start;
  while (slashes > 0) {
    if (*ptr == '/') {
      slashes--;
    }
    ptr++;
  }

  size_t len = path.end - ptr;
  return (path_t){ path.str, len, path.count, ptr, path.end };
}

path_t path_prefix(path_t path) {
  if (pathcmp(path_slash, path) == 0) {
    return path_slash;
  }
  return path_dot;
}

path_t path_suffix(path_t path) {
  if (*(path.end - 1) == '/') {
    return path_slash;
  }
  return path_null;
}

void path_print(path_t path) {
  char str[p_len(path) + 1];
  pathcpy(str, path);
  kprintf("path: %s\n", str);
}

//

path_t path_next_part(path_t path) {
  if (path.start >= path.str + path.len) {
    return path_null;
  }

  char *real_end = (char *) path.str + path.len;
  if (path.count == 0 && *path.start == '/') {
    return (path_t){ path.str, path.len, path.count + 1, path.start, path.start + 1 };
  } else if (path.count > 0) {
    path.start = path.end;
    path.end = real_end;
  }

  // skip leading slashes
  path = path_skip_over(path, '/');
  // next part start ptr
  char *start = path.start;
  // skip until next slash (or end)
  path = path_skip_until(path, '/');
  // next part end ptr
  char *end = path.start;

  if (start >= real_end && end >= real_end) {
    return path_null;
  }

  return (path_t){ path.str, path.len, path.count + 1, start, end };
}
