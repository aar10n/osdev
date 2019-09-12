//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#ifndef FS_EXT2_EXT2_H
#define FS_EXT2_EXT2_H

#include <drivers/ata.h>
#include "../../../fs/ext2/dir.h"
#include "../../../fs/ext2/inode.h"
#include "../../../fs/ext2/super.h"

void ext2_mount(ata_t *disk);

#endif // FS_EXT2_EXT2_H
