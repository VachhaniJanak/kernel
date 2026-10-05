#include <arch/x86_64/mmu.h>
#include <boot/boot.h>
#include <kernel.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

struct mm_state_s mm_state;
uintptr_t hhdm_offset = 0;

void* mm_get_kernel_root_table(void) { return mm_state.kernel_root_table; }

size_t mm_get_page_size(void) { return mm_state.page_size; }

void* mm_get_user_stack_base(void) { return (void*)mm_state.user_stack_base; }

void* mm_get_user_mmap_base(void) { return (void*)mm_state.user_mmap_base; }

size_t mm_get_user_stack_size(void) { return mm_state.user_stack_size; }

void* mm_get_user_virtual_base(void) {
  return (void*)mm_state.user_virtual_base;
}

// return virtual address of the current root page table
void* mm_get_root_table(void) {
  uintptr_t root_table_phys = get_page_table_addr();
  return phys_to_virt((void*)root_table_phys);
}

size_t mm_get_kernel_thread_stack_size(void) {
  return mm_state.kernel_thread_stack_size;
}

int mm_init(void) {
  memset(&mm_state, 0, sizeof(mm_state));

  mm_state.hhdm_offset = getHHDMOffset();
  mm_state.page_size = MM_DEFAULT_PAGE_SIZE;
  hhdm_offset = mm_state.hhdm_offset;

  if (mm_state.hhdm_offset == 0) {
    return -1;
  }

  // mm_state.kernel_phys_base = KERNEL_PHYS_BASE;
  mm_state.kernel_virt_base = KERNEL_VIRTUAL_BASE;
  // mm_state.kernel_size = KERNEL_SIZE;

  mm_state.vmalloc_base = VMALLOC_BASE;
  mm_state.vmalloc_size = VMALLOC_SIZE;

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

  int result =
      pmm_init(&mm_state.pmm_state, mm_state.page_size, mm_state.hhdm_offset);

  if (result < 0) {
    return -1;
  }

  kmalloc_init(&mm_state.pmm_state);

  result = vmm_init(&mm_state);

  if (result < 0) {
    return -1;
  }

  return 0;
}

mm_result_t mm_create_page_table(uintptr_t* user_root_table) {
  void* kernel_root_table_phy = mm_get_kernel_root_table();
  void* user_root_table_phy = pmm_alloc(&mm_state.pmm_state, PML4_SIZE);

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
  void* addr = vmalloc(stack_size, flags);

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
  void* phys_page = pmm_alloc(&mm_state.pmm_state, page_size);

  if (phys_page == NULL) {
    return MM_ERR_OUT_OF_MEMORY;
  }

  // Map user stack
  mm_result_t result;
  mm_flags_t flags = MM_FLAG_WRITABLE | MM_FLAG_USER;

  result = map_page(root_table, (void*)stack_virt_addr, phys_page, flags);

  if (result != MM_SUCCESS) {
    pmm_free(&mm_state.pmm_state, phys_page);
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
      pmm_free(&mm_state.pmm_state, (void*)phys_addr);
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

mm_result_t mm_verify_process_addr(void* virt_addr) {
  if (virt_addr == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  uintptr_t addr = (uintptr_t)virt_addr;

  if (addr <= mm_state.user_virtual_base || addr >= mm_state.user_stack_base) {
    return MM_ERR_INVALID_ADDRESS;
  }

  return MM_SUCCESS;
}

mm_result_t mm_map_io_address(uintptr_t* virt_addr, void* phys_addr) {
  if (virt_addr == NULL || phys_addr == NULL) {
    return MM_ERR_INVALID_PARAMETER;
  }

  const size_t page_size = mm_state.page_size;
  void* root_table = phys_to_virt(mm_get_kernel_root_table());
  const uintptr_t io_virt_addr = (uintptr_t)phys_to_virt(phys_addr);

  uintptr_t paddr_aligned = page_align_down((uintptr_t)phys_addr, page_size);
  uintptr_t vaddr_aligned = page_align_down(io_virt_addr, page_size);

  const mm_flags_t flags = MM_FLAG_WRITABLE | MM_FLAG_EXE;

  mm_result_t result =
      map_page(root_table, (void*)vaddr_aligned, (void*)paddr_aligned, flags);

  if (result != MM_SUCCESS) {
    return result;
  }

  *virt_addr = (uintptr_t)io_virt_addr;
  return MM_SUCCESS;
}

mm_result_t mm_get_current_mapping(void* virt_addr, uintptr_t* phys_addr) {
  if (virt_addr == NULL || phys_addr == NULL) {
    return MM_ERR_INVALID_PARAMETER;
  }

  void* root_table = mm_get_root_table();
  mm_result_t result = get_mapping(root_table, virt_addr, phys_addr);

  if (result != MM_SUCCESS) {
    return result;
  }

  return MM_SUCCESS;
}
