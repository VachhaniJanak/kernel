#include <mm/kheap/slub.h>
#include <mm/pmm/pmm.h>
#include <stddef.h>
#include <utils/panic.h>

#define NUM_SMALL_OBJECT_CACHES 24
#define NUM_LARGE_OBJECT_CACHES 4

#define MAX_SMALL_OBJECT_SIZE 192
#define MAX_LARGE_OBJECT_SIZE 2048

static const size_t kmalloc_small_sizes[NUM_SMALL_OBJECT_CACHES] = {
    8,   16,  24,  32,  40,  48,  56,  64,  72,  80,  88,  96,
    104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184, 192};

static const size_t kmalloc_large_sizes[NUM_LARGE_OBJECT_CACHES] = {256, 512,
                                                                    1024, 2048};

static kmem_cache_t kmalloc_small_caches[NUM_SMALL_OBJECT_CACHES];
static kmem_cache_t kmalloc_large_caches[NUM_LARGE_OBJECT_CACHES];
static struct pmm_state_s* global_pmm;  // Store for global access

void kmalloc_init(struct pmm_state_s* pmm) {
  global_pmm = pmm;

  // Initialize small object caches
  for (int i = 0; i < NUM_SMALL_OBJECT_CACHES; i++) {
    kmem_cache_init(&kmalloc_small_caches[i], kmalloc_small_sizes[i]);
  }

  // Initialize large object caches
  for (int i = 0; i < NUM_LARGE_OBJECT_CACHES; i++) {
    kmem_cache_init(&kmalloc_large_caches[i], kmalloc_large_sizes[i]);
  }
}

static inline size_t size_to_idx(size_t size) { return (size - 1) / 8; }

void* kmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  }

  if (size > MAX_LARGE_OBJECT_SIZE) {
    void* phys_ptr = pmm_alloc(global_pmm, size);

    if (!phys_ptr) {
      return NULL;
    }

    uintptr_t virt_ptr = (uintptr_t)phys_ptr + global_pmm->hhdm_offset;
    return (void*)virt_ptr;
  }

  // Small Allocations
  if (size <= MAX_SMALL_OBJECT_SIZE) {
    size_t idx = size_to_idx(size);

    if (idx >= NUM_SMALL_OBJECT_CACHES) {
      return NULL;  // Out of bounds
    }

    return kmem_cache_alloc(&kmalloc_small_caches[idx], global_pmm);
  }

  // Large Allocations
  if (size <= MAX_LARGE_OBJECT_SIZE) {
    for (int i = 0; i < NUM_LARGE_OBJECT_CACHES; i++) {
      if (kmalloc_large_sizes[i] < size) {
        continue;  // Skip caches that are too small
      }

      return kmem_cache_alloc(&kmalloc_large_caches[i], global_pmm);
    }
  }

  return NULL;
}

void kfree(void* ptr) {
  if (!ptr) {
    return;
  }

  uint64_t virt_addr = (uint64_t)ptr;
  uint64_t virt_page = virt_addr & ~(global_pmm->page_size - 1);  // ~0xFFF
  uint64_t phys_page = virt_page - global_pmm->hhdm_offset;

  frame_info_t* frame = pmm_get_frame_info(global_pmm, (void*)phys_page);

  if (frame->page_type == PAGE_TYPE_SLAB) {
    // It belongs to a SLUB cache.
    kmem_cache_free(ptr, global_pmm);
  } else if (frame->page_type == PAGE_TYPE_ALLOC) {
    // It was a large allocation straight from the buddy allocator.
    pmm_free(global_pmm, (void*)phys_page);
  } else {
    kernel_panic(
        "kfree: Attempted to free a pointer that was not allocated by "
        "kmalloc.");
  }
}
