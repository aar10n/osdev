//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#ifndef FS_EXT2_EXT2_H
#define FS_EXT2_EXT2_H

#include <drivers/ata_pio.h>
#include <fs/fs.h>

struct ext2_superblock;
struct ext2_dirent;

int ext2_mount(fs_type_t *fs_type);

void ext2_print_debug_super(struct ext2_superblock *super);
void ext2_print_debug_dirent(struct ext2_dirent *dirent);

#endif // FS_EXT2_EXT2_H
