//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <vfs.h>
#include <inode.h>
#include <path.h>
#include <file.h>
#include <device.h>
#include <process.h>
#include <panic.h>
#include <mm/heap.h>
#include <asm/bitmap.h>
#include <hash_table.h>
#include <murmur3.h>
#include <stdio.h>

#include <ramfs/ramfs.h>


// #define PWD (current->pwd)
#define PWD (PERCPU->pwd)

// #define UID (current->uid)
#define UID (PERCPU->uid)

// #define GID (current->gid)
#define GID (PERCPU->gid)


fs_t *root_fs = NULL;
fs_node_t *fs_root = NULL;
fs_node_map_t *nodes = NULL;
fs_node_table_t *links = NULL;

uint32_t hash(char *str) {
  size_t len = strlen(str);
  uint32_t out;
  murmur_hash_x86_32(str, len, 0xDEADBEEF, &out);
  return out;
}

uint64_t pair(uint32_t a, uint32_t b) {
  return (uint64_t) a << 32 | b;
}

//

fs_node_t *get_node(ino_t ino, dev_t dev) {
  uint64_t key = pair(ino, dev);
  aquire(nodes->rwlock);
  rb_node_t *node = rb_tree_find(nodes->tree, key);
  release(nodes->rwlock);
  if (node == NULL) {
    return NULL;
  }
  return node->data;
}

void add_node(fs_node_t *node) {
  uint64_t key = pair(node->inode, node->dev);
  lock(nodes->rwlock);
  rb_tree_insert(nodes->tree, key, node);
  unlock(nodes->rwlock);
}

void remove_node(fs_node_t *node) {
  uint64_t key = pair(node->inode, node->dev);
  lock(nodes->rwlock);
  rb_tree_delete(nodes->tree, key);
  unlock(nodes->rwlock);
}

//

