//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#ifndef KERNEL_VFS_PATH_H
#define KERNEL_VFS_PATH_H
#define __PATH__

#include <kernel/base.h>

#define MAX_PATH_LEN UINT16_MAX

#define NULL_PATH   ((path_t){{NULL, 0}, {0, 0}})
#define SLASH_PATH  ((path_t){{"/", 1}, {0, 1}})
#define DOT_PATH    ((path_t){{".", 1}, {0, 1}})

/**
 * A filesystem path.
 *
 * The path struct is used to represent a path in the filesystem. It does not own
 * any of the memory it points to and is valid only as long as the backing string
 * is. The path struct should be passed by value and no struct should ever hold
 * hold a reference to one.
 *
 * \note The longest support path length is 65535 bytes but the system limit
 *       may be much lower.
 */
typedef struct path {
  struct {
    const char *str;
    uint16_t len;
  } storage;
  struct {
    uint16_t off;
    uint16_t len;
  } view;
  struct {
    uint16_t orig_len;  // original length
    uint16_t valid : 1; // path is an iterator
  } iter;
} path_t;

static inline size_t path_len(path_t path) { return path.view.len; }
static inline const char *path_start(path_t path) { return path.storage.str + path.view.off; }
static inline const char *path_end(path_t path) { return path.storage.str + path.view.off + path.view.len; }
static inline int path_first_char(path_t path) { return path_len(path) ? path_start(path)[0] : '\0'; }
static inline bool path_is_null(path_t path) { return path_len(path)== 0; }
static inline bool path_is_slash(path_t path) { return path_len(path) == 1 && path_start(path)[0] == '/'; }
static inline bool path_is_dot(path_t path) { return path_len(path) == 1 && path_start(path)[0] == '.'; }
static inline bool path_is_dotdot(path_t path) { return path_len(path) == 2 && path_start(path)[0] == '.' && path_start(path)[1] == '.'; }
static inline bool path_is_special(path_t path) { return path_is_slash(path) || path_is_dot(path) || path_is_dotdot(path); }
static inline bool path_is_absolute(path_t path) { return path_len(path) && path_start(path)[0] == '/'; }
static inline bool path_is_relative(path_t path) { return !path_is_absolute(path); }

/// Creates a new path from a string.
path_t path_make(const char *str);

/// Creates a new path from a string with a specified length.
/// Note: the length must be less than or equal to MAX_PATH_LEN
///       or else the string will be truncated.
path_t path_new(const char *str, size_t len);

/// Allocates a new string and copies the path into it. The caller is responsible
/// for freeing the string.
char *path2str(path_t path);

/// Copies the path into a buffer. The buffer should at least have size path_len(path)+1
/// to account for the null terminator. Returns the number of bytes copied.
size_t path_copy(char *dest, size_t size, path_t path);


/// Compares two paths for equality. Returns true if the paths the same.
bool path_eq(path_t path1, path_t path2);

/// Compares the path and the string for equality. Returns true if the paths the same.
bool path_eq_charp(path_t path, const char *str);

/// Compares a path and a string with a specified length for equality. Returns true if
/// the paths the same.
bool path_eq_charpn(path_t path, const char *str, uint16_t len);

/// Returns true if path1 is at or under path2 (i.e. /a/b/c is under /a/b).
bool path_is_subpath(path_t path1, path_t path2);

/// Counts the number of occurrences of a character in a path.
int path_count_char(path_t path, char c);


/// Returns a new path with the first character removed.
path_t path_drop_first(path_t path);

/// Returns a new path with all leading characters matching c removed.
path_t path_strip_leading(path_t path, char c);

/// Returns a new path with all trailing characters matching c removed.
path_t path_strip_trailing(path_t path, char c);

/// Returns a new path with all characters up to the first occurrence of c removed.
path_t path_remove_until(path_t path, char c);

/// Returns a new path with all characters up to the last occurrence of c removed.
path_t path_remove_until_reverse(path_t path, char c);


/// Returns the base name of a path.
path_t path_basename(path_t path);

/// Returns the directory name of a path.
path_t path_dirname(path_t path);

/// Returns the first or next component of a path. Returns NULL_PATH if there are no more.
path_t path_next_part(path_t path);

/// Returns whether the path iterator has reached the end.
bool path_iter_end(path_t path);

#endif
