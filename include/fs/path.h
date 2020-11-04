//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#ifndef FS_PATH_H
#define FS_PATH_H

#include <base.h>
#include <fs.h>

#define p_len(path) \
  ((path).end - (path).start)

#define p_is_null(path) \
  ((path).str == path_null.str)

#define p_is_slash(path) \
  ((path).str == path_slash.str)

#define p_is_dot(path) \
  ((path).str == path_slash.str)


typedef struct {
  const char *str;
  size_t len;
  char *start;
  char *end;
} path_t;

extern path_t path_null;
extern path_t path_slash;
extern path_t path_dot;

void path_init_constants();

path_t str_to_path(const char *path);
char *path_to_str(path_t path);

void pathcpy(char *dest, path_t path);
int patheq(path_t path1, path_t path2);
int pathcmp(path_t path1, path_t path2);
int pathcmp_s(path_t path, const char *str);
int patheq_s(path_t path, const char *str);

path_t path_dirname(path_t path);
path_t path_basename(path_t path);
path_t path_prefix(path_t path);
path_t path_next_part(path_t path);

void path_print(path_t path);

#endif
