#include "slub_utils.h"
#include "debug.h"
#include "slub.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

bool is_slab_list_empty(kmem_slab_t *slab_ptr) { return slab_ptr == NULL; }

kmem_slab_t *pop_slab(kmem_slab_t **head_ptr) {

  if (head_ptr == NULL || *head_ptr == NULL)
    return NULL;

  kmem_slab_t *ptr = *head_ptr;
  *head_ptr = ptr->nxt;

  if (ptr->nxt != NULL)
    ptr->nxt->prv = NULL;

  ptr->nxt = NULL;
  ptr->prv = NULL;
  return ptr;
}

void push_slab(kmem_slab_t **head_ptr, kmem_slab_t *new_slab) {

  if (head_ptr == NULL || new_slab == NULL)
    return;

  new_slab->nxt = *head_ptr;
  new_slab->prv = NULL;

  if (new_slab->nxt != NULL)
    new_slab->nxt->prv = new_slab;

  *head_ptr = new_slab;
}

void remove_slab(kmem_slab_t **head_ptr, kmem_slab_t *node_ptr) {

  if (head_ptr == NULL || node_ptr == NULL)
    return;

  if (*head_ptr == node_ptr)
    *head_ptr = node_ptr->nxt;

  if (node_ptr->prv != NULL)
    node_ptr->prv->nxt = node_ptr->nxt;

  if (node_ptr->nxt != NULL)
    node_ptr->nxt->prv = node_ptr->prv;

  node_ptr->nxt = NULL;
  node_ptr->prv = NULL;
}

static inline uint64_t getLevelXPageIndex(uint64_t address, int level) {
  level--;
  uint64_t level_mask = LEVEL_1_PAGE_MASK;
  level_mask <<= (level * 9);
  return (level_mask & address) >> (level * 9 + 12);
}

void hashMapInit(HashMap_t *hash) {
  hash->primaryTable = (uint64_t *)get_page(PAGE_SIZE);
  memset(hash->primaryTable, 0, PAGE_SIZE);
}

void hashMapInsert(HashMap_t *hash, uint64_t address, uint64_t value) {
  uint64_t idx = getLevelXPageIndex(address, 4);
  uint64_t *next_table = hash->primaryTable;

  for (int level = 3; level > 0; level--) {
    if (next_table[idx] == 0)
      next_table[idx] = (uint64_t)get_page(PAGE_SIZE);

    next_table = (uint64_t *)next_table[idx];
    idx = getLevelXPageIndex(address, level);
  }

  next_table[idx] = value;
}

uint64_t hashMapSearch(HashMap_t *hash, uint64_t address) {
  uint64_t idx = getLevelXPageIndex(address, 4);
  uint64_t *next_table = hash->primaryTable;

  for (int level = 3; level > 0; level--) {
    if (next_table[idx] == 0)
      return 0;

    next_table = (uint64_t *)next_table[idx];
    idx = getLevelXPageIndex(address, level);
  }

  return next_table[idx];
}

static inline bool isAllNull(uint64_t *table) {
  for (int i = 0; i < NO_ENTRIES; i++)
    if (table[i] != 0)
      return false;
  return true;
}

bool hashMapDelete(HashMap_t *hash, uint64_t address) {
  uint64_t idx = getLevelXPageIndex(address, 4);
  uint64_t *next_table = hash->primaryTable;
  uint64_t *tables[3];

  for (int level = 3; level > 0; level--) {
    if (next_table[idx] == 0)
      return false;

    next_table = (uint64_t *)next_table[idx];
    idx = getLevelXPageIndex(address, level);
    tables[3 - level] = next_table;
  }

  next_table[idx] = 0;
  int level = 2;
  idx = 2;

  while (idx >= 0) {
    uint64_t *table = tables[idx];
    if (!isAllNull(table))
      return true;

    free_page((void *)table, PAGE_SIZE);
    uint64_t page_idx = getLevelXPageIndex(address, level);

    if (idx != 0)
      tables[idx - 1][page_idx] = 0;
    else
      hash->primaryTable[page_idx] = 0;

    idx--;
    level++;
  }

  return true;
}
