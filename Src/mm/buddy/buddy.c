#include "buddy.h"
#include <stdbool.h>

#define _min(a, b) ((a) < (b) ? (a) : (b))

static inline size_t addr_to_page_index(buddy_t *buddy, uintptr_t addr) {
  return (addr - (uintptr_t)buddy->base) / buddy->page_size;
}

static inline bool is_valid_addr(buddy_t *buddy, uintptr_t addr) {

  if (addr < (uintptr_t)buddy->base ||
      addr >= (uintptr_t)buddy->base + buddy->total_size) {
    return false;
  }
  return true;
}

static void push_block(buddy_t *buddy, uintptr_t addr, uint8_t order) {

  size_t index = addr_to_page_index(buddy, addr);
  block_t *blk = &buddy->metadata[index];

  blk->order = order;
  blk->free = BUDDY_BLOCK_FREE;
  blk->prv = NULL;
  blk->nxt = buddy->free_area[order];

  if (blk->nxt)
    blk->nxt->prv = blk;

  buddy->free_area[order] = blk;
}

static block_t *pop_block(buddy_t *buddy, uint8_t order) {

  block_t *blk = buddy->free_area[order];

  if (!blk)
    return NULL;

  buddy->free_area[order] = blk->nxt;

  if (buddy->free_area[order])
    buddy->free_area[order]->prv = NULL;

  blk->nxt = NULL;
  blk->prv = NULL;
  blk->free = BUDDY_BLOCK_USED;

  return blk;
}

static void remove_block(buddy_t *buddy, size_t index) {

  block_t *blk = &buddy->metadata[index];

  if (blk->prv)
    blk->prv->nxt = blk->nxt;
  else
    buddy->free_area[blk->order] = blk->nxt;

  if (blk->nxt)
    blk->nxt->prv = blk->prv;

  blk->nxt = NULL;
  blk->prv = NULL;
  blk->free = BUDDY_BLOCK_USED;
}

int buddy_init(buddy_t *buddy, void *memory, block_t *metadata,
               block_t **free_area, size_t total_size, size_t page_size) {

  if (!buddy || !memory || !metadata || total_size < page_size)
    return -1;

  buddy->base = memory;
  buddy->free_area = free_area;
  buddy->total_size = total_size;
  buddy->page_size = page_size;
  buddy->max_order = log2_u64(total_size / page_size);
  buddy->num_pages = total_size / page_size;
  buddy->free_pages = buddy->num_pages;
  buddy->metadata = metadata;

  for (size_t i = 0; i < buddy->max_order + 1; i++)
    buddy->free_area[i] = NULL;

  for (size_t i = 0; i < buddy->num_pages; i++) {
    metadata[i].prv = NULL;
    metadata[i].nxt = NULL;
    metadata[i].order = 0;
    metadata[i].free = BUDDY_BLOCK_USED;
  }

  size_t page_order = log2_u64(page_size);
  size_t order = log2_u64(total_size);
  size_t size_diff = total_size;
  uintptr_t base = (uintptr_t)memory;

  if ((1UL << order) < page_size)
    return -2;

  push_block(buddy, (uintptr_t)base, order - page_order);

  while ((1UL << order) < size_diff) {
    size_diff -= (1UL << order);
    base += (1UL << order);
    order = log2_u64(size_diff);

    if ((1UL << order) < page_size)
      break;

    push_block(buddy, (uintptr_t)base, order - page_order);
  }

  return 0;
}

void *buddy_alloc(buddy_t *buddy, size_t size) {

  if (!size)
    return NULL;

  size_t page_size = buddy->page_size;
  size_t needed_pages = (size + page_size - 1) / page_size;
  needed_pages = nxt_pow2(needed_pages);
  uint8_t order = log2_u64(needed_pages);
  uint8_t current = order;

  while (current <= buddy->max_order && !buddy->free_area[current])
    current++;

  if (current > buddy->max_order)
    return NULL;

  block_t *blk = pop_block(buddy, current);
  uintptr_t addr =
      (uintptr_t)buddy->base + ((blk - buddy->metadata) * page_size);

  /*
   * Split downward
   */
  while (current > order) {
    current--;
    uintptr_t buddy_addr = addr + ((1UL << current) * page_size);
    push_block(buddy, buddy_addr, current);
  }

  size_t page_index = addr_to_page_index(buddy, addr);

  buddy->metadata[page_index].order = order;
  buddy->metadata[page_index].free = BUDDY_BLOCK_USED;
  buddy->free_pages -= needed_pages;

  return (void *)addr;
}

void buddy_free(buddy_t *buddy, void *ptr) {

  if (!ptr || !is_valid_addr(buddy, (uintptr_t)ptr))
    return;

  uintptr_t addr = (uintptr_t)ptr;
  size_t page_size = buddy->page_size;
  size_t idx = addr_to_page_index(buddy, (uintptr_t)ptr);
  uint8_t order = buddy->metadata[idx].order;
  uint8_t no_pages = 1UL << order;

  if (buddy->metadata[idx].free)
    return;

  while (order < buddy->max_order) {
    uintptr_t relative = addr - (uintptr_t)buddy->base;
    uintptr_t buddy_relative = relative ^ ((1UL << order) * page_size);
    uintptr_t buddy_addr = (uintptr_t)buddy->base + buddy_relative;
    size_t buddy_index = addr_to_page_index(buddy, buddy_addr);
    block_t *buddy_blk = &buddy->metadata[buddy_index];

    /*
     * Buddy unavailable
     */
    if (!buddy_blk->free || buddy_blk->order != order)
      break;

    /*
     * Remove buddy
     */
    remove_block(buddy, buddy_index);

    /*
     * Merge
     */
    addr = _min(addr, buddy_addr);
    order++;
  }

  push_block(buddy, addr, order);
  buddy->free_pages += no_pages;
}
