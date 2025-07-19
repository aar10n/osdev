//
// Created by Aaron Gill-Braun on 2023-06-21.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define GETTY_PATH "/sbin/getty"
#define DEFAULT_TTY "/dev/ttyS0"
#define DEFAULT_SHELL "/bin/sh"

static void sigchld_handler(int sig) {
  int status;
  pid_t pid;

  // reap all dead child processes
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    write(STDOUT_FILENO, "init: child process exited\n", 28);
  }
}

static void spawn_getty(const char* tty, const char* shell) {
  pid_t pid = fork();
  if (pid == 0) {
    // child process - exec getty
    execl(GETTY_PATH, "getty", tty, shell, NULL);
    perror("init: failed to exec getty");
    exit(1);
  } else if (pid < 0) {
    perror("init: fork failed");
  } else {
    printf("init: spawned getty (pid %d) for %s\n", pid, tty);
  }
}

int main() {
  const char* tty = getenv("TTY");
  const char* shell = getenv("SHELL");
  if (!tty) {
    tty = DEFAULT_TTY;
  }
  if (!shell) {
    shell = DEFAULT_SHELL;
  }

  printf("init: starting with TTY=%s SHELL=%s\n", tty, shell);

  // set up signal handler for child processes
  signal(SIGCHLD, sigchld_handler);

  // spawn initial getty
  spawn_getty(tty, shell);

  // main loop - respawn getty if it dies
  while (1) {
    int status;
    pid_t pid = wait(&status);

    if (pid > 0) {
      printf("init: getty died, respawning...\n");
      spawn_getty(tty, shell);
    }
  }

  return 0;
}
