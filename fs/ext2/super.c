//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#include <ext2/ext2.h>
#include <inode.h>
#include <thread.h>
#include <asm/bits.h>
#include <panic.h>

#define ext2sb(super) ((ext2_data_t *)((super)->data))
typedef LIST_HEAD(ext2_load_chunk_t) chunk_list_t;

static inline mode_t ext2_mode_convert(uint16_t i_mode) {
  mode_t mode = 0;
  if (i_mode & EXT2_S_IFSOCK) {
    mode |= S_IFSOCK;
  }
  if (i_mode & EXT2_S_IFLNK) {
    mode |= S_IFLNK;
  }
  if (i_mode & EXT2_S_IFREG) {
    mode |= S_IFREG;
  }
  if (i_mode & EXT2_S_IFBLK) {
    mode |= S_IFBLK;
  }
  if (i_mode & EXT2_S_IFDIR) {
    mode |= S_IFDIR;
  }
  if (i_mode & EXT2_S_IFCHR) {
    mode |= S_IFCHR;
  }
  if (i_mode & EXT2_S_IFIFO) {
    mode |= S_IFIFO;
  }
  return mode;
}

static inline uint32_t ino_to_table_block(ext2_data_t *data, ino_t ino) {
  ext2_super_t *esb = data->sb;

  uint32_t block_group = (ino - 1) / esb->s_inodes_per_group;
  uint32_t index = (ino - 1) % esb->s_inodes_per_group;

  ext2_bg_desc_t *bg = &data->bgdt[block_group];
  uint32_t block_offset = (index * esb->s_inode_size) / (1024 << esb->s_log_block_size);
  return bg->bg_inode_table + block_offset;
}

static uint32_t ino_to_block_offset(ext2_data_t *data, ino_t ino) {
  ext2_super_t *esb = data->sb;
  uint32_t inodes_per_block = (1024 << esb->s_log_block_size) / esb->s_inode_size;
  uint32_t index = (ino - 1) % data->sb->s_inodes_per_group;
  return index % inodes_per_block;
  // return (ino - 1) % data->sb->s_inodes_per_group;
}

//

static int direct_load_chunks(const uint32_t *blocks, size_t len, chunk_list_t *chunks) {
  // direct blocks
  uint32_t start = 0;
  uint32_t last = 0;
  for (int i = 0; i < len; i++) {
    uint32_t b = blocks[i];
    if (b == 0) {
      break;
    }

    if (start == 0) {
      start = b;
      last = b;
      continue;
    } else if (b == last + 1) {
      last++;
      continue;
    }

    ext2_load_chunk_t *chunk = kmalloc(sizeof(ext2_load_chunk_t));
    memset(chunk, 0, sizeof(ext2_load_chunk_t));
    chunk->start = start;
    chunk->len = last - start;
    LIST_ADD(chunks, chunk, chunks);
    start = 0;
    last = 0;
  }

  if (start != 0) {
    ext2_load_chunk_t *chunk = kmalloc(sizeof(ext2_load_chunk_t));
    memset(chunk, 0, sizeof(ext2_load_chunk_t));
    chunk->start = start;
    chunk->len = last == start ? 1 : last - start;
    LIST_ADD(chunks, chunk, chunks);
  }

  return 0;
}

static int indirect_load_chunks(super_block_t *sb, uint32_t block, chunk_list_t *chunks) {
  if (block == 0) {
    return 0;
  }

  uint32_t *blocks = EXT2_READX(sb, block, 1, BLKDEV_NOCACHE);
  if (blocks == NULL) {
    ERRNO = EFAILED;
    return -1;
  }

  chunk_list_t indirect_chunks;
  LIST_INIT(&indirect_chunks);
  int result = direct_load_chunks(blocks, sb->blksize / sizeof(uint32_t), &indirect_chunks);
  LIST_CONCAT(chunks, LIST_FIRST(&indirect_chunks), LIST_LAST(&indirect_chunks), chunks);
  blkdev_freebuf(blocks);
  return result;
}

static int double_indirect_load_chunks(super_block_t *sb, uint32_t block, chunk_list_t *chunks) {
  if (block == 0) {
    return 0;
  }

  uint32_t *blocks = EXT2_READX(sb, block, 1, BLKDEV_NOCACHE);
  if (blocks == NULL) {
    ERRNO = EFAILED;
    return -1;
  }

  chunk_list_t double_chunks;
  LIST_INIT(&double_chunks);
  for (int i = 0; i < sb->blksize / sizeof(uint32_t); i++) {
    if (blocks[i] == 0) {
      break;
    }

    int result = indirect_load_chunks(sb, blocks[i], &double_chunks);
    if (result < 0) {
      break;
    }
    LIST_CONCAT(chunks, LIST_FIRST(&double_chunks), LIST_LAST(&double_chunks), chunks);
  }

  blkdev_freebuf(blocks);
  return 0;
}

