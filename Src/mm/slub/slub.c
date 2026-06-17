#include <mm/slub/slub.h>

#include <stddef.h>
#include <stdint.h>

#include "debug.h"

void create_small_caches(struct sslub_state_s *state);
void *so_slub_alloc(struct sslub_state_s *state, size_t size);
void so_slub_free(struct sslub_state_s *state, void *ptr);

void create_large_caches(struct lslub_state_s *state);
void *so_large_kmalloc(struct lslub_state_s *state, size_t size);
void so_large_kfree(struct lslub_state_s *state, void *ptr);

void kmalloc_init(void) {
  create_small_caches();
  create_large_caches();
}

void *kmalloc(size_t size) {
  if (size <= MAX_SIZE_SMALL_OBJECTS)
    return so_slub_alloc(NULL, size);

  if (size <= MAX_SIZE_LARGE_OBJECTS)
    return _large_kmalloc(size);

  return get_page(size);
}

void kfree(void *ptr, size_t size) {
  if (ptr == NULL || size == 0)
    return;

  if (size <= MAX_SIZE_SMALL_OBJECTS)
    return _small_kfree(ptr);

  if (size <= MAX_SIZE_LARGE_OBJECTS)
    return _large_kfree(ptr);

  free_page(ptr, size);
}
