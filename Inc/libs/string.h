#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stddef.h>

/*
 * Memory operations
 */

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

/*
 * String operations
 */

size_t strlen(const char *str);

int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);

/*
 * String search
 */

char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);

/*
 * String tokenization
 */

char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif
