//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fat/fat.h>
#include <fs/blkdev.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <printf.h>
#include <mm.h>
#include <panic.h>

#define fat_log(str, args...) kprintf("[fat] " str "\n", ##args)

typedef struct load_chunk {
  uint32_t cluster;
  uint32_t count;
  struct load_chunk *next;
} load_chunk_t;

typedef enum {
  ENT_FREE,
  ENT_USED,
  ENT_RESVD,
  ENT_BAD,
  ENT_EOF,
} ent_type_t;


const char *fat_types[] = {
  [FAT12] = "FAT12",
  [FAT16] = "FAT16",
  [FAT32] = "FAT32"
};

//

static inline uint32_t get_first_cluster(fat_dirent_t *file) {
  return ((uint32_t) file->fst_clus_hi << 16) | file->fst_clus_lo;
}

static inline uint32_t cluster_to_entry(fs_fat_t *fatfs, uint64_t n) {
  if (fatfs->type == FAT16) {
    return ((uint16_t *) fatfs->fat)[n];
  } else if (fatfs->type == FAT32) {
    return ((uint32_t *) fatfs->fat)[n] & 0x0FFFFFFF;
  }
  return 0;
}

static inline ent_type_t entry_to_type(fs_fat_t *fatfs, uint32_t entry) {
  if (entry == 0) {
    return ENT_FREE;
  }

  uint32_t max = fatfs->cluster_count + 1;
  if (fatfs->type == FAT16) {
    if (entry == 0xFFFF) {
      return ENT_EOF;
    } else if (entry == 0xFFF7) {
      return ENT_BAD;
    } else if (entry >= max + 1) {
      return ENT_RESVD;
    }
  } else if (fatfs->type == FAT32) {
    if (entry == 0xFFFFFFFF) {
      return ENT_EOF;
    } else if (entry == 0xFFFFFF7) {
      return ENT_BAD;
    } else if (entry >= max + 1) {
      return ENT_RESVD;
    }
  }
  return ENT_USED;
}

static inline mode_t dirent_to_mode(fat_dirent_t *dirent) {
  if (dirent->attr & FAT_DIRECTORY) {
    return (mode_t) S_IFDIR;
  }
  return (mode_t) S_IFREG;
}

//

void fat_print_clusters(fs_t *fs, fat_dirent_t *file) {
  fs_fat_t *fatfs = fs->data;

  bool first = true;
  uint32_t n = get_first_cluster(file);
  while (true) {
    if (n < 2) {
      return;
    }

    if (first) {
      first = false;
      kprintf("%d", n);
    } else {
      kprintf(" -> %d", n);
    }

    uint32_t ent = cluster_to_entry(fatfs, n);
    if (fatfs->type == FAT16) {
      if (ent == 0xFFFF) {
        kprintf("\n");
        return;
      }

      n = ent;
    } else if (fatfs->type == FAT32) {
      if (ent == 0xFFFFFFFF) {
        kprintf("\n");
        return;
      }
      n = ent;
    }
  }
}

void fat_print_dir(fs_t *fs, fat_dirent_t *dir, int indent) {
  fs_fat_t *fatfs = fs->data;

  char space[indent + 1];
  space[indent] = '\0';
  for (int i = 0; i < indent; i++) {
    space[i] = ' ';
  }

  fat_dirent_t *dirent = dir;
  while (dirent->name[0] != 0) {
    kprintf("%s%s  (%d) %X\n", space, dirent->name, dirent->file_size, dirent->attr);
    uint32_t n = ((uint32_t) dirent->fst_clus_hi << 16) | dirent->fst_clus_lo;
    if (n < 2) {
      dirent++;
      continue;
    }

    fat_print_clusters(fs, dirent);

    uint32_t count = max(dirent->file_size / SEC_SIZE, 1);

    if (dirent->attr & FAT_DIRECTORY) {
      void *next_dir = blkdev_read(fs->device, dirent->fst_clus_lo, count);
      fat_print_dir(fs, next_dir, indent + 2);
    } else if (dirent->attr & FAT_VOLUME_ID) {

    } else if (dirent->file_size > 0 && dirent->file_size <= 1024) {
      void *file = blkdev_read(fs->device, dirent->fst_clus_lo, count);
      uint8_t *buffer = file;
      for (int i = 0; i < dirent->file_size; i++) {
        kprintf("%02X ", buffer[i]);
        if (i != 0 && i % 16 == 0) {
          kprintf("\n");
        } else if (i != 0 && i % 4 == 0) {
          kprintf(" ");
        }
      }
      kprintf("\n");
    }
    dirent++;
  }
}

