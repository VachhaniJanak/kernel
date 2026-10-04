#include <mm/pmm/buddy.h>
#include <mm/utils.h>
#include <process/locks.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/log.h>

// --- Helper Functions ---
static inline uint8_t get_order(buddy_frame_info_t* frame) {
  return frame->metadata & BUDDY_ORDER_MASK;
}

static inline void set_order(buddy_frame_info_t* frame, uint8_t order) {
  frame->metadata =
      (frame->metadata & BUDDY_FREE_FLAG_MASK) | (order & BUDDY_ORDER_MASK);
}

static inline bool is_free(buddy_frame_info_t* frame) {
  return (frame->metadata & BUDDY_FREE_FLAG_MASK) != 0;
}

static inline void set_free(buddy_frame_info_t* frame) {
  frame->metadata |= BUDDY_FREE_FLAG_MASK;
}

static inline void set_allocated(buddy_frame_info_t* frame) {
  frame->metadata &= ~BUDDY_FREE_FLAG_MASK;
}

// reurn the index of the page in the buddy allocator's metadata array
static inline size_t addr_to_index(buddy_state_t* self, size_t addr) {
  return addr / self->page_size;
}

static inline buddy_free_block_t* virt_to_phys(buddy_state_t* self,
                                               buddy_free_block_t* addr) {
  return (buddy_free_block_t*)((size_t)addr - self->hhdm_offset);
}

static inline buddy_free_block_t* phys_to_virt(buddy_state_t* self,
                                               buddy_free_block_t* addr) {
  return (buddy_free_block_t*)((size_t)addr + self->hhdm_offset);
}

void buddy_init(buddy_state_t* self, size_t page_size, size_t hhdm_offset,
                buddy_frame_info_t* frame_info_ptr,
                size_t metadata_num_frames) {
  self->page_size = page_size;
  self->max_order = BUDDY_MAX_ORDER;
  self->hhdm_offset = hhdm_offset;
  self->metadata = frame_info_ptr;
  self->total_frames = metadata_num_frames;
  self->free_frames = 0;
  self->total_usable_frames = 0;
  self->base = 0;

  // Initialize free lists
  for (int i = 0; i <= self->max_order; i++) {
    self->free_area[i] = NULL;
  }

  // Initialize the metadata
  // Mark EVERY frame as allocated by default
  for (size_t i = 0; i < self->total_frames; i++) {
    set_allocated(&self->metadata[i]);
    set_order(&self->metadata[i], 0);  // Order 0 for single pages
  }

  // Initialize the spinlock
  spinlock_init(&self->lock);
}

// --- Doubly Linked List Helpers ---
static void list_add(buddy_state_t* self, uint8_t order,
                     buddy_free_block_t* block) {
  // Translate to virtual ONLY to write into the block's memory
  buddy_free_block_t* virt_block = phys_to_virt(self, block);

  virt_block->prev = NULL;
  virt_block->next = self->free_area[order];  // Store the physical pointer

  if (self->free_area[order] != NULL) {
    buddy_free_block_t* virt_next = phys_to_virt(self, self->free_area[order]);
    // Store the physical address of 'block' into the 'prev' field
    virt_next->prev = block;
  }

  self->free_area[order] = block;
}

static void list_remove(buddy_state_t* self, uint8_t order,
                        buddy_free_block_t* block) {
  buddy_free_block_t* virt_block = phys_to_virt(self, block);

  if (virt_block->prev != NULL) {
    buddy_free_block_t* virt_prev = phys_to_virt(self, virt_block->prev);
    // Copy the physical 'next' pointer into the previous block
    virt_prev->next = virt_block->next;
  } else {
    self->free_area[order] = virt_block->next;
  }

  if (virt_block->next != NULL) {
    buddy_free_block_t* virt_next = phys_to_virt(self, virt_block->next);
    // Copy the physical 'prev' pointer into the next block
    virt_next->prev = virt_block->prev;
  }
}

static size_t size_to_order(size_t size, size_t page_size) {
  size_t pages = (size + page_size - 1) / page_size;
  pages = nxt_pow2(pages);
  return log2(pages);
}

