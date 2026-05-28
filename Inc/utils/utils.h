#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool get_mb_tag(unsigned long addr, uint8_t tag_type, void **ptr);
void memcpy(void *restrict dest, const void *restrict src, size_t n);
void memset(void *ptr, uint8_t c, size_t n);
