//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#include <fat/fat.h>


typedef enum {
  ENT_FREE,
  ENT_USED,
  ENT_RESVD,
  ENT_BAD,
  ENT_EOF,
} ent_type_t;

static inline ent_type_t entry_to_type(fat_super_t *super, uint32_t entry) {
  if (entry == 0) {
    return ENT_FREE;
  }

  uint32_t max = super->cluster_count + 1;
  if (super->type == FAT16) {
    if (entry == 0xFFFF) {
      return ENT_EOF;
    } else if (entry == 0xFFF7) {
      return ENT_BAD;
    } else if (entry >= max + 1) {
      return ENT_RESVD;
    }
  } else if (super->type == FAT32) {
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

static inline uint32_t get_first_cluster(fat_dentry_t *dentry) {
  return ((uint32_t) dentry->fst_clus_hi << 16) | dentry->fst_clus_lo;
}

static inline uint32_t cluster_to_entry(fat_super_t *super, uint32_t n) {
  if (super->type == FAT16) {
    return ((uint16_t *) super->fat)[n];
  } else if (super->type == FAT32) {
    return ((uint32_t *) super->fat)[n] & 0x0FFFFFFF;
  }
  return 0;
}

fat_dentry_t *fat_get_dirent(fat_super_t *super, fat_dentry_t *dentry, ino_t ino) {
  fat_dentry_t *ent = dentry;
  while (ent->name[0] != 0) {
    uint32_t n = get_first_cluster(ent);
    if (n == ino) {
      return ent;
    }
    ent++;
  }
  return NULL;
}


//

int fat_create(inode_t *dir, dentry_t *dentry, mode_t mode) {
  fat_super_t *fsb = dir->sb->data;


  // uint32_t n = 0;
  // while (true) {
  //   uint32_t entry = cluster_to_entry(fsb, n);
  //   if (entry_to_type(fsb, entry) == ENT_FREE) {
  //
  //   }
  // }
}

dentry_t *fat_lookup(inode_t *dir, const char *name, bool filldir) {

}

int fat_link(inode_t *dir, dentry_t *old_dentry, dentry_t *dentry) {

}

int fat_unlink(inode_t *dir, dentry_t *dentry) {

}

int fat_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode) {

}

int fat_rmdir(inode_t *dir, dentry_t *dentry) {

}

int fat_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev) {

}

// int (*rename)(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry);

int fat_readlink(dentry_t *dentry, char *buffer, int buflen) {

}

void fat_truncate(inode_t *inode) {

}

