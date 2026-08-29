#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

static inline size_t page_align_up(size_t value, size_t page_size) {
  return (value + page_size - 1) & ~(page_size - 1);
}

static inline size_t page_align_down(size_t value, size_t page_size) {
  return (value & ~(page_size - 1));
}

static inline bool is_page_aligned(uintptr_t addr, size_t alignment) {
  return (addr & (alignment - 1)) == 0;
}
