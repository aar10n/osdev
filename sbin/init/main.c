//
// Created by Aaron Gill-Braun on 2023-06-21.
//

#include <stdio.h>
#include <signal.h>
#include <unistd.h>


void handle_alarm(int sig) {
  printf("Alarm triggered!\n");
}

int main(int argc, const char **argv) {
  puts("Hello, from userspace!\n");

  signal(SIGALRM, handle_alarm); // Set signal handler
  alarm(2);                      // Schedule alarm in 2 seconds

  printf("Waiting for alarm...\n");
  pause();                       // Wait for any signal
  printf("Alarm received!\n");
  return 0;
}
