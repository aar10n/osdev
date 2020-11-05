//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <dirent.h>
#include <fs.h>

//

dirent_t *dirent_create(fs_node_t *node, const char *name) {
  if (strlen(name) > MAX_FILE_NAME) {
    errno = ENAMETOOLONG;
    return NULL;
  }

  inode_t *inode = inode_get(node);
  inode_t *parent = inode_get(node->parent);
  if (inode == NULL || parent == NULL) {
    return NULL;
  }

  return node->fs->impl->link(node->fs, inode, parent, (char *) name);
}

int dirent_remove(fs_node_t *node, dirent_t *dirent) {
  inode_t *inode = inode_get(node);
  if (inode == NULL) {
    return -1;
  }

  return node->fs->impl->unlink(node->fs, inode, dirent);
}
