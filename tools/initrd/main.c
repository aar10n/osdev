//
// Created by Aaron Gill-Braun on 2020-09-07.
//

#include <stdio.h>

int main(int argc, char **argv) {
  printf("argc: %d\n", argc);
  for (int i = 0; i < argc; i++) {
    printf("> %s\n", argv[i]);
  }

  return 0;
}