load_chunk_t *fat_get_load_chunks(fs_t *fs, fat_dirent_t *file, uint32_t *sec_count) {
  fs_fat_t *fatfs = fs->data;
  fat_bpb_t *bpb = fatfs->bpb;

  uint32_t n = get_first_cluster(file);
  if (n < 2) {
    return NULL;
  }

  uint32_t total_count = 0;
  load_chunk_t *first = NULL;
  load_chunk_t *chunk = NULL;

  uint32_t cluster = n;
  uint32_t count = 1;
  while (true) {
    uint32_t ent = cluster_to_entry(fatfs, n);
    ent_type_t type = entry_to_type(fatfs, ent);
    total_count += bpb->sec_per_clus;

    if (ent == n + 1) {
      count++;
      n = ent;
      continue;
    }

    load_chunk_t *chk = kmalloc(sizeof(load_chunk_t));
    chk->cluster = cluster;
    chk->count = count;
    chk->next = NULL;
    if (chunk == NULL) {
      first = chk;
    } else {
      chunk->next = chk;
    }
    chunk = chk;

    if (type == ENT_EOF)
      break;

    cluster = ent;
    count = 1;
  }

  kprintf("total count: %d\n", total_count);
  *sec_count = total_count;
  return first;
}

fat_dirent_t *fat_get_dirent(fs_t *fs, fat_dirent_t *dir, ino_t ino) {
  fat_dirent_t *ent = dir;
  while (ent->name[0] != 0) {
    uint32_t n = get_first_cluster(ent);
    if (n == ino) {
      return ent;
    }
    ent++;
  }
  return NULL;
}

int fat_load_file(fs_t *fs, inode_t *inode) {
  blkdev_t *dev = fs->device;
  fs_fat_t *fatfs = fs->data;
  if (IS_LOADED(inode->mode)) {
    return 0;
  }

  size_t size = align(inode->size, PAGE_SIZE);
  page_t *buffer = alloc_pages(SIZE_TO_PAGES(size), PE_WRITE);

  uintptr_t ptr = buffer->addr;
  load_chunk_t *chunk = inode->data;
  while (chunk != NULL) {
    size_t sectors = chunk->count * fatfs->bpb->sec_per_clus;
    if (blkdev_readbuf(dev, chunk->cluster, sectors, (void *) ptr) != 0) {
      kprintf("[fat] failed to load file\n");
      return -EIO;
    }

    ptr += chunk->count * SEC_SIZE;
    load_chunk_t *next = chunk->next;
    kfree(chunk);
    chunk = next;
  }

  inode->data = (void *) buffer->addr;
  inode->mode |= S_ISLDD;
  return 0;
}

//

// Mounting Steps:
//  - collect basic info (free/used)
//  - load root directory

// fs_t *(*mount)(blkdev_t *device, fs_node_t *mount);
// int (*unmount)(fs_t *fs, fs_node_t *mount);
//
// inode_t *(*locate)(fs_t *fs, inode_t *parent, ino_t ino);
// inode_t *(*create)(fs_t *fs, mode_t mode);
// int (*remove)(fs_t *fs, inode_t *inode);
// dirent_t *(*link)(fs_t *fs, inode_t *inode, inode_t *parent, char *name);
// int (*unlink)(fs_t *fs, inode_t *inode, dirent_t *dirent);
// int (*update)(fs_t *fs, inode_t *inode);
//
// ssize_t (*read)(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
// ssize_t (*write)(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
// int (*sync)(fs_t *fs);

