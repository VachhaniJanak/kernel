#pragma once

#include <mm/pmm/pmm.h>
#include <process/locks.h>
#include <stddef.h>
#include <stdint.h>

// The Cache manager for a specific object size
typedef struct kmem_cache_s {
  char* name;                  // Name of the cache
  size_t object_size;          // Requested size
  size_t object_size_aligned;  // Size padded to 8 bytes for alignment
  size_t objects_per_slab;

  frame_info_t* partial_slabs;
  frame_info_t* full_slabs;
  spinlock_t lock;
} kmem_cache_t;

void kmem_cache_init(kmem_cache_t* cache, size_t object_size);

void* kmem_cache_alloc(kmem_cache_t* cache, struct pmm_state_s* pmm);

void kmem_cache_free(void* virt_obj, struct pmm_state_s* pmm);
