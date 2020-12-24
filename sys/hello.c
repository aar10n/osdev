//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <stdbool.h>

static int x = 123456789;
const char *str = "Hello, world!";
_Thread_local int y = 0xBEBE;

int main() {
  int a = 1;
  int b = 2;
  int c = a + b;
  return 0;
}
