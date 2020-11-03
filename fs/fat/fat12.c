//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#include <fat/fat.h>
#include <fat/fat12.h>
#include <stdio.h>


uint32_t fat12_cluster_to_fat(fat_bpb_t *bpb, uint32_t n) {
  uint32_t fat_offset = n + (n / 2);
  uint32_t sec_num = bpb->rsvd_sec_cnt + (fat_offset / bpb->byts_per_sec);
  uint32_t fat_ent_offset = fat_offset % bpb->byts_per_sec;

  kprintf("[fat12] cluster: %d\n", n);
  kprintf("[fat12] sec_num: %d\n", sec_num);
  kprintf("[fat12] fat_ent_offset: %d\n", fat_ent_offset);

  return 0;
}

void fat12_print_fat(void *buf) {
  fat_bpb_t *bpb = buf;

  uint8_t *fat = (buf + sizeof(fat_bpb_t));

  uint32_t root_dir_sec = bpb->rsvd_sec_cnt + (bpb->num_fats * bpb->fat_sz_16);

  uint16_t fat_ent_cnt = (bpb->fat_sz_16 * 512) / 1.5;
  kprintf("[fat12] fat entries: %d\n", fat_ent_cnt);
  kprintf("[fat12] root dir sec: %d\n", root_dir_sec);
  for (int i = 0; i < (bpb->fat_sz_16 * 512) / 3; i += 3) {
    fat12_packed_ent_t *ents = (void *)(&fat[i]);

    kprintf("%03X %03X\n", ents->ent1, ents->ent2);
  }

  fat_dirent_t *root_dir = (void *)((uintptr_t) buf + (root_dir_sec * bpb->byts_per_sec));
  kprintf("[fat12] root dir: %p\n", root_dir);

  char name[12];
  for (int i = 0; i < 6; i++) {
    fat_dirent_t *dirent = &(root_dir[i]);

    if (dirent->attr == FAT_LONG_NAME) {
      kprintf("long name entry\n");
      continue;
    }

    for (int j = 0; j < 11; j++) {
      name[j] = dirent->name[j];
    }
    name[11] = '\0';

    fat12_packed_ent_t *ent = (void *)(&fat[dirent->fst_clus_lo]);
    kprintf("%s | attr: 0x%X | 0x%02X | %dB\n",
            name, dirent->attr, dirent->fst_clus_lo, dirent->file_size);
    kprintf("FAT Entry: 0x%03X | 0x%03X\n", ent->ent1, ent->ent2);
  }
}
