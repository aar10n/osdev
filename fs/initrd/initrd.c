//
// Created by Aaron Gill-Braun on 2022-12-19.
//

#include <initrd/initrd.h>
#include <ramfs/ramfs.h>
#include <super.h>
#include <dentry.h>
#include <inode.h>
#include <path.h>
#include <thread.h>
#include <string.h>
#include <panic.h>
#include <printf.h>

#include <hash_map.h>

#define INITRD_FIRST_ENTRY(buffer) offset_ptr(buffer, sizeof(initrd_header_t))
#define INITRD_NEXT_ENTRY(entry) offset_ptr(entry, sizeof(initrd_entry_t) + (entry)->path_len + 1)

MAP_TYPE_DECLARE(dentry_t *);


// file ops
int initrd_open(file_t *file, dentry_t *dentry);
int initrd_flush(file_t *file);
ssize_t initrd_read(file_t *file, char *buf, size_t count, off_t *offset);

static file_ops_t initrd_file_ops = {
  .open = initrd_open,
  .flush = initrd_flush,
  .read = initrd_read,
};
static inode_ops_t initrd_inode_ops = {
  .mkdir = ramfs_mkdir,
};

static void *initrd_buffer;
static size_t initrd_size;

//

super_block_t *initrd_mount(file_system_t *fs, dev_t devid, blkdev_t *dev, dentry_t *mount) {
  kassert(initrd_buffer != NULL);
  kassert(initrd_size > 0);

  initrd_header_t *header = initrd_buffer;
  if (strncmp(header->signature, INITRD_SIGNATURE, 6) != 0) {
    kprintf("initrd: invalid signature\n");
    ERRNO = EINVAL;
    return NULL;
  }

  ramfs_super_t *rsb = kmalloc(sizeof(ramfs_super_t));
  bitmap_init(&rsb->inodes, RAMFS_MAX_FILES);
  spin_init(&rsb->lock);

  super_block_t *sb = kmalloc(sizeof(super_block_t));
  sb->flags = 0;
  sb->blksize = 1;
  sb->dev = dev;
  sb->devid = devid;
  sb->fs = fs;
  sb->ops = fs->sb_ops;
  sb->root = mount;
  sb->data = rsb;
  sb->inode_cache = create_rb_tree();

  // allocate the root inode
  inode_t *root = i_alloc(0, sb);
  if (root == NULL) {
    panic("failed to mount initrd");
  }

  root->mode = S_IFDIR | S_ISFLL;
  root->sb = sb;
  d_attach(mount, root);
  d_populate_dir(mount);
  rb_tree_insert(sb->inode_cache, root->ino, root);

  kprintf("initrd:\n");
  kprintf("  signature: %06s\n", header->signature);
  kprintf("  total size: %u\n", header->total_size);
  kprintf("  data offset: %u\n", header->total_size);
  kprintf("  entry count: %u\n", header->entry_count);

  void *initrd_data = offset_ptr(initrd_buffer, header->data_offset);
  ino_t initrd_next_inode = 1;
  hash_map_t *initrd_dentry_map = hash_map_new(); // path -> dentry lookup table

  // load all of the entries
  initrd_entry_t *entry = INITRD_FIRST_ENTRY(initrd_buffer);
  for (int i = 0; i < header->entry_count; i++) {
    kprintf("entry '%c' | path = '%s' [len = %u, offset = %u]\n", entry->entry_type, entry->path, entry->path_len, entry->data_offset);

    // get the parent inode
    path_t p = str_to_path(entry->path);
    kassert(!p_is_null(p) && !p_is_dot(p));

    path_t p_dirname = path_dirname(p);
    dentry_t *parent = mount;
    if (!p_is_slash(p_dirname)) {
      // non-root entry
      char *parent_path = path_to_str(p_dirname);
      parent = hash_map_get(initrd_dentry_map, parent_path);
      if (parent == NULL) {
        panic("failed to find parent inode for '%s'", parent_path);
      }
      kfree(parent_path);
    }

    path_t p_basename = path_basename(p);
    kassert(!p_is_null(p_basename) && !p_is_dot(p_basename));

    // create the inode and dentry

    inode_t *inode = i_alloc(initrd_next_inode++, sb);
    kassert(inode != NULL);
    inode->mode = S_ISFLL;
    inode->sb = sb;
    inode->size = entry->data_size;
    inode->data = offset_ptr(initrd_data, entry->data_offset);
    inode->blksize = 1;
    inode->blocks = entry->data_size;

    char *name = path_to_str(p_basename);
    dentry_t *dentry = d_alloc(parent, name);
    d_attach(dentry, inode);
    d_add_child(parent, dentry);
    kfree(name);

    switch (entry->entry_type) {
      case INITRD_ENT_FILE:
        inode->mode |= S_IFREG;
        dentry->mode |= S_IFREG;
        break;
      case INITRD_ENT_DIR:
        inode->mode |= S_IFDIR;
        dentry->mode |= S_IFDIR;
        d_populate_dir(dentry);
        break;
      case INITRD_ENT_LINK:
        inode->mode |= S_IFLNK;
        inode->size = entry->data_size;
        dentry->mode |= S_IFLNK;
        break;
    }

    // add the dentry to the lookup table
    hash_map_set_c(initrd_dentry_map, entry->path, dentry);
    entry = INITRD_NEXT_ENTRY(entry);
  }

  kprintf("initrd: mounted successfully\n");
  hash_map_free(initrd_dentry_map);
  return sb;
}

//

file_system_t initrd_file_system = {
  .name = "initrd",
  .flags = 0,
  .mount = initrd_mount,
  .inode_ops = &initrd_inode_ops,
  .file_ops = &initrd_file_ops,
  .dentry_ops = NULL,
};

void initrd_init() {
  initrd_file_system.sb_ops = ramfs_super_ops;

  if (boot_info_v2->initrd_addr != 0) {
    initrd_buffer = (void *) boot_info_v2->initrd_addr;
    initrd_size = boot_info_v2->initrd_size;

    if (fs_register(&initrd_file_system) < 0) {
      panic("failed to register initrd filesystem");
    }
  }
}
