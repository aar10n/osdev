//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#ifndef FS_FAT_H
#define FS_FAT_H

#include <base.h>
#include <blkdev.h>
#include <fs.h>

#define FAT_SIG_WORD 0x55AA
#define FAT_BACKUP_BPB_SECT 6

typedef enum {
  FAT12,
  FAT16,
  FAT32,
} fat_volume_type_t;

#define FAT_LAST_LONG_ENTRY 0x40

// FAT file attributes
#define FAT_READ_ONLY 0x01
#define FAT_HIDDEN    0x02
#define FAT_SYSTEM    0x04
#define FAT_VOLUME_ID 0x08
#define FAT_DIRECTORY 0x10
#define FAT_ARCHVE    0x20

#define FAT_LONG_NAME \
  (FAT_READ_ONLY | FAT_HIDDEN | FAT_SYSTEM | FAT_VOLUME_ID)

//
// Common FAT structures
//

// valid `media` field values:
//   0xF0, 0xF8, 0xF9, 0xFA, 0xFB,
//   0xFC, 0xFD, 0xFE, 0xFF
//
//   0xF8 -> "fixed" (non-removable) media
//

// BPB common to all FAT volumes
typedef struct packed {
  uint8_t bs_jmp_boot[3]; // jump instructions to boot code
  char oem_name[8];       // oem name identifier
  uint16_t byts_per_sec;  // number of bytes per sector
  uint8_t sec_per_clus;   // number of sectors per cluster
  uint16_t rsvd_sec_cnt;  // number of sectors in reserved region
  uint8_t num_fats;       // number of file allocation tables (FATs)
  uint16_t root_ent_cnt;  // number of root dirents (FAT12 and FAT16 only)
  uint16_t tot_sec_16;    // 16-bit total sector count (can be 0 for FAT32)
  uint8_t media;          // volume media type
  uint16_t fat_sz_16;     // 16-bit count of sectors occupied by one FAT (FAT12/FAT16 only)
  uint16_t sec_per_trk;   // sectors per track for interrupt 0x13
  uint16_t num_heads;     // number of heads for interrupt 0x13
  uint32_t hidd_sec;      // count of hidden sectors preceding this FAT volume
  uint32_t tot_sec_32;    // 32-bit total count of sectors of this volume
} fat_bpb_t;

typedef struct packed {
  char name[11];          // short file name
  uint8_t attr;           // file attributes
  uint8_t ntres;          // ? (set to 0)
  uint8_t crt_time_tenth; // creation time (tenths of second)
  uint16_t crt_time;      // creation time (gran of 2 seconds)
  uint16_t crt_date;      // creation date
  uint16_t lst_acc_date;  // last access date
  uint16_t fst_clus_hi;   // high word of first data cluster (FAT32 only)
  uint16_t wrt_time;      // last modification (write) time
  uint16_t wrt_date;      // last modification (write) date
  uint16_t fst_clus_lo;   // low word of first data cluster (FAT32 only)
  uint32_t file_size;     // 32-bit file size in bytes
} fat_dirent_t;

// long name directory entry
typedef struct packed {
  uint8_t order;        // the order of this entry in the name
  char name1[10];       // a portion of the file name (part 1)
  uint8_t type;         // must be set to 0
  uint8_t chksum;       // checksum of short file name
  char name2[12];       // a portion of the file name (part 2)
  uint16_t fst_clus_lo; // must be set to 0
  char name3[4];        // a portion of the file name (part 3)
} fat_lname_dirent_t;

//
// FAT12/FAT16
//

// `boot_sig` field:
//   0x29 -> either of the following two fields are non-zero

// Extended BPB for FAT12 and FAT16 volumes
typedef struct packed {
  uint8_t drv_num;        // drive number for interrupt 0x13
  uint8_t reserved1;      // reserved - set to 0
  uint8_t boot_sig;       // extended boot signature
  uint32_t vol_id;        // volume serial number
  char vol_lab[11];       // volume label (default: "NO NAME ")
  char fil_sys_type[8];   // one of the strings: "FAT12 ", "FAT16 " or "FAT "
  uint8_t reserved2[448]; // reserved - set to 0
  uint16_t sig_word;      // signature word (0x55 and 0xAA)
  // if byts_per_sec > 512 the remainder of the
  // sector is also reserved and set to 0
} fat_legacy_ebpb_t;

//
// FAT32
//

typedef union packed {
  uint16_t raw;
  struct {
    uint16_t active_fat : 4; // index of active FAT (valid if mirrored = 1)
    uint16_t : 3;            // reserved
    uint16_t fat_mode : 1;   // 0 = mirrored | 1 = one FAT active
    uint16_t : 8;            // reserved
  };
} fat32_ext_flags;

// `boot_sig` field:
//   0x29 -> either of the following two fields are non-zero

// Extended BPB for FAT32 volumes
typedef struct packed {
  uint32_t fat_sz_32;        // 32-bit count of sectors occupied by one FAT
  fat32_ext_flags ext_flags; // extended flags
  uint16_t fs_ver;           // high byte = major rev num, low byte = minor rev num
  uint32_t root_clus;        // cluster number of first cluster of root directory (minimum 2)
  uint16_t fs_info;          // sector number of fsinfo structure
  uint16_t bk_boot_sec;      // set to 0 or 6 (non-zero indicates sector number of copy)
  uint8_t reserved0[12];     // reserved - set to 0
  uint8_t drv_num;           // drive number for interrupt 0x13
  uint8_t reserved1;         // reserved - set to 0
  uint8_t boot_sig;          // extended boot signature
  uint32_t vol_id;           // volume serial number
  char vol_lab[11];          // volume label
  char fil_sys_type[8];      // human readable string
  uint8_t reserved2[420];    // reserved - set to 0
  uint16_t sig_word;         // signature word (0x55 and 0xAA)
} fat32_ebpb_t;

//

typedef struct {
  fat_volume_type_t type;
  uint32_t fat_size;
  uint32_t total_sectors;
  uint32_t data_sectors;
  uint32_t cluster_count;

  fat_bpb_t *bpb;
  void *fat;
  fat_dirent_t *root;
} fs_fat_t;

//
// Common functions
//

fs_t *fat_mount(blkdev_t *dev, fs_node_t *mount);
inode_t *fat_locate(fs_t *fs, inode_t *parent, ino_t ino);

ssize_t fat_read(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);

#endif
