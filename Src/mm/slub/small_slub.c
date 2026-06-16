#include "debug.h"
#include "slub.h"
#include "slub_utils.h"
#include <stdio.h>

// predefined caches for small objects
static kmem_cache_t small_pre_caches[NO_SMALL_PRE_CACHES];
static char kmem_cache_names[NO_SMALL_PRE_CACHES][16];

// create predefined caches for small objects
void create_small_caches(void) {
  for (size_t i = 0; i < NO_SMALL_PRE_CACHES; i++) {
    size_t cache_size = MIN_SIZE_SMALL_OBJECTS + i * 8;

    snprintf(kmem_cache_names[i], sizeof(kmem_cache_names[i]), "kmem_cache-%d",
             cache_size);
    small_pre_caches[i].name = kmem_cache_names[i];
    small_pre_caches[i].size = cache_size;
    small_pre_caches[i].freelist = NULL;
    small_pre_caches[i].active = NULL;
    small_pre_caches[i].partial = NULL;
    small_pre_caches[i].full = NULL;
    small_pre_caches[i].npartial = 0;
    small_pre_caches[i].nslabs = 0;
  }
}

// return index of the cache for the given size
// size must be less than or equal to 192
static inline size_t size_to_idx(size_t size) { return (size - 1) / 8; }

// initialize the slab
static inline bool init_slab(void *page_addr, size_t obj_size,
                             size_t page_size) {

  void *obj_ptr = page_addr + sizeof(struct kmem_slab);
  size_t available_size = page_size - sizeof(struct kmem_slab);
  struct kmem_slab *slab_ptr = (struct kmem_slab *)page_addr;

  slab_ptr->prv = NULL;
  slab_ptr->nxt = NULL;
  slab_ptr->cache = NULL;
  slab_ptr->aloc_obj = 0;
  slab_ptr->obj = obj_ptr;
  slab_ptr->slab_addr = page_addr;

  if (!init_obj(obj_ptr, available_size, obj_size)) {
    slab_ptr->obj = NULL;
    return false;
  }
  return true;
}

// return cache for the given size
static inline kmem_cache_t *get_cache(size_t size) {
  if (size <= MAX_SIZE_SMALL_OBJECTS) {
    size_t idx = size_to_idx(size);
    return &small_pre_caches[idx];
  }
  return NULL;
}

// return object from given cache freelist if available, otherwise create new
// slab only for small objects
void *_small_kmalloc(size_t size) {

  kmem_cache_t *kmc = NULL;
  kmc = get_cache(size);

  if (kmc == NULL)
    return NULL;

  // if freelist is not empty, return object from freelist
  if (kmc->freelist != NULL)
    return get_free_obj(kmc);

  // if partial slab is not empty, move partial slab to active slab and return
  // object from active slab
  if (kmc->partial != NULL) {

    // if active slab is not empty, move active slab to full slab list
    if (kmc->active != NULL)
      mv_active_to_full(kmc);

    mv_partial_to_active(kmc);
    return get_free_obj(kmc);
  }

  // create new slab and return object from new slab
  if (kmc->active != NULL)
    mv_active_to_full(kmc);

  void *ptr = get_page(PAGE_SIZE);
  if (ptr == NULL)
    return NULL;

  if (!init_slab(ptr, kmc->size, PAGE_SIZE)) {
    free_page(ptr, PAGE_SIZE);
    return NULL;
  }

  set_new_active(kmc, (kmem_slab_t *)ptr);
  return get_free_obj(kmc);
}

// free the given object and add it to slab freelist, if slab is empty after
// freeing the object, remove the slab from cache and free the page
// only for small objects
void _small_kfree(void *ptr) {

  if (ptr == NULL)
    return;

  void *page_addr = round_to_page_boundary(ptr);
  kmem_slab_t *slab_ptr = (kmem_slab_t *)page_addr;

  if (slab_ptr->slab_addr != page_addr)
    return;

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
      free_page(page_addr, PAGE_SIZE);
      return;
    }

    // else add the object to given slab freelist
    add_obj_to_freelist(slab_ptr, obj_ptr);
    update_freelist_ptr(cache_ptr);
    return;
  }

  // if slab is full slab, add the object to slab freelist, move the slab to
  // partial slab list
  add_obj_to_freelist(slab_ptr, obj_ptr);
  remove_slab(&cache_ptr->full, slab_ptr);
  mv_slab_to_partial(cache_ptr, slab_ptr);
  return;
}
