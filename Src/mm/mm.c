#include <arch/x86_64/mmu.h>
#include <boot/boot.h>
#include <kernel.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/vmm/kheap.h>
#include <mm/vmm/vmm.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

#include "debug.h"

static struct mm_state_s mm_state;
uintptr_t hhdm_offset;

void get_max_len(struct MemoryMapEntry_s* entries, size_t noEntries,
                 uint8_t type, size_t* max_usable_length,
                 uintptr_t* max_usable_base) {
  *max_usable_length = 0;
  *max_usable_base = 0;

  for (size_t i = 0; i < noEntries; i++) {
    if (type != entries[i].type) continue;

    if (entries[i].length > *max_usable_length) {
      *max_usable_length = entries[i].length;
      *max_usable_base = entries[i].base;
    }
  }
}

bool mm_setup_kstack(void) {
  mm_state.stack_state.cursor = (void*)mm_state.kernel_stack_base;
  mm_state.stack_state.fragment_list = NULL;

  const uint64_t flags = MMU_PRESENT | MMU_WRITABLE;
  const size_t page_size = mm_state.page_size;
  const size_t stack_size =
      round_to_page_size(mm_state.kernel_stack_size, page_size);
  const size_t no_pages = stack_size / mm_state.page_size;

  // check if stack is aligned to page size
  if ((uintptr_t)mm_state.stack_state.cursor % page_size != 0) {
    LOG_ERROR("[MM] Kernel stack base address is not aligned to page size.");
    return false;
  }

  // allocate stack from top to bottom
  void* base_addr = mm_state.stack_state.cursor - stack_size;
  uint8_t* vir_addr = (uint8_t*)base_addr;

  // allocate non continues physical pages
  for (size_t i = 0; i < no_pages; i++) {
    uint8_t* v_addr = vir_addr + page_size * i;
    void* p_addr = pmm_alloc(page_size);

    if (p_addr != NULL) {
      map_page(v_addr, p_addr, flags, phys_to_virt);
      continue;
    }

    // rollback
    for (size_t j = 0; j < i; j++) {
      uint8_t* v_addr = vir_addr + page_size * j;

      void* p_addr = unmap_page(v_addr, phys_to_virt, virt_to_phys);
      if (p_addr != NULL) pmm_free(p_addr);
    }

    LOG_ERROR("[MM] Failed to allocate physical page for kernel stack.");
    return false;
  }

  mm_state.stack_state.cursor = base_addr;
  return true;
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

  mm_state.kernel_heap_base = KERNEL_HEAP_BASE;
  mm_state.kernel_heap_size = KERNEL_HEAP_SIZE;

  mm_state.kernel_stack_base = KERNEL_STACK_BASE;
  mm_state.kernel_stack_size = KERNEL_STACK_SIZE;

  if (!init_vmm(&mm_state)) {
    LOG_ERROR("[MM] VMM initialization failed!");
    return -1;
  }

  // init kernel heap
  init_kheap(&mm_state);

  if (!mm_setup_kstack()) {
    LOG_ERROR("[MM] Kernel stack setup failed!");
    return -1;
  }

  // print_buddy_state(get_buddy());

  return 0;
}

bool mmap(void* virt_addr, void* phys_addr) {
  uint64_t flags = MMU_PRESENT | MMU_WRITABLE;
  return map_page(virt_addr, phys_addr, flags, phys_to_virt);
}

void ummap(void* virt_addr) {
  unmap_page(virt_addr, phys_to_virt, virt_to_phys);
}
