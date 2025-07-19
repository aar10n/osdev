//
// Created by Aaron Gill-Braun on 2025-07-03.
//

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "builtins.h"

struct builtin_cmd builtins[] = {
  {"help",     shell_help,     "Show this help"},
  {"exit",     shell_exit,     "Exit shell"},
  {"cd",       shell_cd,       "Change directory"},
  {"pwd",      shell_pwd,      "Print working directory"},
  {"ls",       shell_ls,       "List directory contents"},
  {"cat",      shell_cat,      "Display file contents"},
  {"echo",     shell_echo,     "Display text"},
  {"mkdir",    shell_mkdir,    "Create directory"},
  {"rmdir",    shell_rmdir,    "Remove directory"},
  {"rm",       shell_rm,       "Remove file"},
  {"segfault", shell_segfault, "Inject a segmentation fault"},
  {"test_mmap", shell_test_mmap, "Test mmap across forks"},
  {NULL, NULL, NULL}
};

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
  exit(0);
}

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
      printf("%s\n", entry->d_name);
    }
  }
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

int shell_segfault(char **args) {
  char *fault_type = args[1];
  if (fault_type == NULL) {
    fprintf(stderr, "segfault: expected fault type (r/w)\n");
    return 1;
  }

  int write;
  if (strcmp(fault_type, "r") == 0 || strcmp(fault_type, "read") == 0) {
    write = 0;
  } else if (strcmp(fault_type, "w") == 0 || strcmp(fault_type, "write") == 0) {
    write = 1;
  } else {
    fprintf(stderr, "fault: unknown fault type '%s'\n", fault_type);
    return 1;
  }

  // fork and cause fault in child
  char *ptr = (char *)0x12345678;
  pid_t pid = fork();
  if (pid == 0) {
    // in child process, cause a fault
    const char *desc = write ? "write" : "read";
    printf("causing a %s fault at address %p\n", desc, ptr);
    if (write) {
      *ptr = 'A'; // this should cause a segmentation fault
    } else {
      char c = *ptr; // this should also cause a segmentation fault
      printf("read value: %c\n", c); // this line should not be reached
    }
    assert(!"unreachable");
  } else if (pid < 0) {
    perror("fork");
    return 0;
  } else {
    wait(NULL); // wait for child to exit
  }

  printf("segfault command executed successfully, child exited\n");
  return 1;
}

int shell_test_mmap(char **args) {
  printf("allocating memory with mmap\n");

  void *ptr = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  printf("mmap allocated memory at %p\n", ptr);

  const unsigned long magic = 0xDEADBEEF;
  printf("writing magic value %lx to address %p\n", magic, ptr);
  *(unsigned long *)ptr = magic;
  printf("reading back value from address %p: %lx\n", ptr, *(unsigned long *)ptr);

  printf("forking to test mmap across processes\n");
  pid_t pid = fork();
  if (pid == 0) {
    // in child process
    printf("child process: reading magic value from address %p: %lx\n", ptr, *(unsigned long *)ptr);

    const unsigned long child_magic = 0xCAFEBABE;
    printf("child process: writing new magic value %lx to address %p\n", child_magic, ptr);
    *(unsigned long *)ptr = child_magic;
    printf("child process: reading back value from address %p: %lx\n", ptr, *(unsigned long *)ptr);
  } else if (pid < 0) {
    perror("fork");
    return 0;
  } else {
    printf("parent process: waiting for child to finish\n");
    wait(NULL); // wait for child to exit
    printf("parent process: child exited\n");
    printf("parent process: reading back value from address %p: %lx\n", ptr, *(unsigned long *)ptr);
  }
  return 1;
}
