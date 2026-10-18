#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/panic.h>

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
  uint16_t refcount;   // Reference count for shared pages
  uint16_t _reserved;  // Padding to ensure 64-bit alignment for pointers

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

static inline size_t pmm_get_frame_index(struct pmm_state_s* self,
                                         void* phys_addr) {
  return (size_t)phys_addr / self->page_size;
}

static inline frame_info_t* pmm_get_frame_info(struct pmm_state_s* self,
                                               void* phys_addr) {
  size_t frame_index = pmm_get_frame_index(self, phys_addr);
  return &self->metadata[frame_index];
}

static inline void* pmm_alloc(struct pmm_state_s* self, size_t size) {
  void* phys_addr = buddy_alloc(&self->buddy_object, size);

  if (phys_addr != NULL) {
    frame_info_t* frame_info = pmm_get_frame_info(self, phys_addr);
    frame_info->refcount = 1;
  }

  return phys_addr;
}

static inline void pmm_free(struct pmm_state_s* self, void* ptr) {
  frame_info_t* frame_info = pmm_get_frame_info(self, ptr);

  if (frame_info->refcount == 0){
    kernel_panic("[PMM] Invalid free detected at address 0x%zx", ptr);
    return;
  }

  if (__atomic_sub_fetch(&frame_info->refcount, 1, __ATOMIC_SEQ_CST) > 0) {
    return;  // Still in use, do not free
  }

  buddy_free(&self->buddy_object, ptr);
}

static inline bool pmm_is_pages_avaliable(struct pmm_state_s* self,
                                          size_t no_pages) {
  return self->buddy_object.free_frames >= no_pages;
}

static inline void pmm_increment_page_refcount(struct pmm_state_s* self,
                                               void* phys_addr) {
  frame_info_t* frame_info = pmm_get_frame_info(self, phys_addr);
  __atomic_add_fetch(&frame_info->refcount, 1, __ATOMIC_SEQ_CST);
}

static inline void pmm_decrement_page_refcount(struct pmm_state_s* self,
                                               void* phys_addr) {
  frame_info_t* frame_info = pmm_get_frame_info(self, phys_addr);
  __atomic_sub_fetch(&frame_info->refcount, 1, __ATOMIC_SEQ_CST);
}