int validate_flags_for_node(fs_node_t *node, int flags) {
  if (flags & O_CREAT && flags & O_EXCL) {
    errno = EEXIST;
    return -1;
  } else if (IS_IFDIR(node->mode) && ((flags & O_WRONLY || flags & O_RDWR) ||
    (flags & O_CREAT && !(flags & O_DIRECTORY)))) {
    errno = EISDIR;
    return -1;
  } else if (!IS_IFDIR(node->mode) && flags & O_DIRECTORY) {
    errno = ENOTDIR;
    return -1;
  } else if ((!IS_IFDIR(node->mode) && flags & O_SEARCH) ||
    (IS_IFIFO(node->mode) && flags & O_RDWR)) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

//

fs_node_table_t *create_node_table() {
  fs_node_table_t *table = kmalloc(sizeof(fs_node_table_t));
  table->hash_table.map.hasher = hash;
  table->hash_table.map.capacity = 1024;
  table->hash_table.map.load_factor = LOAD_FACTOR;
  map_init(&table->hash_table);

  spinrw_init(&table->rwlock);
  return table;
}

fs_node_map_t *create_node_map() {
  fs_node_map_t *map = kmalloc(sizeof(fs_node_map_t));
  map->tree = create_rb_tree();
  spinrw_init(&map->rwlock);
  return map;
}

dirent_t *create_dirent(ino_t ino, const char *name) {
  dirent_t *dirent = kmalloc(sizeof(dirent_t));
  dirent->inode = ino;
  strcpy(dirent->name, name);
  return dirent;
}

//

void vfs_init() {
  nodes = create_node_map();
  links = create_node_table();
  inodes = create_inode_table();

  root_fs = ramfs_mount(0, NULL);
  fs_root = root_fs->root;
  fs_root->mode = S_IFDIR;

  fs_root->dirent = create_dirent(fs_root->inode, "/");
  vfs_populate_dir_node(fs_root);

  // `/dev` directory
  fs_node_t *dev = vfs_create_node(fs_root, S_IFDIR);
  int result = vfs_add_node(fs_root, dev, "dev");
  if (result < 0) {
    panic("failed to create directory: /dev | %s", strerror(errno));
  }
}

fs_node_t *vfs_create_node(fs_node_t *parent, mode_t mode) {
  if (__popcnt64(mode & I_TYPE_MASK) == 0) {
    errno = EINVAL;
    return NULL;
  }

  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  memset(node, 0, sizeof(fs_node_t));
  inode_t *inode = parent->fs->impl->create(parent->fs, mode);
  if (inode == NULL) {
    kfree(node);
    errno = ENOENT;
    return NULL;
  }

  node->inode = inode->ino;
  node->dev = inode->dev;
  node->mode = mode;
  node->fs = parent->fs;
  node->parent = parent;

  if (mode & S_IFDIR) {
    vfs_populate_dir_node(node);
  }

  return node;
}

fs_node_t *vfs_create_from_inode(fs_node_t *parent, inode_t *inode) {
  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  memset(node, 0, sizeof(fs_node_t));

  node->inode = inode->ino;
  node->dev = inode->dev;
  node->mode = inode->mode;
  node->dirent = NULL;
  node->fs = parent->fs;
  node->parent = parent;
  node->next = NULL;
  node->prev = NULL;

  if (inode->mode & S_IFDIR) {
    vfs_populate_dir_node(node);
  }

  return node;
}

fs_node_t *vfs_copy_node(fs_node_t *node) {
  fs_node_t *copy = kmalloc(sizeof(fs_node_t));
  memcpy(copy, node, sizeof(fs_node_t));
  return copy;
}

void vfs_populate_dir_node(fs_node_t *node) {
  kassert(IS_IFDIR(node->mode));

  // add entries for '.' and '..'
  fs_node_t *dot = vfs_copy_node(node);
  dot->dev = 0;
  dot->fs = root_fs;
  dot->parent = node;
  dot->next = NULL;
  dot->prev = NULL;
  dot->dirent = create_dirent(node->inode, ".");

  fs_node_t *parent = node->parent ? node->parent : node;
  fs_node_t *dotdot = vfs_copy_node(parent);
  dotdot->dev = 0;
  dotdot->fs = root_fs;
  dotdot->parent = node;
  dotdot->next = NULL;
  dotdot->prev = NULL;
  dotdot->dirent = create_dirent(parent->inode, "..");

  dot->next = dotdot;
  dotdot->prev = dot;

  node->ifdir.first = dot;
  node->ifdir.last = dotdot;
}

fs_node_t *vfs_get_node(path_t path, int flags) {
  if (path.len > MAX_PATH) {
    errno = ENAMETOOLONG;
    return NULL;
  }

  // handle simple case
  if (patheq_s(path, "/") == 0) {
    return fs_root;
  } else if (patheq_s(path, ".") == 0) {
    return PWD;
  }

  fs_node_t *node = PWD;
  path_t part = path_dirname(path);
  while (!p_is_null(part = path_next_part(part))) {
    if (patheq_s(part, "/") == 0) {
      node = fs_root;
      continue;
    } else if (patheq_s(part, ".") == 0) {
      // node = node;
      continue;
    } else if (patheq_s(part, "..") == 0) {
      node = node->parent;
      continue;
    }

    // check acl for `node`
    fs_node_t *child = vfs_find_child(node, part);
    if (child == NULL) {
      return NULL;
    }

    // first resolve any links if needed
    if (IS_IFLNK(child->mode)) {
      if (flags & O_NOFOLLOW && !(flags & V_NOFAIL)) {
        errno = ELOOP;
        return NULL;
      }

      // resolve the link
      child = vfs_resolve_link(child, flags);
      if (child == NULL) {
        return NULL;
      }
    }

    if (!IS_IFDIR(child->mode)) {
      errno = ENOTDIR;
      return NULL;
    }

    node = child;
  }

  path_t name = path_basename(path);
  if (patheq_s(name, ".") == 0) {
    return node;
  } else if (patheq_s(name, "..") == 0) {
    return node->parent;
  }

  fs_node_t *file = vfs_find_child(node, name);
  if (file == NULL) {
    errno = ENOENT;
    return NULL;
  }

  // first resolve any links if needed
  if (IS_IFLNK(file->mode)) {
    if (flags & O_NOFOLLOW) {
      if (flags & V_NOFAIL) {
        return file;
      }
      errno = ELOOP;
      return NULL;
    }

    // resolve the link
    file = vfs_resolve_link(file, flags);
    if (file == NULL) {
      return NULL;
    }
  }

  if (validate_flags_for_node(file, flags) < 0) {
    return NULL;
  }
  return file;
}

fs_node_t *vfs_find_child(fs_node_t *parent, path_t name) {
  if (!IS_IFDIR(parent->mode)) {
    errno = ENOTDIR;
    return NULL;
  }

  fs_node_t *child = parent->ifdir.first;
  while (child) {
    if (pathcmp_s(name, child->dirent->name) == 0) {
      return child;
    }
    child = child->next;
  }

  errno = ENOENT;
  return NULL;
}

fs_node_t *vfs_resolve_link(fs_node_t *node, int flags) {
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
      errno = ELOOP;
      return NULL;
    }

    fs_node_t *linked = vfs_get_link(node->iflnk.path);
    if (linked) {
      node = linked;
      lcount++;
      continue;
    }

    // get the link from the file
    inode_t *inode = inode_get(node);
    if (inode == NULL) {
      return NULL;
    }

    char *path = kmalloc(MAX_PATH);
    ssize_t nread = node->fs->impl->read(node->fs, inode, 0, MAX_PATH, path);
    if (nread < 0) {
      kfree(path);
      return NULL;
    }

    linked = vfs_get_node(str_to_path(path), O_NOFOLLOW);
    if (linked == NULL) {
      kfree(path);
      return NULL;
    }

    vfs_add_link(path, linked);
    node = linked;
    lcount++;
  }

  return node;
}

