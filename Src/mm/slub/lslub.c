#include "rbtree.h"
#include "slubutils.h"

#include <mm/slub/slub.h>

#include <utils/printf.h>
#include <utils/utils.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern void *pre_obj_alloc(size_t size);
extern void pre_obj_free(void *node);

static inline void *allocate_obj(size_t size) { return pre_obj_alloc(size); }
static inline void free_obj(void *ptr) { pre_obj_free(ptr); }

static RBTree slab_cache_map;

// create predefined caches for large objects
void init_slub_lcaches(struct lslub_state_s *state) {

  slub_rb_create(&slab_cache_map);

  for (size_t i = 0; i < NO_LARGE_CACHES; i++) {

    size_t cache_size = MIN_SIZE_LARGE_OBJECTS + i * 8;

    snprintf(state->names[i], SLUB_MAX_NAME_SIZE, "kmem_cache-%d", cache_size);

    state->caches[i].name = state->names[i];
    state->caches[i].size = cache_size;
    state->caches[i].freelist = NULL;
    state->caches[i].active = NULL;
    state->caches[i].partial = NULL;
    state->caches[i].full = NULL;
    state->caches[i].npartial = 0;
    state->caches[i].nslabs = 0;
  }
}

// return index of the cache for the given size
static inline size_t size_to_idx(size_t size) {
  size_t idx = (size - 1) / 8;
  size_t offset = (MIN_SIZE_LARGE_OBJECTS - 8) / 8;
  idx -= offset;
  return idx;
}

// initialize the slab
static inline kmem_slab_t *init_slab(void *page_addr, size_t obj_size,
                                     size_t page_size) {

  kmem_slab_t *slab_ptr = allocate_obj(sizeof(kmem_slab_t));

  if (slab_ptr == NULL)
    return NULL;

  slab_ptr->prv = NULL;
  slab_ptr->nxt = NULL;
  slab_ptr->cache = NULL;
  slab_ptr->aloc_obj = 0;
  slab_ptr->obj = page_addr;
  slab_ptr->slab_addr = page_addr;

  if (!init_obj(page_addr, page_size, obj_size)) {
    slab_ptr->obj = NULL;
    return NULL;
  }

  slub_rb_insert(&slab_cache_map, (void *)page_addr, (void *)slab_ptr);
  return slab_ptr;
}

// return cache for the given size
static inline struct kmem_cache *get_cache(struct lslub_state_s *state,
                                           size_t size) {

  if (size >= MIN_SIZE_LARGE_OBJECTS && size <= MAX_SIZE_LARGE_OBJECTS) {
    int idx = size_to_idx(size);
    return &state->caches[idx];
  }

  return NULL;
}

void *lslub_alloc(struct lslub_state_s *state, size_t size) {

  struct kmem_cache *kmc = NULL;
  kmc = get_cache(state, size);

  if (kmc == NULL)
    return NULL;

  // if freelist is not empty, return object from freelist
  if (kmc->freelist != NULL)
    return get_free_obj(kmc);

  // if active slab is not empty, move active slab to full slab list
  if (kmc->active != NULL)
    mv_active_to_full(kmc);

  // if partial slab is not empty, move partial slab to active slab and return
  // object from active slab
  if (kmc->partial != NULL) {
    mv_partial_to_active(kmc);
    return get_free_obj(kmc);
  }

  // create new slab and return object from new slab
  void *ptr = state->get_page();

  if (ptr == NULL)
    return NULL;

  kmem_slab_t *slab_ptr = init_slab(ptr, kmc->size, state->page_size);

  if (slab_ptr == NULL) {
    state->free_page(ptr);
    return NULL;
  }

  set_new_active(kmc, slab_ptr);
  return get_free_obj(kmc);
}

// free the given object and add it to slab freelist, if slab is empty after
// freeing the object, remove the slab from cache and free the page
// only for large objects
bool lslub_free(struct lslub_state_s *state, void *ptr) {

  if (ptr == NULL)
    return false;

  void *page_addr = round_to_page_boundary(ptr, state->page_size);
  kmem_slab_t *slab_ptr = slub_rb_search(&slab_cache_map, (void *)page_addr);

  if (slab_ptr == NULL)
    return false;

  if (slab_ptr->slab_addr != page_addr)
    return false;

  struct kmem_obj *obj_ptr = (struct kmem_obj *)ptr;
  kmem_cache_t *cache_ptr = slab_ptr->cache;

  // if slab is active slab or partial slab
  if (slab_ptr->obj != NULL || cache_ptr->active == slab_ptr) {

    // if slab is empty, remove slab from cache and free the page
    if (slab_ptr->aloc_obj == 1) {

      // if slab is partial slab, decrement npartial count
      if (cache_ptr->active != slab_ptr) {
        cache_ptr->npartial--;
        remove_slab(&cache_ptr->partial, slab_ptr);
      } else {
        cache_ptr->freelist = NULL;
        remove_slab(&cache_ptr->active, slab_ptr);
      }

      cache_ptr->nslabs--;
      state->free_page(page_addr);
      slub_rb_delete(&slab_cache_map, (void *)page_addr);
      free_obj(slab_ptr);
      return true;
    }

    // else add the object to slab freelist
    add_obj_to_freelist(slab_ptr, obj_ptr);
    update_freelist_ptr(cache_ptr);
    return true;
  }

  // if slab is full slab, add the object to slab freelist, move the slab to
  // partial slab list
  add_obj_to_freelist(slab_ptr, obj_ptr);
  remove_slab(&cache_ptr->full, slab_ptr);
  mv_slab_to_partial(cache_ptr, slab_ptr);
  return true;
}
