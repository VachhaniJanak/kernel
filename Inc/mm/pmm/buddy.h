#pragma once

#include <process/locks.h>
#include <stddef.h>
#include <stdint.h>

// Maximum order for buddy allocator (2^11 = 2048 pages(4KB) = 8MB)
#define BUDDY_MAX_ORDER 10

#define BUDDY_FREE_FLAG_MASK (1 << 5)
#define BUDDY_ORDER_MASK (0x1F)

// Frame metadata to track state (allocated/free) and current order
typedef struct {
  uint8_t metadata;  // 0-4: order, 5: free/used, 6-7: reserved
} buddy_frame_info_t;

// Embedded inside free physical pages
typedef struct buddy_free_block_s {
  struct buddy_free_block_s* next;
  struct buddy_free_block_s* prev;
} buddy_free_block_t;

typedef struct {
  // minimum page allocated by the buddy allocator
  size_t page_size;

  size_t max_order;
  size_t hhdm_offset;

  // usable frames
  size_t total_frames;
  size_t total_usable_frames;
  size_t free_frames;

  // base address of the usable memory
  size_t base;

  // Array of free lists, one for each order
  buddy_free_block_t* free_area[BUDDY_MAX_ORDER + 1];

  // One metadata entry per minimum page
  buddy_frame_info_t* metadata;

  spinlock_t lock;  // Spinlock for thread safety
} buddy_state_t;

void buddy_init(buddy_state_t* self, size_t page_size, size_t hhdm_offset,
                buddy_frame_info_t* frame_info_ptr, size_t metadata_num_frames);

void* buddy_alloc(buddy_state_t* self, size_t requested_size);

void buddy_free(buddy_state_t* self, void* ptr);

void buddy_print_state(buddy_state_t* self);
