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
#define MAX_PIPELINE 8
#define PROMPT "> " // busybox/sh uses '#' so make it distinct

//#define DEBUG
#ifdef DEBUG
#define DPRINTF(x, ...) fprintf(debug_file, "shell: " x, ##__VA_ARGS__)
#else
#define DPRINTF(x, ...)
#endif

FILE *debug_file;

typedef struct {
  char **args;
  int argc;
} command_t;

typedef struct {
  command_t *commands;
  int count;
} pipeline_t;

// parse a line into tokens, respecting double-quoted strings
char **parse_tokens(char *line, int *token_count) {
  int bufsize = MAX_ARGS;
  int position = 0;
  char **tokens = malloc(bufsize * sizeof(char*));
  
  if (!tokens) {
    fprintf(stderr, "memory allocation error\n");
    exit(EXIT_FAILURE);
  }

  char *ptr = line;
  while (*ptr) {
    // skip leading whitespace
    while (*ptr && strchr(" \t\r\n", *ptr)) {
      ptr++;
    }
    
    if (!*ptr) break;
    
    char *start;
    if (*ptr == '"') {
      ptr++; // skip opening quote
      start = ptr;
      while (*ptr && *ptr != '"') {
        ptr++;
      }
      if (*ptr == '"') {
        *ptr = '\0'; // replace closing quote with null
        ptr++;
      }
    } else {
      // handle regular token
      start = ptr;
      while (*ptr && !strchr(" \t\r\n|", *ptr)) {
        ptr++;
      }
      if (*ptr && *ptr != '|') {
        *ptr = '\0';
        ptr++;
      }
    }

    tokens[position] = start;
    position++;

    // expand array if needed
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

    // check for pipe separator
    if (*ptr == '|') {
      tokens[position] = "|";
      position++;
      ptr++;

      // expand array if needed
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
    }
  }
  
  // ensure we have space for the NULL terminator
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
  if (token_count) *token_count = position;
  return tokens;
}

// parse tokens into a pipeline structure
pipeline_t *parse_pipeline(char **tokens) {
  pipeline_t *pipeline = malloc(sizeof(pipeline_t));
  if (!pipeline) {
    fprintf(stderr, "memory allocation error\n");
    exit(EXIT_FAILURE);
  }
  
  pipeline->commands = malloc(MAX_PIPELINE * sizeof(command_t));
  if (!pipeline->commands) {
    free(pipeline);
    fprintf(stderr, "memory allocation error\n");
    exit(EXIT_FAILURE);
  }
  
  pipeline->count = 0;
  int cmd_start = 0;
  int i = 0;
  
  while (tokens[i]) {
    if (strcmp(tokens[i], "|") == 0 || tokens[i + 1] == NULL) {
      // end of command
      int is_last = (tokens[i + 1] == NULL && strcmp(tokens[i], "|") != 0);
      int cmd_end = is_last ? i + 1 : i;
      int argc = cmd_end - cmd_start;
      
      if (argc > 0) {
        pipeline->commands[pipeline->count].args = malloc((argc + 1) * sizeof(char*));
        if (!pipeline->commands[pipeline->count].args) {
          fprintf(stderr, "memory allocation error\n");
          exit(EXIT_FAILURE);
        }
        
        for (int j = 0; j < argc; j++) {
          pipeline->commands[pipeline->count].args[j] = tokens[cmd_start + j];
        }
        pipeline->commands[pipeline->count].args[argc] = NULL;
        pipeline->commands[pipeline->count].argc = argc;
        pipeline->count++;
        
        if (pipeline->count >= MAX_PIPELINE) {
          fprintf(stderr, "pipeline too long (max %d commands)\n", MAX_PIPELINE);
          break;
        }
      }
      
      cmd_start = i + 1;
    }
    i++;
  }
  
  return pipeline;
}

// free pipeline structure
void free_pipeline(pipeline_t *pipeline) {
  if (!pipeline) return;
  
  for (int i = 0; i < pipeline->count; i++) {
    free(pipeline->commands[i].args);
  }
  free(pipeline->commands);
  free(pipeline);
}

// execute a single command (external or builtin in a child process)
int execute_command(char **args, int input_fd, int output_fd, int close_fd) {
  pid_t pid = fork();
  if (pid == 0) {
    // child process
    if (input_fd != STDIN_FILENO) {
      dup2(input_fd, STDIN_FILENO);
      close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
      dup2(output_fd, STDOUT_FILENO);
      close(output_fd);
    }
    if (close_fd >= 0) {
      close(close_fd);
    }
    
    // check if it's a builtin
    for (int i = 0; builtins[i].name; i++) {
      if (strcmp(args[0], builtins[i].name) == 0) {
        exit(builtins[i].func(args));
      }
    }
    
    // execute external command
    if (execvp(args[0], args) == -1) {
      perror(args[0]);
      exit(EXIT_FAILURE);
    }
  }
  
  return pid;
}

