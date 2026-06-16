#include "slub.h"
#include <stddef.h>
#include <stdint.h>

#include "debug.h"

void create_small_caches(void);
void *_small_kmalloc(size_t size);
void _small_kfree(void *ptr);

void create_large_caches(void);
void *_large_kmalloc(size_t size);
void _large_kfree(void *ptr);

void kmalloc_init(void) {
  create_small_caches();
  create_large_caches();
}

void *kmalloc(size_t size) {
  if (size <= MAX_SIZE_SMALL_OBJECTS)
    return _small_kmalloc(size);

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


