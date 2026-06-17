#pragma once

#include <mm/slub/slub.h>

#include <stdbool.h>
#include <stdint.h>

#define LEVEL_1_PAGE_MASK 0x00000000001FF000
#define NO_ENTRIES 512

typedef struct {
  uint64_t *primaryTable;
} HashMap_t;

kmem_slab_t *pop_slab(kmem_slab_t **head_ptr);
void push_slab(kmem_slab_t **head_ptr, kmem_slab_t *new_slab);
void remove_slab(kmem_slab_t **head_ptr, kmem_slab_t *node_ptr);
bool is_slab_list_empty(kmem_slab_t *slab_ptr);

void hashMapInit(HashMap_t *hash);
void hashMapInsert(HashMap_t *hash, uint64_t address, uint64_t value);
uint64_t hashMapSearch(HashMap_t *hash, uint64_t address);
bool hashMapDelete(HashMap_t *hash, uint64_t address);

// initialize the slab objects
static inline bool init_obj(void *obj_ptr, size_t available_size,
                            size_t obj_size) {

  if (available_size < obj_size)
    return false;

  size_t num_objs = available_size / obj_size;
  struct kmem_obj *obj = (struct kmem_obj *)obj_ptr;

  while (--num_objs) {
    obj->nxt = (struct kmem_obj *)((uintptr_t)obj + obj_size);
    obj = obj->nxt;
  }

  obj->nxt = NULL;
  return true;
}

// return free object from freelist
static inline void *get_free_obj(kmem_cache_t *ptr) {

  struct kmem_slab *slab_ptr = ptr->active;
  struct kmem_obj *obj_ptr = slab_ptr->obj;

  ptr->freelist = obj_ptr->nxt;
  slab_ptr->obj = obj_ptr->nxt;
  slab_ptr->aloc_obj++;
  return obj_ptr;
}

// move active slab to full slab list
static inline void mv_active_to_full(kmem_cache_t *ptr) {

  kmem_slab_t *slab_ptr = pop_slab(&ptr->active);
  push_slab(&ptr->full, slab_ptr);
}

// move partial slab to active slab
static inline void mv_partial_to_active(kmem_cache_t *ptr) {

  kmem_slab_t *slab_ptr = pop_slab(&ptr->partial);
  push_slab(&ptr->active, slab_ptr);
  ptr->freelist = slab_ptr->obj;
  ptr->npartial--;
}

// move given slab to partial slab list
static inline void mv_slab_to_partial(kmem_cache_t *ptr,
                                      kmem_slab_t *slab_ptr) {

  push_slab(&ptr->partial, slab_ptr);
  ptr->npartial++;
}

// set new created slab to active slab
static inline void set_new_active(kmem_cache_t *ptr, kmem_slab_t *slab_ptr) {

  ptr->active = slab_ptr;
  ptr->freelist = slab_ptr->obj;
  slab_ptr->cache = ptr;
  ptr->nslabs++;
}

// add given object to given slab freelist
static inline void add_obj_to_freelist(kmem_slab_t *slab_ptr,
                                       struct kmem_obj *obj_ptr) {

  obj_ptr->nxt = slab_ptr->obj;
  slab_ptr->obj = obj_ptr;
  slab_ptr->aloc_obj--;
}

static inline void update_freelist_ptr(kmem_cache_t *ptr) {
  if (ptr->active == NULL)
    return;
  ptr->freelist = ptr->active->obj;
}
