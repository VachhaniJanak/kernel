#pragma once

#include <stddef.h>
#include <stdint.h>

void *kmemcpy(void *restrict dest, const void *restrict src, size_t n);

void *kmemset(void *s, int c, size_t n);

void *kmemmove(void *dest, const void *src, size_t n);

int kmemcmp(const void *s1, const void *s2, size_t n);

char *kstrchr(const char *s, int c);

void kstrcpy(char *dest, const char *src);
