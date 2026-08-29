#include <arch/x86_64/mmu.h>
#include <boot/boot.h>
#include <kernel.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
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

void* mm_get_user_virtual_base(void) {
  return (void*)mm_state.user_virtual_base;
}

void* mm_get_root_table(void) {
  uintptr_t root_table_phys = get_page_table_addr();
  return phys_to_virt((void*)root_table_phys);
}

size_t mm_get_kernel_thread_stack_size(void) {
  return mm_state.kernel_thread_stack_size;
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
      page_align_up(mm_state.kernel_stack_size, page_size);
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
  mm_state.kernel_thread_stack_size = KERNEL_THREAD_STACK_SIZE;

  mm_state.user_virtual_base = USER_VIRTUAL_BASE;
  mm_state.user_stack_base = USER_STACK_BASE;
  mm_state.user_stack_size = USER_STACK_SIZE;
  mm_state.user_kernel_stack_size = USER_KERNEL_STACK_SIZE;

  mm_state.user_mmap_base = USER_MMAP_BASE;

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

mm_result_t mm_allocate_kstack(void* root_table, uintptr_t* stack_base) {
  if (root_table == NULL || stack_base == NULL) {
    return MM_ERR_INVALID_PAGE_TABLE;
  }

  const size_t page_size = mm_state.page_size;
  size_t stack_size = page_align_up(mm_state.user_kernel_stack_size, page_size);

  // Allocate stack
  mm_flags_t flags = MM_FLAG_WRITABLE | MM_FLAG_USER;
  void* addr = vmalloc(stack_size, flags, false);

  if (addr == NULL) {
    return MM_ERR_OUT_OF_MEMORY;
  }

  *stack_base = (uintptr_t)addr + stack_size;  // Stack grows downwards
  return MM_SUCCESS;
}

mm_result_t mm_free_kstack(void* root_table, uintptr_t stack_base) {
  if (root_table == NULL) {
    return MM_ERR_INVALID_PAGE_TABLE;
  }

  const size_t page_size = mm_state.page_size;
  size_t stack_size = page_align_up(mm_state.user_kernel_stack_size, page_size);

  uintptr_t stack_start = stack_base - stack_size;
  vfree((void*)stack_start);

  return MM_SUCCESS;
}

mm_result_t mm_allocate_pstack(void* root_table, uintptr_t* stack_base) {
  if (root_table == NULL || stack_base == NULL) {
    return MM_ERR_INVALID_PAGE_TABLE;
  }

  const size_t page_size = mm_state.page_size;
  size_t stack_virt_addr = page_align_down(mm_state.user_stack_base, page_size);
  *stack_base = (uintptr_t)stack_virt_addr;

  // stack grows downwards, so we need to allocate the last page first
  stack_virt_addr -= page_size;

  // Allocate user stack
  void* phys_page = pmm_alloc(page_size);

  if (phys_page == NULL) {
    return MM_ERR_OUT_OF_MEMORY;
  }

  // Map user stack
  mm_result_t result;
  mm_flags_t flags = MM_FLAG_WRITABLE | MM_FLAG_USER;

  result = map_page(root_table, (void*)stack_virt_addr, phys_page, flags);

  if (result != MM_SUCCESS) {
    pmm_free(phys_page);
    return result;
  }

  return MM_SUCCESS;
}

mm_result_t mm_free_pstack(void* root_table, uintptr_t stack_base) {
  if (root_table == NULL) {
    return MM_ERR_INVALID_PAGE_TABLE;
  }

  // check if stack_base is aligned to page size
  if (!is_page_aligned(stack_base, mm_state.page_size)) {
    return MM_ERR_INVALID_ALIGNMENT;
  }

  const size_t page_size = mm_state.page_size;
  const size_t stack_size = page_align_up(mm_state.user_stack_size, page_size);

  uintptr_t stack_virt_addr = stack_base - stack_size;
  const size_t no_pages = stack_size / page_size;

  for (size_t i = 0; i < no_pages; i++) {
    uintptr_t page_virt_addr = stack_virt_addr + i * page_size;
    uintptr_t phys_addr = 0;

    mm_result_t result =
        unmap_page(root_table, (void*)page_virt_addr, &phys_addr);

    if (result == MM_SUCCESS && phys_addr != 0) {
      pmm_free((void*)phys_addr);
    }
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