int vfs_add_device(fs_device_t *device) {
  fs_node_t *dev_dir = vfs_get_node(str_to_path("/dev"), 0);
  if (dev_dir == NULL) {
    panic("[vfs] /dev: %s", strerror(errno));
  }

  fs_node_t *dev = vfs_create_node(dev_dir, S_IFBLK);
  dev->ifblk.device = device;

  char name[MAX_FILE_NAME];
  ksprintf(name, "disk%d", device->id);
  return vfs_add_node(dev_dir, dev, name);
}

int vfs_add_node(fs_node_t *parent, fs_node_t *child, char *name) {
  if (parent == NULL || child == NULL) {
    return -1;
  }

  kassert(IS_IFDIR(parent->mode));
  inode_t *parent_inode = inode_get(parent);
  inode_t *inode = inode_get(child);
  if (parent_inode == NULL || inode == NULL) {
    return -1;
  }

  dirent_t *dirent = child->fs->impl->link(child->fs, inode, parent_inode, name);
  if (dirent == NULL) {
    return -1;
  }

  child->dirent = dirent;
  child->parent = parent;
  if (parent->ifdir.last == NULL) {
    parent->ifdir.first = child;
    parent->ifdir.last = child;
  } else {
    child->prev = parent->ifdir.last;
    parent->ifdir.last->next = child;
    parent->ifdir.last = child;
  }
  return 0;
}

int vfs_remove_node(fs_node_t *node) {
  while (node) {
    if (strcmp(node->dirent->name, ".") != 0 ||
      strcmp(node->dirent->name, "..") != 0) {
      inode_t *inode = inode_get(node);
      if (inode == NULL) {
        return -1;
      }

      int result = node->fs->impl->unlink(node->fs, inode, node->dirent);
      if (result < 0) {
        return -1;
      }
    } else {
      errno = EPERM;
      return -1;
    }

    // cleans up all external references to the node
    if (node->prev) {
      node->prev->next = node->next;
    }
    if (node->next) {
      node->next->prev = node->prev;
    }

    if (node->parent && node->parent->ifdir.first == node) {
      node->parent->ifdir.first = node->next;
    }

    // break any links to this node
    char *node_path = vfs_path_from_node(node);
    vfs_remove_link(node_path);
    kfree(node_path);

    if (IS_IFDIR(node->mode)) {
      int result = vfs_remove_node(node->ifdir.first);
      if (result < 0) {
        kfree(node);
        return -1;
      }
    }

    fs_node_t *next = node->next;
    kfree(node);
    node = next;
  }

  return 0;
}

int vfs_swap_node(fs_node_t *orig_node, fs_node_t *new_node) {
  inode_t *parent_inode = inode_get(orig_node->parent);
  inode_t *orig_inode = inode_get(orig_node);
  inode_t *new_inode = inode_get(new_node);
  if (parent_inode == NULL || orig_inode == NULL || new_inode == NULL) {
    return -1;
  }

  dirent_t *dirent = new_node->fs->impl->link(
    new_node->fs, new_inode, parent_inode, orig_node->dirent->name
  );
  if (dirent == NULL) {
    return -1;
  }
  new_node->dirent = dirent;

  int result = orig_node->fs->impl->unlink(orig_node->fs, orig_inode, orig_node->dirent);
  if (result < 0) {
    return -1;
  }

  new_node->parent = orig_node->parent;
  new_node->prev = orig_node->prev;
  new_node->next = orig_node->next;

  if (orig_node->prev) {
    orig_node->prev->next = new_node;
  }
  if (orig_node->next) {
    orig_node->next->prev = new_node;
  }

  if (orig_node->parent && orig_node->parent->ifdir.first == orig_node) {
    orig_node->parent->ifdir.first = new_node;
  }

  // break any links to this node
  char *node_path = vfs_path_from_node(orig_node);
  vfs_remove_link(node_path);
  kfree(node_path);
  return 0;
}

//

fs_node_t *vfs_get_link(const char *path) {
  aquire(links->rwlock);
  fs_node_t **node = map_get(&links->hash_table, (char *) path);
  release(links->rwlock);
  if (node) {
    return *node;
  }
  return NULL;
}

void vfs_add_link(const char *path, fs_node_t *node) {
  lock(links->rwlock);
  map_set(&links->hash_table, (char *) path, node);
  unlock(links->rwlock);
}

void vfs_remove_link(const char *path) {
  lock(links->rwlock);
  map_delete(&links->hash_table, (char *) path);
  unlock(links->rwlock);
}

//

char *vfs_path_from_node(fs_node_t *node) {
  if (is_root(node)) {
    return "/";
  }

  char *path = kmalloc(MAX_PATH);
  char *ptr = path;
  memset(path, 0, MAX_PATH);

  while (!is_root(node)) {
    int len = strlen(node->dirent->name);

    // copy in reverse
    for (int i = 0; i < len; i++) {
      int c = len - i - 1;
      *ptr = node->dirent->name[c];
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

