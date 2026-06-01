#pragma once

#include <stddef.h>

void *kmemcpy(void *restrict dest, const void *restrict src, size_t n);

void *kmemset(void *s, int c, size_t n);

