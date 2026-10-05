#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buddy.h"

typedef enum {
  PAGE_TYPE_FREE = 1,
  PAGE_TYPE_ALLOC,
  PAGE_TYPE_SLAB,
  PAGE_TYPE_FS,
} page_type_t;

// Forward declaration
struct kmem_cache_s;

// Frame metadata to track state (allocated/free) and current order
typedef struct frame_info_s {
  uint8_t metadata;    // 0-4: order, 5: free/used, 6-7: reserved
  uint8_t page_type;   // Identifies what subsystem owns this page
  uint16_t inuse;      // How many objects are currently allocated
  uint32_t _reserved;  // Padding to ensure 64-bit alignment for pointers

  // --- Subsystem Specific Data (32 bytes) ---
  union {
    // Active when page_type == PAGE_TYPE_SLAB
    struct {
      struct kmem_cache_s* cache;
      void* freelist;
      struct frame_info_s* next;  // Doubly-linked list of slabs
      struct frame_info_s* prev;
    } slab;

    // Future: Active when page holds memory-mapped files
    struct {
      void* file_node;
      uint64_t file_offset;
    } fs;
  };
} frame_info_t;

struct pmm_state_s {
  size_t total_memory_size;
  size_t usable_memory_size;
  size_t page_size;
  size_t hhdm_offset;
  frame_info_t* metadata;  // Pointer to the metadata array
  buddy_state_t buddy_object;
};

int pmm_init(struct pmm_state_s* self, size_t page_size, size_t hhdm_offset);

static inline void* pmm_alloc(struct pmm_state_s* self, size_t size) {
  return buddy_alloc(&self->buddy_object, size);
}

static inline void pmm_free(struct pmm_state_s* self, void* ptr) {
  buddy_free(&self->buddy_object, ptr);
}

static inline bool pmm_is_pages_avaliable(struct pmm_state_s* self,
                                          size_t no_pages) {
  return self->buddy_object.free_frames >= no_pages;
}

static inline size_t pmm_get_frame_index(struct pmm_state_s* self,
                                         void* phys_addr) {
  return (size_t)phys_addr / self->page_size;
}

static inline frame_info_t* pmm_get_frame_info(struct pmm_state_s* self,
                                               void* phys_addr) {
  size_t frame_index = (size_t)phys_addr / self->page_size;
  return &self->metadata[frame_index];
}
