//
// Created by Aaron Gill-Braun on 2020-09-07.
//

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "initrd.h"
#include "path.h"

#define VERSION 1.0

#define HELP_TEXT "usage: initrd [-hqvV] [-b block-size] [-c count] \n" \
                  "              [-o out-file] [-r reserved] <command> <args> \n" \
                  "\n"                     \
                  "commands: \n"           \
                  "  create file... \n"  \
                  "  cat <file> [path] \n" \
                  "  ls <file> [path] \n"

uint32_t block_size = 512;
char *out_file = "./initrd.img";
uint32_t reserved = 32;
bool quiet = false;
bool verbose = false;

char *get_arg(int argc, char **argv, bool optional) {
  if (optind >= argc) {
    if (optional) {
      return NULL;
    }

    fprintf(stderr, HELP_TEXT);
    exit(1);
  }

  char *arg = argv[optind];
  optind++;
  return arg;
}

int main(int argc, char **argv) {
  int c;
  while ((c = getopt (argc, argv, "b:ho:qr:vV")) != -1) {
    switch (c) {
      case 'b':
        block_size = (uint32_t) atoi(optarg);
        if (block_size == 0) {
          fprintf(stderr, "initrd: illegal field value for -%c\n", c);
          exit(1);
        }
        break;
      case 'h':
        printf(HELP_TEXT);
        exit(0);
      case 'o':
        out_file = optarg;
        break;
      case 'q':
        quiet = true;
        verbose = false;
        break;
      case 'r':
        reserved = (uint32_t) atoi(optarg);
        if (reserved == 0) {
          fprintf(stderr, "initrd: illegal field value for -%c\n", c);
          exit(1);
        }
      case 'v':
        verbose = true;
        quiet = false;
        break;
      case 'V':
        printf("initrd v%.2f\n", VERSION);
        exit(0);
      case '?':
        if (optopt == 'd' || optopt == 'o') {
          fprintf(stderr, "initrd: option requires an argument -- %c\n", optopt);
        } else if (isprint(optopt)) {
          fprintf(stderr, "initrd: illegal option -- %c\n", optopt);
        } else {
          fprintf(stderr, "unknown option character `\\x%x'.\n", optopt);
        }
        fprintf(stderr, HELP_TEXT);
        exit(1);
      default:
        abort();
    }
  }

  if (optind >= argc - 1) {
    fprintf(stderr, HELP_TEXT);
    exit(1);
  }

  char *command = argv[optind];
  if (strcmp(command, "cat") == 0) {
    optind++;

    char *file = get_arg(argc, argv, false);
    char *path = get_arg(argc, argv, false);

    fs_t fs;
    initrd_read(file, &fs);
    fs_catfile(fs.root, path);
  } else if (strcmp(command, "create") == 0) {
    optind++;
    initrd_create(argc, argv);
  } else if (strcmp(command, "ls") == 0) {
    optind++;

    char *file = get_arg(argc, argv, false);
    char *path = get_arg(argc, argv, true);

    fs_t fs;
    initrd_read(file, &fs);
    if (path == NULL) {
      fs_lsdir(fs.root, ".");
    } else {
      fs_lsdir(fs.root, path);
    }
  } else {
    fprintf(stderr, "initrd: unknown command: %s\n", command);
    fprintf(stderr, HELP_TEXT);
    exit(1);
  }

  return 0;
}
