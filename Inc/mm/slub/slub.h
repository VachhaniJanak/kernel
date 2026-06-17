#pragma once

#include <stddef.h>
#include <stdint.h>

#define MAX_SIZE_LARGE_OBJECTS 4096
#define MIN_SIZE_LARGE_OBJECTS 200
#define NO_LARGE_CACHES                                                        \
  ((MAX_SIZE_LARGE_OBJECTS - MIN_SIZE_LARGE_OBJECTS) / 8 + 1)

#define MAX_SIZE_SMALL_OBJECTS 192
#define MIN_SIZE_SMALL_OBJECTS 8
#define NO_SMALL_PRE_CACHES                                                    \
  (MAX_SIZE_SMALL_OBJECTS - MIN_SIZE_SMALL_OBJECTS) / 8 + 1

#define MAX_NAME_SIZE 16

struct kmem_obj {
  struct kmem_obj *nxt;
};

struct kmem_slab {
  struct kmem_slab *prv;
  struct kmem_slab *nxt;
  struct kmem_cache *cache;
  struct kmem_obj *obj;
  void *slab_addr;
  long aloc_obj;
};

typedef struct kmem_slab kmem_slab_t;

struct kmem_cache {
  const char *name;
  uint16_t size;
  uint16_t npartial;
  uint16_t nslabs;
  struct kmem_obj *freelist;
  struct kmem_slab *active;
  struct kmem_slab *partial;
  struct kmem_slab *full;
};

typedef struct kmem_cache kmem_cache_t;

struct sslub_state_s {
  kmem_cache_t caches[NO_SMALL_PRE_CACHES];
  char names[NO_SMALL_PRE_CACHES][MAX_NAME_SIZE];
  size_t page_size;
  void *(*get_page)();
  void (*free_page)(void *addr);
};

struct lslub_state_s {
  kmem_cache_t caches[NO_LARGE_CACHES];
  char names[NO_LARGE_CACHES][MAX_NAME_SIZE];
};

void init_small_caches(struct sslub_state_s *state);
void *sslub_alloc(struct sslub_state_s *state, size_t size);
void sslub_free(struct sslub_state_s *state, void *ptr);

// void kmalloc_init(void);
// void *kmalloc(unsigned long size);
// void kfree(void *ptr, unsigned long size);
