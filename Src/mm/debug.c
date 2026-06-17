#include "debug.h"

#include <mm/pmm/buddy.h>
#include <mm/slub/slub.h>

#include <utils/log.h>

#include <kernel.h>

void print_buddy_state(buddy_t *buddy) {
  LOG_NEWLINE();
  LOG_DEBUG("Buddy Allocator State:\n");
  LOG_DEBUG("Base Address    : 0x%lx\n", buddy->base);

  if (buddy->total_size < (1UL << 20)) {
    LOG_DEBUG("Total Size      : %lu Bytes\n", buddy->total_size);
  } else if (buddy->total_size < (1UL << 30)) {
    LOG_DEBUG("Total Size      : %lu MiB\n", buddy->total_size / (1UL << 20));
  } else {
    LOG_DEBUG("Total Size      : %lu GiB\n", buddy->total_size / (1UL << 30));
  }

  if (buddy->page_size < (1UL << 10)) {
    LOG_DEBUG("Page Size       : %lu Bytes\n", buddy->page_size);
  } else if (buddy->page_size < (1UL << 20)) {
    LOG_DEBUG("Page Size       : %lu KiB\n", buddy->page_size / (1UL << 10));
  } else {
    LOG_DEBUG("Page Size       : %lu MiB\n", buddy->page_size / (1UL << 20));
  }

  LOG_DEBUG("Max Order       : %zu\n", buddy->max_order);
  LOG_DEBUG("Number of Pages : %zu\n", buddy->num_pages);
  LOG_DEBUG("Free Pages      : %zu\n", buddy->free_pages);

  LOG_NEWLINE();
  LOG_DEBUG("Free Blocks by Order:\n");

  for (size_t i = 0; i <= buddy->max_order; i++) {
    LOG_DEBUG("Order %.3zu: ", i);
    block_t *blk = buddy->free_area[i];
    LOG_PRINT("[");
    while (blk) {
      uintptr_t addr =
          (uintptr_t)buddy->base + ((blk - buddy->metadata) * buddy->page_size);
      LOG_PRINT("%p ", (void *)addr);
      blk = blk->nxt;
    }
    LOG_PRINT("]");
    LOG_NEWLINE();
  }
}

void print_kernel_addr(void) {
  LOG_NEWLINE();
  LOG_DEBUG("Kernel Memory Map:\n");
  LOG_DEBUG("Kernel Base Address    : %p\n", (void *)KERNEL_VIRTUAL_BASE);
  LOG_DEBUG("Kernel Heap Address    : %p\n", (void *)KERNEL_HEAP_BASE);
  LOG_DEBUG("Kernel VMalloc Address : %p\n", (void *)KERNEL_VMALLOC_BASE);
  LOG_DEBUG("Kernel Stack Address   : %p\n", (void *)KERNEL_STACK_BASE);
}

void print_slab_list(struct kmem_slab *p) {
  struct kmem_slab *ptr_s = p;
  LOG_PRINT("[");
  while (ptr_s != NULL) {
    LOG_PRINT("(%p, %p, %p, %ld), ", ptr_s->prv, ptr_s, ptr_s->nxt,
              ptr_s->aloc_obj);
    ptr_s = ptr_s->nxt;
  }
  LOG_PRINT("]\n");
}

void print_slub_kmem_cache(struct kmem_cache *p) {
  LOG_NEWLINE();
  LOG_DEBUG("------------------ kmem_cache Info ------------------- \n");
  LOG_PRINT("Name          : %s \n", p->name);
  LOG_PRINT("Obj Size      : %d \n", p->size);
  LOG_PRINT("No Partial    : %d \n", p->npartial);
  LOG_PRINT("No Slabs      : %d \n", p->nslabs);
  LOG_PRINT("Freelist Ptr  : %p \n", p->freelist);
  LOG_PRINT("Active Ptr    : ");
  print_slab_list(p->active);
  LOG_PRINT("Partial Ptr   : ");
  print_slab_list(p->partial);
  LOG_PRINT("Full Ptr      : ");
  print_slab_list(p->full);
  LOG_DEBUG("-------------------------------------------------------- \n");
}

void print_slub_kmem_slab(struct kmem_slab *p) {
  LOG_NEWLINE();
  LOG_DEBUG("------------------ kmem_slab Info ------------------- \n");
  LOG_PRINT("Cache Ptr : %p \n", p->cache);
  LOG_PRINT("Obj Ptr   : %p \n", p->obj);
  LOG_PRINT("Slab Addr : %p \n", p->slab_addr);
  LOG_PRINT("Aloc Obj  : %ld \n", p->aloc_obj);
  LOG_DEBUG("-------------------------------------------------------- \n");
}

void print_slub_kmem_obj(struct kmem_obj *p) {
  LOG_NEWLINE();
  LOG_DEBUG("------------------ kmem_obj Info ------------------- \n");

  struct kmem_obj *ptr_o = p;
  LOG_PRINT("[");
  while (ptr_o != NULL) {
    LOG_PRINT("(%p, %p), ", ptr_o, ptr_o->nxt);
    ptr_o = ptr_o->nxt;
  }
  LOG_PRINT("]\n");

  LOG_DEBUG("-------------------------------------------------------- \n");
}
