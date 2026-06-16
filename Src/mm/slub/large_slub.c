#include "debug.h"
#include "slub.h"
#include "slub_utils.h"
#include <stdio.h>

void *_small_kmalloc(size_t size);
void _small_kfree(void *ptr);

kmem_cache_t large_pre_caches[NO_LARGE_CACHES];
static char kmem_cache_names[NO_LARGE_CACHES][16];
static HashMap_t slab_cache_map;

// create predefined caches for large objects
void create_large_caches(void) {
  hashMapInit(&slab_cache_map);

  for (size_t i = 0; i < NO_LARGE_CACHES; i++) {
    size_t cache_size = MIN_SIZE_LARGE_OBJECTS + i * 8;

    snprintf(kmem_cache_names[i], sizeof(kmem_cache_names[i]), "kmem_cache-%d",
             cache_size);

    large_pre_caches[i].name = kmem_cache_names[i];
    large_pre_caches[i].size = cache_size;
    large_pre_caches[i].freelist = NULL;
    large_pre_caches[i].active = NULL;
    large_pre_caches[i].partial = NULL;
    large_pre_caches[i].full = NULL;
    large_pre_caches[i].npartial = 0;
    large_pre_caches[i].nslabs = 0;
  }
}

// return index of the cache for the given size
static inline size_t size_to_idx(size_t size) {
  int idx = (size - 1) / 8;
  int offset = (MIN_SIZE_LARGE_OBJECTS - 8) / 8;
  idx -= offset;
  return idx;
}

// initialize the slab
static inline kmem_slab_t *init_slab(void *page_addr, size_t obj_size,
                                     size_t page_size) {

  kmem_slab_t *slab_ptr;
  slab_ptr = (kmem_slab_t *)_small_kmalloc(sizeof(kmem_slab_t));

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

  hashMapInsert(&slab_cache_map, (uint64_t)page_addr, (uint64_t)slab_ptr);
  return slab_ptr;
}

// return cache for the given size
static inline struct kmem_cache *get_cache(uint32_t size) {
  if (size <= MAX_SIZE_LARGE_OBJECTS) {
    int idx = size_to_idx(size);
    return &large_pre_caches[idx];
  }
  return NULL;
}

void *_large_kmalloc(size_t size) {

  struct kmem_cache *kmc = NULL;
  kmc = get_cache(size);
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
  void *ptr = get_page(PAGE_SIZE);
  if (ptr == NULL)
    return NULL;

  kmem_slab_t *slab_ptr = init_slab(ptr, kmc->size, PAGE_SIZE);
  if (slab_ptr == NULL) {
    free_page(ptr, PAGE_SIZE);
    return NULL;
  }

  set_new_active(kmc, slab_ptr);
  return get_free_obj(kmc);
}

// free the given object and add it to slab freelist, if slab is empty after
// freeing the object, remove the slab from cache and free the page
// only for large objects
void _large_kfree(void *ptr) {

  if (ptr == NULL)
    return;

  void *page_addr = round_to_page_boundary(ptr);
  kmem_slab_t *slab_ptr;
  slab_ptr = (kmem_slab_t *)hashMapSearch(&slab_cache_map, (uint64_t)page_addr);

  if (slab_ptr == 0)
    return;

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
      hashMapDelete(&slab_cache_map, (uint64_t)page_addr);
      _small_kfree(slab_ptr);
      return;
    }

    // else add the object to slab freelist
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
