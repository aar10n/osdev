//
// Created by Aaron Gill-Braun on 2025-07-03.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "builtins.h"

struct builtin_cmd builtins[] = {
  {"cd", shell_cd, "Change directory"},
  {"pwd", shell_pwd, "Print working directory"},
  {"ls", shell_ls, "List directory contents"},
  {"cat", shell_cat, "Display file contents"},
  {"echo", shell_echo, "Display text"},
  {"mkdir", shell_mkdir, "Create directory"},
  {"rmdir", shell_rmdir, "Remove directory"},
  {"rm", shell_rm, "Remove file"},
  {"help", shell_help, "Show this help"},
  {"exit", shell_exit, "Exit shell"},
  {NULL, NULL, NULL}
};

int shell_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "cd: expected argument\n");
  } else {
    if (chdir(args[1]) != 0) {
      perror("cd");
    }
  }
  return 1;
}

int shell_pwd(char **args) {
  char *cwd = getcwd(NULL, 0);
  if (cwd) {
    printf("%s\n", cwd);
    free(cwd);
  } else {
    perror("pwd");
  }
  return 1;
}

int shell_ls(char **args) {
  DIR *dir;
  struct dirent *entry;
  const char *path = (args[1] != NULL) ? args[1] : ".";

  dir = opendir(path);
  if (dir == NULL) {
    perror("ls");
    return 1;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] != '.') { // skip hidden files
      printf("%s  ", entry->d_name);
    }
  }
  printf("\n");
  closedir(dir);
  return 1;
}

int shell_cat(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "cat: expected argument\n");
    return 1;
  }

  FILE *file = fopen(args[1], "r");
  if (file == NULL) {
    perror("cat");
    return 1;
  }

  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file)) {
    printf("%s", buffer);
  }
  fclose(file);
  return 1;
}

int shell_echo(char **args) {
  int i = 1;
  while (args[i] != NULL) {
    printf("%s", args[i]);
    if (args[i + 1] != NULL) {
      printf(" ");
    }
    i++;
  }
  printf("\n");
  return 1;
}

int shell_mkdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "mkdir: expected argument\n");
    return 1;
  }

  if (mkdir(args[1], 0755) != 0) {
    perror("mkdir");
  }
  return 1;
}

int shell_rmdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "rmdir: expected argument\n");
    return 1;
  }

  if (rmdir(args[1]) != 0) {
    perror("rmdir");
  }
  return 1;
}

int shell_rm(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "rm: expected argument\n");
    return 1;
  }

  if (unlink(args[1]) != 0) {
    perror("rm");
  }
  return 1;
}

int shell_help(char **args) {
  int i;
  printf("Basic Shell - Built-in Commands:\n");
  for (i = 0; builtins[i].name; i++) {
    printf("  %-8s - %s\n", builtins[i].name, builtins[i].desc);
  }
  printf("\nYou can also run external programs by typing their name.\n");
  return 1;
}

int shell_exit(char **args) {
  return 0; // signal to exit
}
