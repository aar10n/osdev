//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#ifndef FS_FAT_FAT12_H
#define FS_FAT_FAT12_H

#include <base.h>

#define FAT12_CLUSTER_FREE  0x000
#define FAT12_CLUSTER_BAD   0xFF7
#define FAT12_CLUSTER_FINAL 0xFFF

typedef union packed {
 uint32_t raw : 24;
  struct packed {
    uint16_t ent1 : 12;
    uint16_t ent2 : 12;
  };
} fat12_packed_ent_t;
static_assert(sizeof(fat12_packed_ent_t) == 3);

void fat12_print_fat(void *buf);

#endif
