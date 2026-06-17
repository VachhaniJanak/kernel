#include "debug.h"

#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/vmm/vmm.h>

#include <arch/x86_64/mmu.h>
#include <boot/boot.h>
#include <kernel.h>

#include <stdint.h>

#include <utils/log.h>
#include <utils/utils.h>

static struct mm_state_s mm_state;
uintptr_t hhdm_offset;

void get_max_len(struct MemoryMapEntry_s *entries, size_t noEntries,
                 uint8_t type, size_t *max_usable_length,
                 uintptr_t *max_usable_base) {

  *max_usable_length = 0;
  *max_usable_base = 0;

  for (size_t i = 0; i < noEntries; i++) {

    if (type != entries[i].type)
      continue;

    if (entries[i].length > *max_usable_length) {
      *max_usable_length = entries[i].length;
      *max_usable_base = entries[i].base;
    }
  }
}

int mm_init(void) {

  const size_t noEntries = getMMapEntryCount();
  struct MemoryMapEntry_s entries[noEntries];

  if (!copyMMapEntry(entries)) {
    LOG_ERROR("[MM] Failed to copy memory map entries.");
    return -1;
  }

  hhdm_offset = getHHDMOffset();

  if (hhdm_offset == 0) {
    LOG_ERROR("[MM] Failed to get HHDM offset.");
    return -1;
  }

  size_t max_usable_length;
  uintptr_t max_usable_base;

  get_max_len(entries, noEntries, MEMMAP_USABLE, &max_usable_length,
              &max_usable_base);

  // check memory address is aligned to page size
  if (max_usable_base % MM_DEFAULT_PAGE_SIZE != 0) {
    LOG_ERROR(
        "[MM] Max usable memory base address is not aligned to page size.");
    return -1;
  }

  // init physical allocator
  if (!init_pmm(max_usable_base, max_usable_length, MM_DEFAULT_PAGE_SIZE,
                phys_to_virt)) {
    LOG_ERROR("[MM] Buddy initialization failed!");
    return -1;
  }

#ifdef DEBUG
  LOG_NEWLINE();
  LOG_DEBUG("HHDM Offset: 0x%lx\n", hhdm_offset);
  print_kernel_addr();
#endif

  mm_state.physical_memory_base = max_usable_base;
  mm_state.physical_memory_size = max_usable_length;

  mm_state.hhdm_offset = hhdm_offset;
  mm_state.page_size = MM_DEFAULT_PAGE_SIZE;

  // mm_state.kernel_phys_base = KERNEL_PHYS_BASE;
  mm_state.kernel_virt_base = KERNEL_VIRTUAL_BASE;
  // mm_state.kernel_size = KERNEL_SIZE;

  mm_state.kernel_vmalloc_base = KERNEL_VMALLOC_BASE;
  mm_state.kernel_vmalloc_size = KERNEL_VMALLOC_SIZE;

  mm_state.kernel_stack_base = KERNEL_STACK_BASE;
  mm_state.kernel_stack_size = KERNEL_STACK_SIZE;

  if (!init_vmm(&mm_state)) {
    LOG_ERROR("[MM] VMM initialization failed!");
    return -1;
  }

  debug_print_vmm_tree();

  size_t test_sizes[10] = {
      4096, 1048576, 131072, 1048576, 524288, 1048576, 262144, 32768, 65536, 131072,
  };

  const size_t count = 10;

  void *addr[10];

  for (size_t i = 0; i < count; i++) {
    addr[i] = vmalloc(test_sizes[i], MMU_WRITABLE | MMU_PRESENT, false);

    if (addr[i] == NULL) {
      LOG_ERROR("[MM] Failed to allocate memory of size %zu\n", test_sizes[i]);
      return -1;
    }

    kmemset(addr[i], 0, test_sizes[i]);
    LOG_DEBUG("[MM] Allocated memory of size %zu at address %p\n",
              test_sizes[i], addr[i]);
    // debug_print_vmm_tree();
  }

  print_buddy_state(get_buddy());

  for (size_t i = 0; i < count; i++) {
    // debug_print_size_classes();

    if (addr[i] == NULL) {
      LOG_ERROR("[MM] Invalid address for memory of size %zu\n", test_sizes[i]);
      return -1;
    }

    vfree(addr[i]);
    LOG_DEBUG("[MM] Freed memory of size %zu at address %p\n", test_sizes[i],
              addr[i]);

    // debug_print_vmm_tree();
  }

  print_buddy_state(get_buddy());
  return 0;
}