// execute a pipeline of commands
int execute_pipeline(pipeline_t *pipeline) {
  if (pipeline->count == 0) {
    return 1;
  }
  
  // single command - no pipeline needed
  if (pipeline->count == 1) {
    char **args = pipeline->commands[0].args;
    
    // check for special builtins that shouldn't fork (cd, exit)
    if (strcmp(args[0], "cd") == 0) {
      return shell_cd(args);
    } else if (strcmp(args[0], "exit") == 0) {
      return shell_exit(args);
    }
    
    // execute command and wait
    pid_t pid = execute_command(args, STDIN_FILENO, STDOUT_FILENO, -1);
    if (pid < 0) {
      perror("fork");
      return 0;
    }
    
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
      printf("Process exited with status %d\n", WEXITSTATUS(status));
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
  }
  
  // multiple commands - create pipeline
  int prev_pipe[2] = {-1, -1};
  pid_t *pids = malloc(pipeline->count * sizeof(pid_t));
  if (!pids) {
    fprintf(stderr, "memory allocation error\n");
    return 0;
  }
  
  for (int i = 0; i < pipeline->count; i++) {
    int curr_pipe[2];
    
    // create pipe for all but last command
    if (i < pipeline->count - 1) {
      if (pipe(curr_pipe) == -1) {
        perror("pipe");
        free(pids);
        return 0;
      }
    }
    
    int input_fd = (i == 0) ? STDIN_FILENO : prev_pipe[0];
    int output_fd = (i == pipeline->count - 1) ? STDOUT_FILENO : curr_pipe[1];
    int close_fd = (i < pipeline->count - 1) ? curr_pipe[0] : -1;
    
    pids[i] = execute_command(pipeline->commands[i].args, input_fd, output_fd, close_fd);
    
    if (pids[i] < 0) {
      perror("fork");
      free(pids);
      return 0;
    }
    
    // close used pipe ends
    if (i > 0) {
      close(prev_pipe[0]);
    }
    if (i < pipeline->count - 1) {
      close(curr_pipe[1]);
      prev_pipe[0] = curr_pipe[0];
      prev_pipe[1] = curr_pipe[1];
    }
  }
  
  // wait for all processes
  int final_status = 0;
  for (int i = 0; i < pipeline->count; i++) {
    int status;
    waitpid(pids[i], &status, 0);
    if (i == pipeline->count - 1) {
      final_status = status;
    }
  }
  
  free(pids);
  
  if (WIFEXITED(final_status) && WEXITSTATUS(final_status) != 0) {
    printf("Pipeline exited with status %d\n", WEXITSTATUS(final_status));
  }
  
  return WIFEXITED(final_status) && WEXITSTATUS(final_status) == 0;
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
  char *line_copy = NULL;
  char **tokens;
  pipeline_t *pipeline;
  int status;
  size_t bufsize = 0;

#ifdef DEBUG
  debug_file = fopen("/dev/debug", "w");
  if (debug_file == NULL) {
    perror("fopen /dev/debug");
    exit(EXIT_FAILURE);
  }
#endif

  const char *path = getenv("PATH");
  if (path == NULL) {
    DPRINTF("PATH not set, using default\n");
    setenv("PATH", "/bin:/sbin:/usr/bin", 1);
    path = getenv("PATH");
  }
  DPRINTF("PATH is `%s`\n", path);

  while (1) {
    printf(PROMPT);
    fflush(stdout);

    // read line from stdin
    if (getline(&line, &bufsize, stdin) == -1) {
      if (feof(stdin)) {
        printf("\n");
        DPRINTF("EOF reached\n");
        break; // EOF
      } else {
        perror("getline");
        continue;
      }
    }

    // sanitize the input to remove control characters
    sanitize_line(line);

    char *line_end = strchr(line, '\n');
    size_t line_len = line_end ? (line_end - line) : strlen(line);
    DPRINTF("read line, `%.*s`\n", (int)line_len, line);

    // skip empty lines
    if (is_empty_line(line)) {
      continue;
    }

    // make a copy of the line for parsing (parse_tokens modifies it)
    line_copy = strdup(line);
    if (!line_copy) {
      fprintf(stderr, "memory allocation error\n");
      continue;
    }

    // parse tokens and build pipeline
    tokens = parse_tokens(line_copy, NULL);
    pipeline = parse_pipeline(tokens);
    
    // execute pipeline
    status = execute_pipeline(pipeline);
    
    // cleanup
    free_pipeline(pipeline);
    free(tokens);
    free(line_copy);
  }

  if (debug_file != NULL)
    fclose(debug_file);
  free(line);
}

int main(int argc, char **argv) {
  printf("Basic Shell\n");
  printf("Type 'help' for available commands.\n\n");

  signal(SIGINT, sigint_handler);
  shell_loop();

  return EXIT_SUCCESS;
}
