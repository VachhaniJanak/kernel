#pragma once

#include <stddef.h>
#include <stdint.h>

#define SET_BIT(_val_, _bit_) ((_val_) |= (1 << (_bit_)))
#define CLR_BIT(_val_, _bit_) ((_val_) &= ~(1 << (_bit_)))
#define GET_BIT(_val_, _bit_) (((_val_) >> (_bit_)) & 0x1)

void* kmemcpy(void* restrict dest, const void* restrict src, size_t n);

void* kmemset(void* s, int c, size_t n);

void* kmemmove(void* dest, const void* src, size_t n);

int kmemcmp(const void* s1, const void* s2, size_t n);

char* kstrchr(const char* s, int c);

void kstrcpy(char* dest, const char* src);

