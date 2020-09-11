//
// Created by Aaron Gill-Braun on 2020-09-09.
//

#include <string.h>
#include <stdlib.h>

#include "path.h"

int split_str(char *str, char delim, char ***result) {
  char *ptr = str;

  int part_count = 0;
  int char_count = 0;
  uint32_t len = strlen(str);

  char** parts = malloc(len * sizeof(char *));
  char *part = malloc(len);
  while (*ptr) {
    char ch = *ptr;
    ptr++;

    if (ch == delim) {
      if (char_count == 0) {
        continue;
      }

      char *tmp = malloc(char_count + 1);
      memcpy(tmp, part, char_count);
      memset(part, 0, char_count);
      tmp[char_count] = '\0';
      parts[part_count] = tmp;

      char_count = 0;
      part_count++;
    } else {
      part[char_count] = ch;
      char_count++;
    }
  }

  if (char_count > 0) {
    char *tmp = malloc(char_count + 1);
    memcpy(tmp, part, char_count);
    memset(part, 0, char_count);
    tmp[char_count] = '\0';
    parts[part_count] = tmp;

    part_count++;
  }

  if ((uint32_t) part_count < len) {
    parts = realloc(parts, part_count * sizeof(char *));
  }

  free(part);

  *result = parts;
  return part_count;
}

void free_split(char **parts, int n) {
  for (int i = 0; i < n; i++) {
    free(parts[i]);
  }
  free(parts);
}

//

int split_path(char *path, char ***result) {
  return split_str(path, '/', result);
}

char *concat_path(char *dir, char *base) {
  if (dir == NULL) return base;
  if (base == NULL) return dir;

  uint32_t dir_len = strlen(dir);
  uint32_t base_len = strlen(base);
  uint32_t total_len = dir_len + base_len;
  if (dir[dir_len - 1] == '/') {
    char *path = malloc(total_len + 1);
    memcpy(path, dir, dir_len);
    memcpy(path + dir_len, base, base_len);
    path[total_len] = '\0';
    return path;
  } else {
    char *path = malloc(total_len + 2);
    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, base, base_len);
    path[total_len + 1] = '\0';
    return path;
  }
}