// fs_t *(*mount)(blkdev_t *device, fs_node_t *mount);
fs_t *fat_mount(blkdev_t *dev, fs_node_t *mount) {
  fat_log("mount");

  void *boot_sec = blkdev_read(dev, 0, 1);
  if (boot_sec == NULL) {
    fat_log("failed to read boot sector");
    return NULL;
  }

  fat_bpb_t *bpb = boot_sec;
  fat32_ebpb_t *ebpb32 = (void *)((uintptr_t) bpb + sizeof(fat_bpb_t));

  uint32_t fat_size = bpb->fat_sz_16 != 0 ? bpb->fat_sz_16 : ebpb32->fat_sz_32;
  uint32_t total_sectors = bpb->tot_sec_16 != 0 ? bpb->tot_sec_16 : bpb->tot_sec_32;
  uint32_t fat_sectors = (fat_size * bpb->num_fats) / bpb->byts_per_sec;
  uint32_t root_sectors = ((bpb->root_ent_cnt * 32) + (bpb->byts_per_sec - 1)) / bpb->byts_per_sec;
  uint32_t data_sectors = total_sectors - (bpb->rsvd_sec_cnt + (bpb->num_fats * fat_size) + root_sectors);
  uint32_t cluster_count = data_sectors / bpb->sec_per_clus;

  fs_fat_t *fatfs = kmalloc(sizeof(fs_fat_t));
  if (cluster_count < 4085) {
    fatfs->type = FAT12;
  } else if (cluster_count < 65525) {
    fatfs->type = FAT16;
  } else {
    fatfs->type = FAT32;
  }

  fatfs->fat_size = fat_size;
  fatfs->total_sectors = total_sectors;
  fatfs->data_sectors = data_sectors;
  fatfs->cluster_count = cluster_count;

  fat_log("volume type: %s", fat_types[fatfs->type]);

  void *fat_sec = blkdev_read(dev, bpb->rsvd_sec_cnt, fat_sectors);
  if (fat_sec == NULL) {
    fat_log("failed to read file allocation table");
    kfree(fatfs);
    return NULL;
  }

  uint32_t root_sec_num = bpb->rsvd_sec_cnt + (bpb->num_fats * fat_size);
  void *root_sec = blkdev_read(dev, root_sec_num, root_sectors);
  if (root_sec == NULL) {
    fat_log("failed to read root directory");
    kfree(fatfs);
    return NULL;
  }

  fatfs->bpb = boot_sec;
  fatfs->fat = fat_sec;
  fatfs->root = root_sec;

  fs_t *fs = kmalloc(sizeof(fs_t));
  fs->device = dev;
  fs->mount = mount;
  fs->data = fatfs;

  return fs;
}

// int (*unmount)(fs_t *fs, fs_node_t *mount);
int fat_unmount(fs_t *fs) {
  fat_log("unmount");
  return 0;
}

// inode_t *(*locate)(fs_t *fs, inode_t *parent, ino_t ino);
inode_t *fat_locate(fs_t *fs, inode_t *parent, ino_t ino) {
  blkdev_t *dev = fs->device;
  fs_fat_t *fatfs = fs->data;

  fat_dirent_t *parent_dir;
  if (parent == NULL) {
    parent_dir = fatfs->root;
  } else if (IS_LOADED(parent->mode)) {
    parent_dir = parent->data;
  } else {
    panic("parent not loaded");
  }

  fat_dirent_t *ent = fat_get_dirent(fs, parent_dir, ino);
  if (ent == NULL) {
    return NULL;
  }

  uint32_t sectors;
  load_chunk_t *chunks = fat_get_load_chunks(fs, ent, &sectors);

  inode_t *inode = kmalloc(sizeof(inode_t));
  memset(inode, 0, sizeof(inode_t));
  inode->ino = ino;
  inode->mode = dirent_to_mode(ent);
  inode->size = ent->file_size > 0 ? ent->file_size : sectors * SEC_SIZE;
  inode->blocks = sectors;
  inode->blksize = fatfs->bpb->byts_per_sec;
  inode->data = chunks;
  return inode;
}

// inode_t *(*create)(fs_t *fs, mode_t mode);

// int (*remove)(fs_t *fs, inode_t *inode);

// dirent_t *(*link)(fs_t *fs, inode_t *inode, inode_t *parent, char *name);

// int (*unlink)(fs_t *fs, inode_t *inode, dirent_t *dirent);

// int (*update)(fs_t *fs, inode_t *inode);


// ssize_t (*read)(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
ssize_t fat_read(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf) {
  if (!IS_LOADED(inode->mode)) {
    if (fat_load_file(fs, inode) != 0) {
      kprintf("[fat] failed to read file\n");
      return -EIO;
    }
  }

  if (offset >= inode->size) {
    return 0;
  }

  off_t available = inode->size - offset;
  ssize_t bytes = min(available, nbytes);
  void *buffer = offset_ptr(inode->data, offset);
  memcpy(buf, buffer, bytes);
  return bytes;
}

// ssize_t (*write)(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
// int (*sync)(fs_t *fs);
