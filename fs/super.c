//
// Created by Aaron Gill-Braun on 2021-07-16.
//

#include <super.h>
#include <mm.h>

#include <string.h>
#include <panic.h>
#include <printf.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("super: %s: " fmt, __func__, ##__VA_ARGS__)

#define ITABLE(sb) ((sb)->itable)
#define ITABLE_LOCK(sb) spin_lock(&ITABLE(sb)->lock)
#define ITABLE_UNLOCK(sb) spin_unlock(&ITABLE(sb)->lock)

// This is defined as an opaque type in fs_types.h because it should only be
// modified and accessed by this file.
struct itable {
  rb_tree_t *tree;
  spinlock_t lock;
};

//
// MARK: Superblock API
//

super_block_t *sb_alloc(fs_type_t *fs_type) {
  super_block_t *sb = kmalloc(sizeof(super_block_t));
  memset(sb, 0, sizeof(super_block_t));
  mutex_init(&sb->lock, MUTEX_REENTRANT);

  sb->itable = kmalloc(sizeof(struct itable));
  memset(sb->itable, 0, sizeof(struct itable));
  sb->itable->tree = create_rb_tree();
  spin_init(&sb->itable->lock);
  return sb;
}

void sb_free(super_block_t *sb) {
  ASSERT(sb->data == NULL);
  ASSERT(sb->ino_count == 0);
  ASSERT(ITABLE(sb)->tree->nodes == 0);

  kfree(ITABLE(sb)->tree->nil);
  kfree(ITABLE(sb)->tree);
  kfree(ITABLE(sb));
  kfree(sb);
}

int sb_takeown(super_block_t *sb, inode_t *inode) {
  ASSERT(inode->sb == NULL);
  S_LOCK(sb);
  I_LOCK(inode);
  inode->sb = sb;
  inode->ops = sb->fs->inode_ops;
  I_UNLOCK(inode);
  S_UNLOCK(sb);
  return 0;
}

int sb_add_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(inode->sb == NULL);
  ASSERT(inode->ops == NULL);
  if (rb_tree_find(sb->itable->tree, inode->ino) != NULL) {
    DPRINTF("duplicate inode %d already exists", inode->ino);
    return -1;
  }

  S_LOCK(sb);
  I_LOCK(inode);
  sb->ino_count++;
  inode->sb = sb;
  inode->ops = sb->fs->inode_ops;
  I_UNLOCK(inode);
  S_UNLOCK(sb);

  ITABLE_LOCK(sb);
  rb_tree_insert(sb->itable->tree, inode->ino, inode);
  ITABLE_UNLOCK(sb);
  return 0;
}

int sb_remove_inode(super_block_t *sb, inode_t *inode) {
  ASSERT(inode->sb == sb);
  ASSERT(inode->ops == sb->fs->inode_ops);
  if (rb_tree_find(sb->itable->tree, inode->ino) == NULL) {
    DPRINTF("inode %d not found in itable", inode->ino);
    return -1;
  }

  ITABLE_LOCK(sb);
  rb_tree_delete(sb->itable->tree, inode->ino);
  ITABLE_UNLOCK(sb);

  S_LOCK(sb);
  I_LOCK(inode);
  sb->ino_count--;
  inode->sb = NULL;
  inode->ops = NULL;
  I_UNLOCK(inode);
  S_UNLOCK(sb);
  return 0;
}

//
// MARK: Superblock Operations
//

// int sb_mount(inode_t *root) {
//   ASSERT(sb->fs != NULL);
//   // ASSERT(IS_IFDIR(mount))
//   super_block_t *sb = sb_alloc();
//   sb->mount = mount;
//   // ASSERT(S_OPS(sb)->sb_mount != NULL);
//
//   return -1;
// }

/**
 * Unmounts the superblock for a filesystem. \b Required.
 *
 * This is called when unmounting a filesystem and should perform any cleanup
 * of internal data. It does not need to sync the superblock or any inodes as
 * that is handled before this is called.
 *
 * @param sb The superblock being unmounted.
 * @return
 */
int sb_unmount(super_block_t *sb) {
  return -1;
}

/**
 * Write the superblock to the on-device filesystem. \b Required if not read-only.
 *
 * This should write the superblock to the on-device filesystem. It is called
 * when certain read-write fields change to sync the changes to disk.
 *
 * @param sb The superblock to write.
 * @return
 */
int sb_write(super_block_t *sb) {
  return -1;
}

/**
 * Reads an inode from the filesystem. \b Required.
 *
 * This should load the inode (specified by the given inode's ino field) from
 * the superblock and fill in the relevent read-write fields.
 *
 * \note The inode IFLOADED flag will be set after this function.
 *
 * @param sb The superblock to read the inode from.
 * @param inode [in,out] The inode to be filled in.
 * @return
 */
int sb_read_inode(super_block_t *sb, inode_t *inode) {
  return -1;
}

/**
 * Writes an inode to the on-device filesystem. \b Required if not read-only.
 *
 * This should write the given inode to the on-device superblock and it is called
 * when certain read-write fields change.
 *
 * \note The inode IFDIRTY flag will be cleared after this function.
 *
 * @param sb The superblock to write the inode to.
 * @param inode The inode to be writen.
 * @return
 */
int sb_write_inode(super_block_t *sb, inode_t *inode) {
  return -1;
}

/**
 * Allocates a new inode in the superblock. \b Required if not read-only.
 *
 * This should allocate a new inode in the superblock and then fill in the
 * provided inode with the ino number. It should not pre-allocate any blocks
 * for associated data.
 *
 * @param sb The superblock to allocate the inode in.
 * @param inode [in,out] The inode to be filled in.
 * @return
 */
int sb_alloc_inode(super_block_t *sb, inode_t *inode) {
  return -1;
}

/**
 * Deletes an inode from the superblock. \b Required if not read-only.
 *
 * This should delete the given inode from the superblock and release any data
 * data blocks still held by this inode. It should also assume that there are
 * no links to the inode and nlinks is 0.
 *
 * @param sb The superblock to write the inode to.
 * @param inode The inode to be deleted.
 * @return
 */
int sb_delete_inode(super_block_t *sb, inode_t *inode) {
  return -1;
}

