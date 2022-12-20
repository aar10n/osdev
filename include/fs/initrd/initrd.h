//
// Created by Aaron Gill-Braun on 2022-12-19.
//

#ifndef FS_INITRD_INITRD_H
#define FS_INITRD_INITRD_H

#include <fs.h>

#define INITRD_SIGNATURE "INITv1"

#define INITRD_ENT_FILE 'f'
#define INITRD_ENT_DIR  'd'
#define INITRD_ENT_LINK 'l'

typedef struct packed initrd_header {
  char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '1'
  uint16_t flags;          // initrd flags
  uint32_t total_size;     // total size of the initrd image
  uint32_t data_offset;    // offset from start of image to start of data section
  uint16_t entry_count;    // number of entries in the metadata section
  uint8_t reserved[14];    // reserved
} initrd_header_t;
static_assert(sizeof(initrd_header_t) == 32);

typedef struct packed initrd_entry {
  uint8_t entry_type;   // type of enty: 'f'=file | 'd'=directory | 'l'=symlink
  uint8_t reserved;     // reserved
  uint16_t path_len;    // length of the file path
  uint32_t data_offset; // offset from start of image to associated data
  uint32_t data_size;   // size of the associated data
  char path[];          // file path
} initrd_entry_t;
static_assert(sizeof(initrd_entry_t) == 12);

void initrd_init();

#endif
