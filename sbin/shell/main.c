//
// Created by Aaron Gill-Braun on 2025-06-13.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>

#include "builtins.h"

#define MAX_ARGS 64
#define PROMPT "> " // busybox/sh uses '#' so make it different


char **parse_line(char *line) {
  int bufsize = MAX_ARGS;
  int position = 0;
  char **tokens = malloc(bufsize * sizeof(char*));
  char *token;

  if (!tokens) {
    fprintf(stderr, "memory allocation error\n");
    exit(EXIT_FAILURE);
  }

  token = strtok(line, " \t\r\n\a");
  while (token != NULL) {
    tokens[position] = token;
    position++;

    if (position >= bufsize) {
      bufsize += MAX_ARGS;
      char **new_tokens = realloc(tokens, bufsize * sizeof(char*));
      if (!new_tokens) {
        free(tokens);
        fprintf(stderr, "memory allocation error\n");
        exit(EXIT_FAILURE);
      }
      tokens = new_tokens;
    }

    token = strtok(NULL, " \t\r\n\a");
  }
  
  // Ensure we have space for the NULL terminator
  if (position >= bufsize) {
    bufsize += MAX_ARGS;
    char **new_tokens = realloc(tokens, bufsize * sizeof(char*));
    if (!new_tokens) {
      free(tokens);
      fprintf(stderr, "memory allocation error\n");
      exit(EXIT_FAILURE);
    }
    tokens = new_tokens;
  }
  
  tokens[position] = NULL;
  return tokens;
}

int shell_launch(char **args) {
  int status;
  pid_t pid = fork();
  if (pid == 0) {
    if (execvp(args[0], args) == -1) {
      perror("execvp");
    }
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    perror("fork");
    return 0;
  } else {
    do {
      waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }

  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// execute command (built-in or external)
int shell_execute(char **args) {
  int i;
  if (args[0] == NULL) {
    return 1;
  }

  // first check if it's a built-in command
  for (i = 0; builtins[i].name; i++) {
    if (strcmp(args[0], builtins[i].name) == 0) {
      return builtins[i].func(args);
    }
  }

  // otherwise fork+exec the command
  return shell_launch(args);
}

// check if line is empty or whitespace only
int is_empty_line(char *line) {
  while (*line) {
    if (!isspace(*line)) {
      return 0;
    }
    line++;
  }
  return 1;
}

// signal handler for Ctrl+C
void sigint_handler(int sig) {
  write(STDOUT_FILENO, "\n" PROMPT, sizeof(PROMPT)+1);
}

// sanitize input line by removing control characters
void sanitize_line(char *line) {
  char *read = line;
  char *write = line;

  while (*read) {
    // skip control characters (0x00-0x1F and 0x7F)
    // but keep tab (0x09), newline (0x0A), and carriage return (0x0D)
    if ((*read >= 0x20 && *read < 0x7F) || *read == '\t' || *read == '\n' || *read == '\r') {
      *write++ = *read;
    }
    read++;
  }
  *write = '\0';
}

// main shell loop
void shell_loop() {
  char *line = NULL;
  char **args;
  int status;
  size_t bufsize = 0;

  FILE *debug = fopen("/dev/debug", "w");
  if (debug == NULL) {
    perror("fopen /dev/debug");
    exit(EXIT_FAILURE);
  }

  const char *path = getenv("PATH");
  if (path == NULL) {
    fprintf(debug, "shell: PATH not set, using default\n");
    setenv("PATH", "/bin:/usr/bin", 1); // set a default PATH if not already set
  } else {
    fprintf(debug, "shell: using PATH `%s`\n", path);
  }

  while (1) {
    printf(PROMPT);
    fflush(stdout);

    // read line from stdin
    if (getline(&line, &bufsize, stdin) == -1) {
      if (feof(stdin)) {
        printf("\n");
        fprintf(debug, "shell: EOF reached\n");
        break; // EOF
      } else {
        perror("getline");
        continue;
      }
    }

    // sanitize the input to remove control characters
    sanitize_line(line);
    
    fprintf(debug, "shell: read line, `%s`\n", line);

    // skip empty lines
    if (is_empty_line(line)) {
      continue;
    }

    // parse and execute
    args = parse_line(line);
    status = shell_execute(args);

    free(args);
  }

  fclose(debug);
  free(line);
}

int main(int argc, char **argv) {
  printf("Basic Shell\n");
  printf("Type 'help' for available commands.\n\n");

  signal(SIGINT, sigint_handler);
  shell_loop();

  return EXIT_SUCCESS;
}
