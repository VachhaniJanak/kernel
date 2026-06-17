#pragma once

#include <stddef.h>
#include <stdint.h>

#define BUDDY_BLOCK_FREE 1
#define BUDDY_BLOCK_USED 0

typedef struct block {
  struct block *prv;
  struct block *nxt;
  uint8_t order;
  uint8_t free;
} block_t;

typedef struct {
  uint8_t *base;
  size_t total_size;

  size_t page_size;
  size_t max_order;

  block_t **free_area;

  /*
   * One metadata entry per minimum page
   */
  block_t *metadata;

  size_t num_pages;
  size_t free_pages;

} buddy_t;

int buddy_init(buddy_t *buddy, void *memory, block_t *metadata,
               block_t **free_area, size_t total_size, size_t page_size);

void *buddy_alloc(buddy_t *buddy, size_t size);

void buddy_free(buddy_t *buddy, void *ptr);
