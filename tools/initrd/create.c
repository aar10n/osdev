//
// Created by Aaron Gill-Braun on 2020-09-07.
//

#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "fs.h"
#include "path.h"

//

void copy_file(fs_node_t *root, char *from_path, char *to_path, struct stat f_stat) {
  uint32_t len = strlen(to_path);
  char *tmp = malloc(len + 1);
  strcpy(tmp, to_path);

  char *dir_name = dirname(tmp);
  char *base_name = basename(tmp);

  fs_node_t *parent;
  if (get_node(root, dir_name, GET_DIRECTORY | GET_CREATE, &parent) == -1) {
    fprintf(stderr, "initrd: %s: %s\n", to_path, strerror(fs_errno));
    exit(1);
  }

  fs_node_t *existing;
  if (get_node(parent, base_name, 0, &existing) == 0) {
    print(QUIET, "file: %s already exists\n", tmp);
    print(QUIET, "skipping\n");
    free(tmp);
    return;
  }
  free(tmp);

  uint8_t *buffer = malloc(f_stat.st_size);
  FILE *fp = fopen(from_path, "r");
  fread(buffer, 1, f_stat.st_size, fp);

  create_file(base_name, parent, f_stat.st_size, buffer);
  print(VERBOSE, "added file %s\n", to_path);
}

void copy_dir(fs_node_t *root, char *from_path, char *to_path) {
  DIR *fdir = opendir(from_path);
  if (fdir == NULL) {
    fprintf(stderr, "initrd: %s: %s\n", from_path, strerror(errno));
    exit(1);
  }

  fs_node_t *dir;
  if (get_node(root, to_path, GET_DIRECTORY | GET_CREATE, &dir) == -1) {
    fprintf(stderr, "initrd: %s: %s\n", to_path, strerror(fs_errno));
    exit(1);
  }

  // print(VERBOSE, "creating directory %s\n\n", from_path);

  struct dirent *e;
  while ((e = readdir(fdir)) != NULL) {
    char *file_from_path = concat_path(from_path, e->d_name);
    char *file_to_path = concat_path(to_path, e->d_name);

    struct stat f_stat;
    if (stat(file_from_path, &f_stat) == -1) {
      fprintf(stderr, "initrd: %s: %s\n", file_from_path, strerror(errno));
      exit(1);
    }

    if (S_ISREG(f_stat.st_mode)) {
      print(VERBOSE, "%s -> %s\n", file_from_path, file_to_path);
      copy_file(root, file_from_path, file_to_path, f_stat);
    } else if (S_ISDIR(f_stat.st_mode)) {
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
        continue;
      }

      copy_dir(root, file_from_path, file_to_path);
    }
  }
}

void initrd_create(int argc, char **argv) {
  fs_node_t *root = create_directory("/", NULL, true);

  struct stat path_stat;
  for (int i = optind; i < argc; i++) {
    char *arg = argv[i];

    char **paths;
    int n = split_str(arg, ':', &paths);
    if (n > 2 || n < 1) {
      fprintf(stderr, "initrd: invalid path specifier\n");
      exit(1);
    }

    char *from_path = paths[0];
    char *to_path = n == 2 ? paths[1] : paths[0];
    if (stat(from_path, &path_stat) == -1) {
      fprintf(stderr, "initrd: %s: %s\n", arg, strerror(errno));
      exit(1);
    }

    if (S_ISREG(path_stat.st_mode)) {
      copy_file(root, from_path, to_path, path_stat);
    } else if (S_ISDIR(path_stat.st_mode)) {
      // recursively copy directory
      copy_dir(root, from_path, to_path);
    }
  }

  uint32_t total_nodes = get_tree_size(root);

  print(VERBOSE, "block size: %d bytes\n", block_size);
  print(VERBOSE, "total node count: %d\n", total_nodes + reserved);
  print(VERBOSE, "used node count: %d\n", total_nodes);
  print(VERBOSE, "free node count: %d\n", reserved);
  print(VERBOSE, "\n");

  metadata_t meta;
  meta.last_id = last_id;
  meta.total_nodes = total_nodes;

  fs_t fs;
  fs.meta = meta;
  fs.root = root;

  initrd_write(out_file, &fs);
}
