#pragma once
#include <stdint.h>

#define PAGE_SIZE 4096
#define MAX_SIZE_LARGE_OBJECTS 4096
#define MIN_SIZE_LARGE_OBJECTS 200
#define NO_LARGE_CACHES                                                        \
  ((MAX_SIZE_LARGE_OBJECTS - MIN_SIZE_LARGE_OBJECTS) / 8 + 1)

#define MAX_SIZE_SMALL_OBJECTS 192
#define MIN_SIZE_SMALL_OBJECTS 8
#define NO_SMALL_PRE_CACHES                                                    \
  (MAX_SIZE_SMALL_OBJECTS - MIN_SIZE_SMALL_OBJECTS) / 8 + 1

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


void kmalloc_init(void);
void *kmalloc(unsigned long size);
void kfree(void *ptr, unsigned long size);
