//
// Created by Aaron Gill-Braun on 2025-06-13.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <signal.h>
#include <termios.h>

static void setup_terminal(int fd) {
  struct termios tty;

  // get current terminal attributes
  if (tcgetattr(fd, &tty) < 0) {
    perror("getty: tcgetattr failed");
    return;
  }

  // start with raw mode
  cfmakeraw(&tty);

  // configure canonical mode with line editing and echo
  tty.c_iflag |= ICRNL;
  tty.c_oflag |= OPOST | ONLCR;
  tty.c_lflag |= ICANON | ECHO | ECHOE | ECHOK | ISIG | IEXTEN;

  // apply settings
  if (tcsetattr(fd, TCSANOW, &tty) < 0) {
    perror("getty: tcsetattr failed");
  }
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <tty> <shell>\n", argv[0]);
    exit(1);
  }

  const char* tty_path = argv[1];
  const char* shell_path = argv[2];

  printf("getty: opening %s with shell %s\n", tty_path, shell_path);

  // open the tty device
  int fd = open(tty_path, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    perror("getty: failed to open tty");
    exit(1);
  }

  // start a new session and set the controlling terminal
  if (setsid() < 0) {
    perror("getty: setsid failed");
  }
  if (ioctl(fd, TIOCSCTTY, 0) < 0) {
    perror("getty: ioctl TIOCSCTTY failed");
  }

  // redirect stdin, stdout, stderr to the tty
  if (dup2(fd, STDIN_FILENO) < 0 ||
      dup2(fd, STDOUT_FILENO) < 0 ||
      dup2(fd, STDERR_FILENO) < 0) {
    perror("getty: dup2 failed");
    exit(1);
  }

  // close the original fd if it's not one of the standard descriptors
  if (fd > STDERR_FILENO) {
    close(fd);
  }

  // set up terminal characteristics
  setup_terminal(STDIN_FILENO);

  // reset signal handlers to default
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGHUP, SIG_DFL);

  // set a default PATH if not already set
  setenv("PATH", "/bin:/sbin:/usr/bin", /*overwrite=*/0);

  printf("\nWelcome to the system!\n");
  printf("Starting shell: %s\n\n", shell_path);
  fflush(stdout);

  // Parse shell command with potential arguments
  char shell_copy[256];
  char shell_binary[256];
  strncpy(shell_copy, shell_path, sizeof(shell_copy) - 1);
  shell_copy[sizeof(shell_copy) - 1] = '\0';

  // Build argv array - need stable strings since execv doesn't copy them
  static char arg_storage[16][64];  // Storage for argument strings
  char *shell_argv[16];
  int shell_argc = 0;

  char *token = strtok(shell_copy, " ");
  if (token) {
    // Save the binary path
    strncpy(shell_binary, token, sizeof(shell_binary) - 1);
    shell_binary[sizeof(shell_binary) - 1] = '\0';

    // Copy each argument to stable storage
    while (token != NULL && shell_argc < 15) {
      strncpy(arg_storage[shell_argc], token, sizeof(arg_storage[0]) - 1);
      arg_storage[shell_argc][sizeof(arg_storage[0]) - 1] = '\0';
      shell_argv[shell_argc] = arg_storage[shell_argc];
      shell_argc++;
      token = strtok(NULL, " ");
    }
  }
  shell_argv[shell_argc] = NULL;  // NULL-terminate

  // start the shell with parsed arguments
  execv(shell_binary, shell_argv);

  // exec failed
  perror("getty: failed to exec shell");
  exit(1);
}
