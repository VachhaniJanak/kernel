#include "debug.h"
#include "buddy/buddy.h"
#include <utils/log.h>
#include <kernel.h>

void print_buddy_state(buddy_t *buddy) {
  LOG_NEWLINE();
  LOG_DEBUG("Buddy Allocator State:\n");
  LOG_DEBUG("Base Address    : 0x%lx\n", buddy->base);

  if (buddy->total_size < (1UL << 20)){  
    LOG_DEBUG("Total Size      : %lu Bytes\n", buddy->total_size);
  }else if (buddy->total_size < (1UL << 30)){   
    LOG_DEBUG("Total Size      : %lu MiB\n", buddy->total_size / (1UL << 20));
  }else{    
    LOG_DEBUG("Total Size      : %lu GiB\n", buddy->total_size / (1UL << 30));
  }

  if (buddy->page_size < (1UL << 10)){
    LOG_DEBUG("Page Size       : %lu Bytes\n", buddy->page_size);
  }else if (buddy->page_size < (1UL << 20)){
    LOG_DEBUG("Page Size       : %lu KiB\n", buddy->page_size / (1UL << 10));
  }else{
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
    while (blk) {
      uintptr_t addr =
          (uintptr_t)buddy->base + ((blk - buddy->metadata) * buddy->page_size);
      LOG_PRINT("%p ", (void *)addr);
      blk = blk->nxt;
    }
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