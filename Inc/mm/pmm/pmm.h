#pragma once

#include <mm/pmm/buddy.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern buddy_t usable_memory;

buddy_t *get_buddy(void);

bool init_pmm(uintptr_t usable_addr, size_t usable_size, size_t page_size,
              void *(*phys_to_virt)(void *));

static inline size_t pmm_total_size(void) {
  return usable_memory.num_pages * usable_memory.page_size;
}

static inline size_t pmm_free_size(void) {
  return usable_memory.free_pages * usable_memory.page_size;
}

static inline bool pmm_pages_avaliable(size_t no_pages) {
  return no_pages <= usable_memory.free_pages;
}

static inline void *pmm_alloc(size_t size) {
  return buddy_alloc(&usable_memory, size);
}

static inline void pmm_free(void *ptr) { buddy_free(&usable_memory, ptr); }
