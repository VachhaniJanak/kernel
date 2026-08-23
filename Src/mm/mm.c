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

void* mm_get_kernel_root_table(void) { return mm_state.kernel_root_table; }

size_t mm_get_page_size(void) { return mm_state.page_size; }

void* mm_get_user_stack_base(void) { return (void*)mm_state.user_stack_base; }

void* mm_get_user_mmap_base(void) { return (void*)mm_state.user_mmap_base; }

size_t mm_get_user_stack_size(void) { return mm_state.user_stack_size; }

size_t mm_get_user_mmap_size(void) { return mm_state.user_mmap_size; }

void* mm_get_root_table(void) {
  uintptr_t root_table_phys = get_page_table_addr();
  return phys_to_virt((void*)root_table_phys);
}

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

  void* root_table = mm_get_root_table();
  const mm_flags_t flags = MMU_WRITABLE;
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
      map_page(root_table, v_addr, p_addr, flags);
      continue;
    }

    // rollback
    for (size_t j = 0; j < i; j++) {
      uint8_t* v_addr = vir_addr + page_size * j;

      uintptr_t p_addr;
      mm_result_t result = unmap_page(root_table, v_addr, &p_addr);
      if (result == MM_SUCCESS) pmm_free((void*)p_addr);
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

#ifdef MM_DEBUG
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

  mm_state.user_stack_base = USER_STACK_BASE;
  mm_state.user_stack_size = USER_STACK_SIZE;
  mm_state.user_kernel_stack_size = USER_KERNEL_STACK_SIZE;

  mm_state.user_mmap_base = USER_MMAP_BASE;
  mm_state.user_mmap_size = USER_MMAP_SIZE;

  // set kernel root table
  mm_state.kernel_root_table = (void*)get_page_table_addr();

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

  return 0;
}

bool mmap(void* virt_addr, void* phys_addr) {
  mm_flags_t flags = MMU_WRITABLE;
  void* root_table = mm_get_root_table();
  mm_result_t result = map_page(root_table, virt_addr, phys_addr, flags);
  return result == MM_SUCCESS;
}

void ummap(void* virt_addr) {
  void* root_table = mm_get_root_table();

  uintptr_t phys_addr;
  mm_result_t result = unmap_page(root_table, virt_addr, &phys_addr);

  if (result == MM_SUCCESS) {
    pmm_free((void*)phys_addr);
  }
}

mm_result_t mm_create_page_table(uintptr_t* user_root_table) {
  void* kernel_root_table_phy = mm_get_kernel_root_table();
  void* user_root_table_phy = pmm_alloc(PML4_SIZE);

  if (user_root_table_phy == NULL) {
    return MM_ERR_OUT_OF_MEMORY;
  }

  void* kernel_root_table_virt = phys_to_virt(kernel_root_table_phy);
  void* user_root_table_virt = phys_to_virt(user_root_table_phy);

  // Copy kernel mappings to user root table
  kmemcpy(user_root_table_virt, kernel_root_table_virt, PML4_SIZE);

  if (user_root_table == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  *user_root_table = (uintptr_t)user_root_table_phy;

  return MM_SUCCESS;
}

mm_result_t mm_allocate_user_stacks(void* root_table,
                                    uintptr_t* user_stack_base,
                                    uintptr_t* kernel_stack_base) {
  if (root_table == NULL || user_stack_base == NULL ||
      kernel_stack_base == NULL) {
    return MM_ERR_INVALID_PAGE_TABLE;
  }

  const size_t page_size = mm_state.page_size;

  mm_flags_t u_flags = MM_FLAG_WRITABLE | MM_FLAG_USER;
  mm_flags_t k_flags = MM_FLAG_WRITABLE;

  void* kernel_stack_virt = (void*)mm_state.user_stack_base;
  kernel_stack_virt = PAGE_ALIGN_UP(kernel_stack_virt, page_size);
  *kernel_stack_base = (uintptr_t)kernel_stack_virt;

  size_t kernel_stack_size = mm_state.user_kernel_stack_size;
  kernel_stack_size = round_to_page_size(kernel_stack_size, page_size);

  // boundary between user stack and kernel stack
  uintptr_t boundary = kernel_stack_size;
  boundary += mm_state.page_size;  // leave a guard page

  void* user_stack_virt = (void*)((uintptr_t)kernel_stack_virt - boundary);
  user_stack_virt = PAGE_ALIGN_DOWN(user_stack_virt, page_size);
  *user_stack_base = (uintptr_t)user_stack_virt;

  // stack grows downwards, so we need to allocate the last page first
  user_stack_virt = (void*)((uintptr_t)user_stack_virt - page_size);

  // Allocate user stack
  void* user_stack_page = pmm_alloc(page_size);

  if (user_stack_page == NULL) {
    return MM_ERR_OUT_OF_MEMORY;
  }

  // Map user stack
  mm_result_t result;

  result = map_page(root_table, user_stack_virt, user_stack_page, u_flags);

  if (result != MM_SUCCESS) {
    pmm_free(user_stack_page);
    return result;
  }

  const size_t num_pages = kernel_stack_size / page_size;

  for (size_t i = 0; i < num_pages; i++) {
    void* kernel_stack_page = pmm_alloc(kernel_stack_size);

    if (kernel_stack_page != NULL) {
      // stack grows downwards, so we need to allocate the last page first
      kernel_stack_virt = (void*)((uintptr_t)kernel_stack_virt - page_size);

      result =
          map_page(root_table, kernel_stack_virt, kernel_stack_page, k_flags);

      if (result == MM_SUCCESS) continue;
    }

    // rollback user stack allocation
    uintptr_t phys_addr;
    result = unmap_page(root_table, user_stack_virt, &phys_addr);

    if (result == MM_SUCCESS) pmm_free((void*)phys_addr);

    // rollback kernel stack allocation
    for (size_t j = 0; j < i; j++) {
      void* kernel_stack_page =
          (void*)((uintptr_t)kernel_stack_virt + page_size);

      uintptr_t phys_addr;

      result = unmap_page(root_table, kernel_stack_page, &phys_addr);

      if (result == MM_SUCCESS) pmm_free((void*)phys_addr);
    }

    return MM_ERR_OUT_OF_MEMORY;
  }

  return MM_SUCCESS;
}

uint64_t mm_get_mmu_flags(mm_flags_t flags) {
  uint64_t mmu_flags = 0;

  if (flags & MM_FLAG_READ) mmu_flags |= 0;
  if (flags & MM_FLAG_WRITABLE) mmu_flags |= MMU_WRITABLE;
  if (!(flags & MM_FLAG_EXE)) mmu_flags |= MMU_NO_EXECUTE;
  if (flags & MM_FLAG_USER) mmu_flags |= MMU_USER_MEMORY;
  if (flags & MM_FLAG_4KB) mmu_flags |= 0;
  if (flags & MM_FLAG_2MB) mmu_flags |= MMU_HUGE_PAGE;
  if (flags & MM_FLAG_1GB) mmu_flags |= MMU_HUGE_PAGE;

  return mmu_flags;
}
