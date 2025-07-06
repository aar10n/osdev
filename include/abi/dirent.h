//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_DIRENT_H
#define INCLUDE_ABI_DIRENT_H

#include <bits/dirent.h>

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#define IFTODT(x) ((x)>>12 & 017)
#define DTTOIF(x) ((x)<<12)

#endif
