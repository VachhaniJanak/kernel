#include "../debug.h"

#include <mm/pmm/buddy.h>
#include <mm/pmm/pmm.h>

#include <stdbool.h>
#include <stdint.h>

#include <utils/log.h>
#include <utils/utils.h>

buddy_t usable_memory = {0};

buddy_t *get_buddy(void) { return &usable_memory; }

bool init_pmm(uintptr_t usable_addr, size_t usable_size, size_t page_size,
              void *(*phys_to_virt)(void *)) {

  size_t no_usable_pages = usable_size / page_size;

  // calculate pages required by buddy header metadata

  size_t metadata_size = no_usable_pages * sizeof(block_t);
  size_t max_order = log2(usable_size / page_size) + 1;
  size_t free_area_size = max_order * sizeof(block_t *);
  size_t total_header_size = metadata_size + free_area_size;

  if (total_header_size >= usable_size) {
    LOG_ERROR("Not enough memory for buddy allocator.");
    return false;
  }

  // number of pages required for header
  size_t pages_required = round_to_page(total_header_size, page_size);

  // free space after
  size_t free_size = usable_size - (pages_required * page_size);
  uintptr_t new_base_addr = usable_addr + (pages_required * page_size);

  block_t *metadata_addr = (block_t *)usable_addr;
  block_t **free_area_addr = (block_t **)(usable_addr + metadata_size);

  metadata_addr = (block_t *)phys_to_virt(metadata_addr);
  free_area_addr = (block_t **)phys_to_virt(free_area_addr);

  buddy_init(&usable_memory, (void *)new_base_addr, metadata_addr,
             free_area_addr, free_size, page_size);

#ifdef DEBUG
  LOG_NEWLINE();
  LOG_DEBUG("[PMM] Free space base addr : 0x%lx\n", usable_addr);

  if (usable_size < (1UL << 20)) {
    LOG_DEBUG("[PMM] Total free space     : %lu Bytes\n", usable_size);
  } else if (usable_size < (1UL << 30)) {
    LOG_DEBUG("[PMM] Total free space     : %lu MiB\n",
              usable_size / (1UL << 20));
  } else {
    LOG_DEBUG("[PMM] Total free space     : %lu GiB\n",
              usable_size / (1UL << 30));
  }

  if (total_header_size < (1UL << 20)) {
    LOG_DEBUG("[PMM] Header required space: %lu Bytes\n", total_header_size);
  } else if (total_header_size < (1UL << 30)) {
    LOG_DEBUG("[PMM] Header required space: %lu MiB\n",
              total_header_size / (1UL << 20));
  } else {
    LOG_DEBUG("[PMM] Header required space: %lu GiB\n",
              total_header_size / (1UL << 30));
  }

  if (free_size < (1UL << 20)) {
    LOG_DEBUG("[PMM] Usable space         : %lu Bytes\n", free_size);

  } else if (free_size < (1UL << 30)) {
    LOG_DEBUG("[PMM] Usable space         : %lu MiB\n",
              free_size / (1UL << 20));
  } else {
    LOG_DEBUG("[PMM] Usable space         : %lu GiB\n",
              free_size / (1UL << 30));
  }

  print_buddy_state(&usable_memory);
#endif

  return true;
}
