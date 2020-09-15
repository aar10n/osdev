//
// Created by Aaron Gill-Braun on 2020-09-11.
//

#include <libgen.h>
#include <stdbool.h>
#include <string.h>

static char dir_buffer[1024];
static char base_buffer[1024];

int count_slashes(char *str) {
  int count = 0;
  while (*str) {
    if (*str == '/') count++;
    str++;
  }
  return count;
}

int strip_trailing(char *str, size_t len, bool slashes) {
  // remove trailing slashes or non-slashes
  int stripped = 0;
  while (len > 0 && (slashes ? str[len - 1] == '/' : str[len - 1] != '/')) {
    str[len - 1] = '\0';
    stripped++;
    len--;
  }
  return stripped;
}

/* ---- libgen functions ---- */

char *basename(char *path) {
  if (path == NULL) {
    return ".";
  }

  size_t len = strlen(path);
  strcpy(base_buffer, path);
  if (len == 0) {
    return ".";
  }

  // remove trailing slashes
  len -= strip_trailing(base_buffer, len, true);

  // count the total slashes
  int slashes = count_slashes(base_buffer);
  if (len == 0 || slashes == len) {
    return "/";
  } else if (slashes == 0) {
    return base_buffer;
  }

  // remove characters up to and including last slash
  char *ptr = base_buffer;
  while (slashes > 0) {
    if (*ptr == '/') {
      slashes--;
    }

    *ptr = '\0';
    ptr++;
  }

  return ptr;
}

char *dirname(char *path) {
  if (path == NULL) {
    return ".";
  }

  size_t len = strlen(path);
  strcpy(dir_buffer, path);
  if (len == 0) {
    return ".";
  }

  // remove trailing slashes
  len -= strip_trailing(dir_buffer, len, true);

  // count remaining slashes
  int slashes = count_slashes(dir_buffer);
  if (len == 0 || slashes == len) {
    return "/";
  } else if (slashes == 0) {
    return ".";
  }

  // remove trailing non-slash characters
  len -= strip_trailing(dir_buffer, len, false);

  // remove trailing slashes (again)
  len -= strip_trailing(dir_buffer, len, true);

  if (len == 0) {
    return "/";
  }
  return dir_buffer;
}

