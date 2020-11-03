//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fat/fat.h>

// Lookup table for disk size to sectors per cluster

typedef struct dsksz_to_secperclus {
  uint32_t disk_size;
  uint8_t sec_per_clus;
} dsksz_to_secperclus_t;

// Table for FAT16 drives.
dsksz_to_secperclus_t dsk_table_fat16[] = {
  {8400, 0},      // disks up to 4.1 MB (invalid)
  {32680, 2},     // disks up to 16 MB, 1k cluster
  {262144, 4},    // disks up to 128 MB, 2k cluster
  {524288, 8},    // disks up to 256 MB, 4k cluster
  {1048576, 16},  // disks up to 512 MB, 8k cluster
  // entries after this arent used unless FAT16 is forced
  {2097152, 32},  // disks up to 1 GB, 16k cluster
  {4194304, 64},  // disks up to 2 GB, 32k cluster
  {0xFFFFFFFF, 0} // disks greater than 2 GB (invalid)
};

dsksz_to_secperclus_t disk_table_fat32[] = {
  {66600, 0},       // disks up to 32.5 MB (invalid)
  {532480, 1},      // disks up to 260 MB, 0.5k cluster
  {16777216, 8},    // disks up to 8 GB, 4k cluster
  {33554432, 16},   // disks up to 16 GB, 8k cluster
  {67108864, 32},   // disks up to 32 GB, 16k cluster
  {0xFFFFFFFF, 64}, // disks greater than 32 GB, 32k cluster
};

//

uint32_t cluster_to_fat_entry(uint8_t *boot_sector, uint32_t cluster) {
  fat_volume_type_t type = fat_get_volume_type(boot_sector);
  fat_bpb_t *bpb = (void *) boot_sector;
  fat32_ebpb_t *ebpb32 = (void *)(boot_sector + sizeof(fat_bpb_t));

  uint32_t fat_size = bpb->fat_sz_16 != 0 ? bpb->fat_sz_16 : ebpb32->fat_sz_32;
  uint32_t fat_offset = type == FAT16 ? cluster * 2 : type == FAT32 ? cluster * 4 : 0;

  // for any other FAT table:
  //   uint32_t other_fat_sec_num = (fat_num * fat_sz) + fat_sec_num;

  uint32_t fat_sec_num = bpb->rsvd_sec_cnt + (fat_offset / bpb->byts_per_sec);
  uint32_t fat_ent_offset = fat_offset % bpb->byts_per_sec;
  return fat_ent_offset;
}

fat_volume_type_t fat_get_volume_type(uint8_t *boot_sector) {
  fat_bpb_t *bpb = (void *) boot_sector;
  fat32_ebpb_t *ebpb32 = (void *)(boot_sector + sizeof(fat_bpb_t));

  // number of sectors occupied by the root directory
  uint32_t root_sectors = ((bpb->root_ent_cnt * 32) +(bpb->byts_per_sec - 1)) / bpb->byts_per_sec;

  uint32_t fat_size = bpb->fat_sz_16 != 0 ? bpb->fat_sz_16 : ebpb32->fat_sz_32;
  uint32_t total_sectors = bpb->tot_sec_16 != 0 ? bpb->tot_sec_16 : bpb->tot_sec_32;
  uint32_t data_sectors = total_sectors - (bpb->rsvd_sec_cnt + (bpb->num_fats * fat_size) + root_sectors);

  // A FAT12 volume cannot contain more than 4084 clusters.
  // A FAT16 volume cannot contain less than 4085 clusters or more than 65,524 clusters.
  uint32_t cluster_count = data_sectors / bpb->sec_per_clus;
  if (cluster_count < 4085) {
    return FAT12;
  } else if (cluster_count < 65525) {
    return FAT16;
  } else {
    return FAT32;
  }
}
