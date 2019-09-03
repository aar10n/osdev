#include <stddef.h>
#include <stdint.h>

int memcmp(const void *str1, const void *str2, size_t count);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
void *memset(void *dest, int val, size_t len);

int strcmp(const char *s1, const char *s2);
int strlen(const char *s);
void reverse(char *s);
