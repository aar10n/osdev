//
// Created by Aaron Gill-Braun on 2020-09-09.
//

#ifndef INITRD_PATH_H
#define INITRD_PATH_H

int split_str(char *str, char delim, char ***result);
void free_split(char **path, int n);

int split_path(char *path, char ***result);
char *concat_path(char *dir, char *base);

#endif
