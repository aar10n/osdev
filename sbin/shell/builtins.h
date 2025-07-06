//
// Created by Aaron Gill-Braun on 2025-07-03.
//

#ifndef SBIN_SHELL_BUILTINS_H
#define SBIN_SHELL_BUILTINS_H

struct builtin_cmd {
  char *name;
  int (*func)(char **);
  char *desc;
};

extern struct builtin_cmd builtins[];

int shell_cd(char **args);
int shell_pwd(char **args);
int shell_ls(char **args);
int shell_cat(char **args);
int shell_echo(char **args);
int shell_mkdir(char **args);
int shell_rmdir(char **args);
int shell_rm(char **args);
int shell_help(char **args);
int shell_exit(char **args);

#endif
