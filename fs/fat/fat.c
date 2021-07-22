//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fat/fat.h>
#include <super.h>
#include <dentry.h>
#include <panic.h>
#include <printf.h>
#include <panic.h>
#include <thread.h>

#define fat_log(str, args...) kprintf("[fat] " str "\n", ##args)

typedef struct load_chunk {
  uint32_t cluster;
  uint32_t count;
  struct load_chunk *next;
} load_chunk_t;


const char *fat_types[] = {
  [FAT12] = "FAT12",
  [FAT16] = "FAT16",
  [FAT32] = "FAT32"
};

//

static inline uint32_t get_first_cluster(fat_dentry_t *file) {
  return ((uint32_t) file->fst_clus_hi << 16) | file->fst_clus_lo;
}

static inline uint32_t cluster_to_entry(fat_super_t *super, uint64_t n) {
  if (super->type == FAT16) {
    return ((uint16_t *) super->fat)[n];
  } else if (super->type == FAT32) {
    return ((uint32_t *) super->fat)[n] & 0x0FFFFFFF;
  }
  return 0;
}

static inline uint32_t cluster_to_sector(fat_super_t *super, uint64_t n) {
  return ((n - 2) * super->bpb->sec_per_clus) + super->first_sector;
}

static inline mode_t dirent_to_mode(fat_dentry_t *dentry) {
  if (dentry->attr & FAT_DIRECTORY) {
    return (mode_t) S_IFDIR;
  }
  return (mode_t) S_IFREG;
}

//

super_block_t *fat_mount(file_system_t *fs, blkdev_t *dev, dentry_t *mount) {
  fat_log("mount");


  void *boot_sec = blkdev_read(dev, 0, 1);
  if (boot_sec == NULL) {
    fat_log("failed to read boot sector");
    return NULL;
  }

  fat_bpb_t *bpb = boot_sec;
  fat32_ebpb_t *ebpb32 = (void *)((uintptr_t) bpb + sizeof(fat_bpb_t));
  if (ebpb32->sig_word == FAT_SIG_WORD) {
    // not a fat filesystem
    ERRNO = EINVAL;
    return NULL;
  }

  uint32_t fat_size = bpb->fat_sz_16 != 0 ? bpb->fat_sz_16 : ebpb32->fat_sz_32;
  uint32_t total_sectors = bpb->tot_sec_16 != 0 ? bpb->tot_sec_16 : bpb->tot_sec_32;
  uint32_t fat_sectors = (fat_size * bpb->num_fats) / bpb->byts_per_sec;
  uint32_t root_sectors = ((bpb->root_ent_cnt * 32) + (bpb->byts_per_sec - 1)) / bpb->byts_per_sec;
  uint32_t data_sectors = total_sectors - (bpb->rsvd_sec_cnt + (bpb->num_fats * fat_size) + root_sectors);
  uint32_t cluster_count = data_sectors / bpb->sec_per_clus;

  void *fat_sec = blkdev_read(dev, bpb->rsvd_sec_cnt, fat_sectors);
  if (fat_sec == NULL) {
    fat_log("failed to read file allocation table");
    return NULL;
  }

  uint32_t root_sec_num = bpb->rsvd_sec_cnt + (bpb->num_fats * fat_size);
  void *root_sec = blkdev_read(dev, root_sec_num, root_sectors);
  if (root_sec == NULL) {
    fat_log("failed to read root directory");
    return NULL;
  }

  fat_super_t *fsb = kmalloc(sizeof(fat_super_t ));
  if (cluster_count < 4085) {
    fsb->type = FAT12;
  } else if (cluster_count < 65525) {
    fsb->type = FAT16;
  } else {
    fsb->type = FAT32;
  }

  fat_log("volume type: %s", fat_types[fsb->type]);

  fsb->fat_size = fat_size;
  fsb->total_sectors = total_sectors;
  fsb->data_sectors = data_sectors;
  fsb->first_sector = bpb->rsvd_sec_cnt + (fat_size * bpb->num_fats) + root_sectors;
  fsb->cluster_count = cluster_count;

  fsb->bpb = boot_sec;
  fsb->fat = fat_sec;
  fsb->root = root_sec;

  super_block_t *sb = kmalloc(sizeof(super_block_t));
  sb->flags = 0;
  sb->blksize = bpb->byts_per_sec;
  sb->dev = dev;
  sb->ops = fs->sb_ops;
  sb->fs = fs;
  sb->data = fsb;

  if (fsb->type == FAT12 || fsb->type == FAT16) {
    fat_legacy_ebpb_t *ebpb = (void *)((uintptr_t) bpb + sizeof(fat_bpb_t));
    memcpy(sb->id, ebpb->vol_lab, 11);
  } else {
    memcpy(sb->id, ebpb32->vol_lab, 11);
  }

  return sb;
}
