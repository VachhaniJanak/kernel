#pragma once

#include <mm/pmm/buddy.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pmm_state_s {
  size_t total_memory_size;
  size_t usable_memory_size;
  size_t page_size;
  size_t hhdm_offset;
  buddy_state_t buddy_object;
};

int pmm_init(struct pmm_state_s* self, size_t page_size, size_t hhdm_offset);

static inline void* pmm_alloc(struct pmm_state_s* self, size_t size) {
  return buddy_alloc(&self->buddy_object, size);
}

static inline void pmm_free(struct pmm_state_s* self, void* ptr) {
  buddy_free(&self->buddy_object, ptr);
}

static inline bool pmm_is_pages_avaliable(struct pmm_state_s* self, size_t no_pages) {
  return self->buddy_object.free_frames >= no_pages;
}