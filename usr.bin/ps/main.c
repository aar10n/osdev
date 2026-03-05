#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#define STATUS_PATH_MAX 64
#define LINE_MAX_LEN 256

static int is_numeric(const char *s) {
  while (*s) {
    if (!isdigit(*s))
      return 0;
    s++;
  }
  return 1;
}

static const char *skip_whitespace(const char *s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static void parse_status(const char *pid_str) {
  char path[STATUS_PATH_MAX];
  char line[LINE_MAX_LEN];
  char name_buf[64] = {0};
  char binpath_buf[128] = {0};
  char state_char = '?';
  char ppid_buf[16] = "?";
  char uid_buf[16] = "?";

  snprintf(path, sizeof(path), "/proc/pid/%s/status", pid_str);
  FILE *f = fopen(path, "r");
  if (!f)
    return;

  while (fgets(line, sizeof(line), f)) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = '\0';

    if (strncmp(line, "Name:", 5) == 0) {
      const char *v = skip_whitespace(line + 5);
      if (strcmp(v, "(null)") != 0)
        snprintf(name_buf, sizeof(name_buf), "%s", v);
    } else if (strncmp(line, "Bin Path:", 9) == 0) {
      const char *v = skip_whitespace(line + 9);
      if (strcmp(v, "(null)") != 0)
        snprintf(binpath_buf, sizeof(binpath_buf), "%s", v);
    } else if (strncmp(line, "State:", 6) == 0) {
      const char *v = skip_whitespace(line + 6);
      state_char = *v;
    } else if (strncmp(line, "PPid:", 5) == 0) {
      const char *v = skip_whitespace(line + 5);
      snprintf(ppid_buf, sizeof(ppid_buf), "%s", v);
    } else if (strncmp(line, "Uid:", 4) == 0) {
      const char *v = skip_whitespace(line + 4);
      snprintf(uid_buf, sizeof(uid_buf), "%s", v);
    }
  }
  fclose(f);

  const char *display_name = name_buf[0] ? name_buf :
                             binpath_buf[0] ? binpath_buf : "?";
  printf("%5s %5s  %c %5s  %s\n", pid_str, ppid_buf, state_char, uid_buf, display_name);
}

int main(int argc, char **argv) {
  DIR *dir = opendir("/proc/pid");
  if (!dir) {
    perror("opendir /proc/pid");
    return 1;
  }

  printf("%5s %5s  %s %5s  %s\n", "PID", "PPID", "S", "UID", "CMD");

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;
    if (!is_numeric(ent->d_name))
      continue;
    parse_status(ent->d_name);
  }

  closedir(dir);
  return 0;
}
