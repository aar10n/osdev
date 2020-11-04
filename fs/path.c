//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#include <path.h>
#include <fs.h>
#include <file.h>
#include <dirent.h>
#include <string.h>
#include <mm/heap.h>

extern int count_slashes(char *str);
extern int strip_trailing(char *str, size_t len, bool slashes);
extern fs_node_map_t links;

//

char *path_basename(const char *path) {
  if (path == NULL) {
    return ".";
  }

  size_t len = strlen(path);
  if (len == 0) {
    return ".";
  }

  char *buffer = kmalloc(MAX_PATH);
  strcpy(buffer, path);

  // remove trailing slashes
  len -= strip_trailing(buffer, len, true);

  // count the total slashes
  int slashes = count_slashes(buffer);
  if (len == 0 || slashes == len) {
    kfree(buffer);
    return "/";
  } else if (slashes == 0) {
    return buffer;
  }

  // remove characters up to and including last slash
  char *ptr = buffer;
  while (slashes > 0) {
    if (*ptr == '/') {
      slashes--;
    }

    *ptr = '\0';
    ptr++;
  }

  return ptr;
}

char *path_dirname(const char *path) {
  if (path == NULL) {
    return ".";
  }

  size_t len = strlen(path);
  if (len == 0) {
    return ".";
  }

  char *buffer = kmalloc(MAX_PATH);
  strcpy(buffer, path);

  // remove trailing slashes
  len -= strip_trailing(buffer, len, true);

  // count remaining slashes
  int slashes = count_slashes(buffer);
  if (len == 0 || slashes == len) {
    kfree(buffer);
    return "/";
  } else if (slashes == 0) {
    kfree(buffer);
    return ".";
  }

  // remove trailing non-slash characters
  len -= strip_trailing(buffer, len, false);

  // remove trailing slashes (again)
  len -= strip_trailing(buffer, len, true);

  if (len == 0) {
    kfree(buffer);
    return "/";
  }
  return buffer;
}

char *path_iter_next(path_iter_t *iter) {
  if (iter->last) {
    kfree(iter->last);
  }

  if (iter->ptr == NULL) {
    return NULL;
  } else if (iter->ptr == iter->path) {
    if (iter->ptr[0] == '.' || iter->ptr[0] == '/') {
      char *ptr = iter->ptr;
      while (*ptr != '/') {
        ptr++;
      }

      ptr++;
      iter->ptr = ptr;

      size_t len = iter->ptr - iter->path;
      char *str = kmalloc(len + 1);
      memcpy(str, iter->path, len);
      str[len] = '\0';
      return str;
    }
  } else if (*iter->ptr == '\0') {
    iter->ptr = NULL;
    return NULL;
  }

  char *ptr = iter->ptr;
  char *next = kmalloc(MAX_FILE_NAME);

  // skip any leading slashes
  while (*ptr == '/') {
    ptr++;
  }

  // copy until the next slash (or null)
  char *dest = next;
  while (*ptr && *ptr != '/') {
    *dest = *ptr;
    dest++;
    ptr++;
  }

  if (!*ptr) {
    iter->ptr = NULL;
  } else {
    iter->ptr = ptr;
  }

  iter->last = next;
  return next;
}

//

fs_node_t *path_resolve_link(fs_node_t *node, int flags) {
  if (!IS_IFLNK(node->mode)) {
    return node;
  }

  if (flags & O_NOFOLLOW) {
    errno = ELOOP;
    return NULL;
  }

  size_t lcount = 0;
  while (IS_IFLNK(node->mode)) {
    if (lcount >= MAX_SYMLINKS) {
      // too many symbolic links encountered
      errno = ELOOP;
      return NULL;
    }

    aquire(links.rwlock);
    fs_node_t *linked = *map_get(&links.hash_table, node->iflnk.path);
    release(links.rwlock);
    if (linked) {
      node = linked;
      lcount++;
    } else {
      // dangling symbolic link
      errno = ENOENT;
      return NULL;
    }
  }

  return node;
}

fs_node_t *path_get_node(fs_node_t *root, const char *path, int flags) {
  #define get_node_cleanup() \
  kfree(dirname); kfree(basename); kfree(iter.last)

  size_t path_len = strlen(path);
  if (path_len > MAX_PATH) {
    errno = ENAMETOOLONG;
    return NULL;
  }

  char *dirname = path_dirname(path);
  char *basename = path_basename(path);

  path_iter_t iter = path_iter(dirname);
  char *part;
  fs_node_t *node = root;
  while ((part = path_iter_next(&iter))) {
    if (!IS_IFDIR(node->mode)) {
      fs_node_t *resolved = path_resolve_link(node, flags);
      if (!resolved) {
        get_node_cleanup();
        return NULL;
      }

      if (IS_IFDIR(resolved->mode)) {
        node = resolved;
      } else {
        // part exists but it is not a directory
        errno = ENOTDIR;
        get_node_cleanup();
        return NULL;
      }
    }

    // look through directories children
    fs_node_t *child = node->ifdir.first;
    while (child) {
      if (strcmp(child->name, part) == 0) {
        break;
      }
      child = child->next;
    }

    if (child == NULL) {
      // directory component does not exist
      errno = ENOENT;
      get_node_cleanup();
      return NULL;
    }

    node = child;
  }

  // search last directory for child
  while (node) {
    if (strcmp(node->name, basename) == 0) {
      break;
    }
    node = node->next;
  }
  get_node_cleanup();

  if (node == NULL) {
    errno = ENOENT;
  }
  return node;
}

char *path_from_node(fs_node_t *node) {
  if (is_root(node)) {
    return "/";
  }

  char *path = kmalloc(MAX_PATH);
  char *ptr = path;
  memset(path, 0, MAX_PATH);

  while (!is_root(node)) {
    int len = strlen(node->name);

    // copy in reverse
    for (int i = 0; i < len; i++) {
      int c = len - i - 1;
      *ptr = node->name[c];
      ptr++;
    }

    *ptr = '/';
    ptr++;

    node = node->parent;
  }

  *ptr = '\0';

  // reverse the path to get the final result
  reverse(path);
  return path;
}
