//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#ifndef INCLUDE_FS_EXT2_EXT2_H
#define INCLUDE_FS_EXT2_EXT2_H

#include <drivers/ata.h>
#include "../../../fs/ext2/inode.h"
#include "../../../fs/ext2/super.h"
#include "../../../fs/ext2/dir.h"

void ext2_mount(ata_t *disk);

#endif //INCLUDE_FS_EXT2_EXT2_H