void* buddy_alloc(buddy_state_t* self, size_t requested_size) {
  uint8_t requested_order = size_to_order(requested_size, self->page_size);

  if (requested_order > self->max_order) {
    return NULL;
  }

  size_t requested_frames = 1 << requested_order;

  unsigned long flags;

  SPIN_LOCK_ACQUIRE(&self->lock, flags);

  // Find the smallest available block >= requested_order
  int current_order = requested_order;

  while (current_order <= self->max_order &&
         self->free_area[current_order] == NULL) {
    current_order++;
  }

  // Out of memory
  if (current_order > self->max_order) {
    SPIN_LOCK_RELEASE(&self->lock, flags);
    return NULL;
  }

  // Remove the founded block from the free list
  buddy_free_block_t* block = self->free_area[current_order];

  list_remove(self, current_order, block);

  uint64_t block_addr = (uint64_t)block;
  uint64_t frame_idx = addr_to_index(self, block_addr);

  // Split the block down to the requested order
  while (current_order > requested_order) {
    current_order--;

    // address of the right-hand buddy
    uint64_t buddy_addr = block_addr + (self->page_size * (1 << current_order));
    buddy_free_block_t* buddy = (buddy_free_block_t*)buddy_addr;
    uint64_t buddy_idx = addr_to_index(self, buddy_addr);

    // Add the buddy to the free list of the lower order
    list_add(self, current_order, buddy);

    // Update metadata for the buddy
    set_order(&self->metadata[buddy_idx], current_order);
    set_free(&self->metadata[buddy_idx]);
  }

  // Mark allocated block, in-use
  set_order(&self->metadata[frame_idx], requested_order);
  set_allocated(&self->metadata[frame_idx]);

  self->free_frames -= requested_frames;

  SPIN_LOCK_RELEASE(&self->lock, flags);
  return (void*)block_addr;
}

void buddy_free(buddy_state_t* self, void* ptr) {
  if (ptr == NULL) {
    return;
  }

  uint64_t block_addr = (uint64_t)ptr;
  uint64_t frame_idx = addr_to_index(self, block_addr);

  unsigned long flags;

  SPIN_LOCK_ACQUIRE(&self->lock, flags);

  // Ensure we aren't double-freeing memory
  if (is_free(&self->metadata[frame_idx])) {
    // Handle error: kernel panic or early return
    SPIN_LOCK_RELEASE(&self->lock, flags);
    return;
  }

  // Retrieve the exact order this block was allocated with
  uint8_t order = get_order(&self->metadata[frame_idx]);
  size_t freed_frames = 1 << order;

  while (order < self->max_order) {
    uint64_t buddy_idx = frame_idx ^ (1 << order);

    if (buddy_idx >= self->total_frames) {
      break;
    }

    // Check if buddy is free AND is the exact same order
    if (!is_free(&self->metadata[buddy_idx]) ||
        get_order(&self->metadata[buddy_idx]) != order) {
      break;
    }

    // Buddy matched. Remove it from its free list
    uint64_t buddy_addr = buddy_idx * self->page_size;
    buddy_free_block_t* buddy = (buddy_free_block_t*)buddy_addr;

    list_remove(self, order, buddy);

    // Merge: the new block index is the smaller of the two
    if (buddy_idx < frame_idx) {
      frame_idx = buddy_idx;
      block_addr = buddy_addr;
    }

    order++;
  }

  // Place the final merged block onto the appropriate free list
  buddy_free_block_t* final_block = (buddy_free_block_t*)block_addr;
  list_add(self, order, final_block);

  // Update metadata for the new coalesced block
  set_order(&self->metadata[frame_idx], order);
  set_free(&self->metadata[frame_idx]);

  self->free_frames += freed_frames;

  SPIN_LOCK_RELEASE(&self->lock, flags);
}

void buddy_print_state(buddy_state_t* self) {
  log_print("\n\nBuddy Allocator State:\n");
  log_print("  Page Size: %zu bytes\n", self->page_size);
  log_print("  Max Order: %zu\n", self->max_order);
  log_print("  HHDM Offset: 0x%zx\n", self->hhdm_offset);
  log_print("  Total Frames: %zu\n", self->total_frames);
  log_print("  Total Usable Frames: %zu\n", self->total_usable_frames);
  log_print("  Free Frames: %zu\n", self->free_frames);
  log_print("  Base Address: 0x%zx\n", self->base);
  log_print("  Free Lists:\n");

  for (size_t order = 0; order <= self->max_order; order++) {
    size_t count = 0;
    buddy_free_block_t* current = self->free_area[order];

    while (current != NULL) {
      count++;
      current = phys_to_virt(self, current)->next;
    }

    log_print("    Order %02u: %zu\n", order, count);
  }
}