#pragma once

#include <boot/boot.h>
#include <stddef.h>
#include <stdint.h>

void get_max_len(struct MemoryMapEntry_s *entries, size_t noEntries,
                 uint8_t type, size_t *max_usable_length,
                 uintptr_t *max_usable_base);

static inline uint64_t log2_u64(uint64_t n) { return 63 - __builtin_clzll(n); }

static inline uint64_t nxt_pow2(uint64_t n) {
  if (n == 0)
    return 1;

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

static inline uintptr_t get_end_addr(void *start_addr, size_t size) {
  return (uintptr_t)start_addr + size;
}
