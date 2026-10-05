#include <mm/kheap/slub.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <process/locks.h>
#include <stddef.h>
#include <stdint.h>

void kmem_cache_init(kmem_cache_t* cache, size_t object_size) {
  cache->object_size = object_size;

  // Align object size to 8 bytes
  cache->object_size_aligned = (object_size + 7) & ~7;

  if (cache->object_size_aligned < 8) {
    cache->object_size_aligned = 8;
  }

  size_t usable_space = MM_DEFAULT_PAGE_SIZE;
  cache->objects_per_slab = usable_space / cache->object_size_aligned;

  cache->partial_slabs = NULL;
  cache->full_slabs = NULL;

  spinlock_init(&cache->lock);
}

static frame_info_t* allocate_and_format_slab(kmem_cache_t* cache,
                                              struct pmm_state_s* pmm) {
  const size_t page_size = pmm->page_size;

  void* phys_page = pmm_alloc(pmm, page_size);

  if (!phys_page) {
    return NULL;
  }

  frame_info_t* frame = pmm_get_frame_info(pmm, phys_page);

  frame->page_type = PAGE_TYPE_SLAB;
  frame->inuse = 0;
  frame->slab.cache = cache;
  frame->slab.next = NULL;
  frame->slab.prev = NULL;

  // convert physical address to virtual address for the freelist
  void* virt_page = phys_page + pmm->hhdm_offset;
  frame->slab.freelist = virt_page;

  void** current = (void**)virt_page;

  for (size_t i = 0; i < cache->objects_per_slab - 1; i++) {
    uintptr_t next_obj = (uintptr_t)current + cache->object_size;
    *current = (void*)next_obj;
    current = (void**)next_obj;
  }

  *current = NULL;

  return frame;
}

void* kmem_cache_alloc(kmem_cache_t* cache, struct pmm_state_s* pmm) {
  unsigned long flags;
  spinlock_acquire(&cache->lock, &flags);

  frame_info_t* frame = cache->partial_slabs;

  if (!frame) {
    frame = allocate_and_format_slab(cache, pmm);

    if (!frame) {
      spinlock_release(&cache->lock, flags);
      return NULL;
    }

    frame->slab.next = cache->partial_slabs;

    if (cache->partial_slabs) {
      cache->partial_slabs->slab.prev = frame;
    }

    cache->partial_slabs = frame;
  }

  void* obj = frame->slab.freelist;
  frame->slab.freelist = *(void**)obj;
  frame->inuse++;

  // If the slab is now full, move it from partial to full list
  if (frame->slab.freelist == NULL) {
    // Remove from partial
    cache->partial_slabs = frame->slab.next;

    if (cache->partial_slabs) {
      cache->partial_slabs->slab.prev = NULL;
    }

    // Add to full
    frame->slab.next = cache->full_slabs;
    frame->slab.prev = NULL;

    if (cache->full_slabs) {
      cache->full_slabs->slab.prev = frame;
    }

    cache->full_slabs = frame;
  }

  spinlock_release(&cache->lock, flags);
  return obj;
}

void kmem_cache_free(void* virt_obj, struct pmm_state_s* pmm) {
  if (!virt_obj) {
    return;
  }

  // Find the base virtual address of the page
  uint64_t virt_page = (uint64_t)virt_obj & ~0xFFF;

  // Translate to physical to get the absolute frame index
  uint64_t phys_page = virt_page - pmm->hhdm_offset;
  frame_info_t* frame = pmm_get_frame_info(pmm, (void*)phys_page);

  unsigned long flags;
  spinlock_acquire(&frame->slab.cache->lock, &flags);

  // Safety check
  if (frame->page_type != PAGE_TYPE_SLAB) {
    spinlock_release(&frame->slab.cache->lock, flags);
    return;
  }

  // Push object back onto the freelist
  *(void**)virt_obj = frame->slab.freelist;
  frame->slab.freelist = virt_obj;
  frame->inuse--;

  struct kmem_cache_s* cache = frame->slab.cache;

  // If the slab was previously full, it is now partial. Move it back.
  if (frame->inuse == cache->objects_per_slab - 1) {
    // Remove from full list
    if (frame->slab.prev) {
      frame->slab.prev->slab.next = frame->slab.next;
    } else {
      cache->full_slabs = frame->slab.next;
    }

    if (frame->slab.next) {
      frame->slab.next->slab.prev = frame->slab.prev;
    }

    // Add to partial list
    frame->slab.next = cache->partial_slabs;
    frame->slab.prev = NULL;

    if (cache->partial_slabs) {
      cache->partial_slabs->slab.prev = frame;
    }

    cache->partial_slabs = frame;
  }

  // If the slab is completely empty, free it
  if (frame->inuse == 0) {
    // Remove from partial list
    if (frame->slab.prev) {
      frame->slab.prev->slab.next = frame->slab.next;
    } else {
      cache->partial_slabs = frame->slab.next;
    }

    if (frame->slab.next) {
      frame->slab.next->slab.prev = frame->slab.prev;
    }

    frame->page_type = PAGE_TYPE_FREE;
    frame->slab.cache = NULL;
    pmm_free(pmm, (void*)phys_page);
  }

  spinlock_release(&cache->lock, flags);
}
