#pragma once

#include <stddef.h>

void *kmemcpy(void *restrict dest, const void *restrict src, size_t n);

void *kmemset(void *s, int c, size_t n);

void *kmemmove(void *dest, const void *src, size_t n);

int kmemcmp(const void *s1, const void *s2, size_t n);