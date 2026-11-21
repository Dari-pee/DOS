#ifndef _STRING_H
#define _STRING_H 1

#include <sys/cdefs.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char *strchr(const char *s, int c);
int memcmp(const void*, const void*, size_t);
void* memcpy(void* __restrict, const void* __restrict, size_t);
void* memmove(void*, const void*, size_t);
void* memset(void*, int, size_t);
size_t strlen(const char*);
int strcmp(const char *str1, const char *str2);
char *strcpy(char *dst, const char *src);
char *strncpy(char *, const char *, size_t);
int strncmp(const char *, const char *, size_t);

#ifdef __cplusplus
}
#endif

#endif