static ext2_load_chunk_t *inode_to_load_chunks(super_block_t *sb, ext2_inode_t *inode) {
  chunk_list_t chunks;
  LIST_INIT(&chunks);

  direct_load_chunks(inode->i_block, 12, &chunks);
  indirect_load_chunks(sb, inode->i_block[12], &chunks);
  double_indirect_load_chunks(sb, inode->i_block[13], &chunks);
  if (inode->i_block[14] != 0) {
    panic("triple indirect blocks not supported");
  }
  return LIST_FIRST(&chunks);
}

//

inode_t *ext2_alloc_inode(super_block_t *sb) {
  ext2_data_t *data = ext2sb(sb);
  ext2_super_t *esb = data->sb;

  ext2_bg_desc_t *bg = NULL;
  for (uint32_t i = 0; data->bg_count; i++) {
    ext2_bg_desc_t *b = &data->bgdt[i];
    if (b->bg_free_inodes_count > 0) {
      b->bg_free_inodes_count--;
      bg = b;
      break;
    }
  }

  if (bg == NULL) {
    ERRNO = ENOSPC;
    return NULL;
  }

  uint64_t *inode_bmp = EXT2_READ(sb, bg->bg_inode_bitmap, 1);
  if (inode_bmp == NULL) {
    return NULL;
  }

  uint32_t max_index = esb->s_blocks_per_group / 64;
  uint32_t ino = 0;
  for (uint32_t i = 0; i < max_index; i++) {
    uint64_t v = inode_bmp[i];
    if (v == UINT64_MAX) {
      continue;
    } else if (v == 0) {
      ino = i * 64;
      inode_bmp[i] = 1;
      break;
    }

    uint8_t off = __bsf64(~v);
    ino = (i * 64) + off;
    inode_bmp[i] |= (1 << off);
    break;
  }

  if (EXT2_WRITE(sb, bg->bg_inode_bitmap, 1, inode_bmp) < 0) {
    // in future mark sb as dirty
    panic("failed to write");
  }
  return i_alloc(ino, sb);
}

int ext2_destroy_inode(super_block_t *sb, inode_t *inode) {
  // ext2_data_t *data = ext2sb(sb);
  // ext2_super_t *esb = data->sb;
  //
  // uint64_t block_group = (inode->ino - 1) / esb->s_inodes_per_group;
  // ext2_bg_desc_t *bg = &data->bgdt[block_group];
  // uint64_t *inode_bmp = ext2_read(sb, bg->bg_inode_bitmap, 1);
  //

  return -1;
}

int ext2_read_inode(super_block_t *sb, inode_t *inode) {
  ext2_data_t *data = ext2sb(sb);
  ext2_super_t *esb = data->sb;
  ino_t ino = inode->ino;

  uint32_t block_group = (ino - 1) / esb->s_inodes_per_group;
  uint32_t group_index = (ino - 1) % esb->s_inodes_per_group;
  uint32_t inodes_per_block = (1024 << esb->s_log_block_size) / esb->s_inode_size;
  uint32_t table_block = group_index / inodes_per_block;
  uint32_t local_index = group_index % inodes_per_block;

  ext2_bg_desc_t *bg = &data->bgdt[block_group];
  void *table = EXT2_READ(sb, bg->bg_inode_table + table_block, 1);
  if (table == NULL) {
    ERRNO = EFAILED;
    return -1;
  }

  ext2_inode_t *e2i = offset_ptr(table, local_index * esb->s_inode_size);
  inode->mode = ext2_mode_convert(e2i->i_mode);
  inode->nlink = e2i->i_links_count;
  inode->uid = e2i->i_uid;
  inode->gid = e2i->i_gid;
  inode->size = e2i->i_size;
  inode->dev = sb->devid;
  inode->atime = e2i->i_atime;
  inode->ctime = e2i->i_ctime;
  inode->mtime = e2i->i_mtime;
  inode->blksize = sb->blksize;

  ext2_load_chunk_t *chunks = inode_to_load_chunks(sb, e2i);
  inode->data = chunks;
  return 0;
}

int ext2_write_inode(super_block_t *sb, inode_t *inode) {
  ext2_data_t *data = ext2sb(sb);
  uint32_t table_block = ino_to_table_block(data, inode->ino);
  uint32_t index = ino_to_block_offset(data, inode->ino);

  ext2_inode_t *table = EXT2_READ(sb, table_block, 1);
  if (table == NULL) {
    ERRNO = EFAILED;
    return -1;
  }

  ext2_inode_t *e2i = &table[index];
  e2i->i_mode = inode->mode;
  e2i->i_links_count = inode->nlink;
  e2i->i_uid = inode->uid;
  e2i->i_gid = inode->gid;
  e2i->i_size = inode->size;
  e2i->i_atime = inode->atime;
  e2i->i_ctime = inode->ctime;
  e2i->i_mtime = inode->mtime;
  sb->blksize = inode->blksize;

  if (EXT2_WRITE(sb, table_block, 1, table) < 0) {
    ERRNO = EFAILED;
    return -1;
  }
  return 0;
}
