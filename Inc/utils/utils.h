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

static inline uint64_t log2(uint64_t n) { return 63 - __builtin_clzll(n); }

static inline uint64_t nxt_pow2(uint64_t n) {
  if (n == 0) return 1;

  n--;

  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;

  return ++n;
}

static inline size_t round_to_page_size(size_t size, size_t page_size) {
  return ((size + page_size - 1) / page_size) * page_size;
}

static inline size_t round_to_page(size_t size, size_t page_size) {
  return (size + page_size - 1) / page_size;
}

static inline uintptr_t get_end_addr(void* start_addr, size_t size) {
  return (uintptr_t)start_addr + size;
}

// round a given address to page address (if page address is align)
static inline void* round_to_page_boundary(void* ptr, size_t page_size) {
  return (void*)((size_t)ptr & ~(page_size - 1));
}

static inline void* PAGE_ALIGN_UP(void* ptr, size_t page_size) {
  return (void*)(((size_t)ptr + page_size - 1) & ~(page_size - 1));
}

static inline void* PAGE_ALIGN_DOWN(void* ptr, size_t page_size) {
  return (void*)((size_t)ptr & ~(page_size - 1));
}
