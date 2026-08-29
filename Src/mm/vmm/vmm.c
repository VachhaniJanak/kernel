#include <arch/x86_64/mmu.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/slub/slub.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

#include "../debug.h"
#include "rbtree.h"

void* valloc_page(void);
void vfree_page(void* addr);

static RBTree vmm_tree = {0};
static struct mm_state_s* mm_state = NULL;
static struct sslub_state_s pre_alloc = {0};
static struct sgl_node_s* size_classes[50] = {NULL};

bool init_vmm(struct mm_state_s* state) {
  if (state == NULL) return false;

  mm_state = state;
  rb_create(&vmm_tree);

  mm_state->vmalloc_state.cursor = (void*)mm_state->kernel_vmalloc_base;
  mm_state->vmalloc_state.fragment_list = size_classes;

  // initialize small caches for slab allocator
  // for node allocation e.g: red-black nodes

  pre_alloc.page_size = mm_state->page_size;
  pre_alloc.get_page = &valloc_page;
  pre_alloc.free_page = &vfree_page;

  init_slub_scaches(&pre_alloc);

  return true;
}

static inline bool is_page_table_empty(uint64_t* table, size_t num_entries) {
  for (size_t i = 0; i < num_entries; i++) {
    if (table[i] & MMU_PRESENT) {
      return false;
    }
  }
  return true;
}

mm_result_t map_page(void* root_table, void* virt_addr, void* phys_addr,
                     mm_flags_t mm_flags) {
  if (root_table == NULL || virt_addr == NULL || phys_addr == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  // check if the flags are valid
  if ((mm_flags & MM_FLAG_1GB) && (mm_flags & MM_FLAG_2MB)) {
    return MM_ERR_INVALID_FLAGS;
  }

  uint64_t virt = (uint64_t)virt_addr;
  uint64_t phys = (uint64_t)phys_addr;
  uint64_t mmu_flags = mm_get_mmu_flags(mm_flags);

  // Validate alignment for huge pages
  if (mm_flags & MM_FLAG_1GB) {
    if (!is_page_aligned(virt, MM_SIZE_1GB)) return MM_ERR_INVALID_ALIGNMENT;
    if (!is_page_aligned(phys, MM_SIZE_1GB)) return MM_ERR_INVALID_ALIGNMENT;
  }

  if (mm_flags & MM_FLAG_2MB) {
    if (!is_page_aligned(virt, MM_SIZE_XMB(2))) return MM_ERR_INVALID_ALIGNMENT;
    if (!is_page_aligned(phys, MM_SIZE_XMB(2))) return MM_ERR_INVALID_ALIGNMENT;
  }

  // Level 4 (PML4)
  uint64_t* pml4 = (uint64_t*)root_table;
  uint16_t pml4_idx = PML4_INDEX(virt);

  // check if the entry is present
  if (!(pml4[pml4_idx] & MMU_PRESENT)) {
    uint64_t* new_table = pmm_alloc(PDPT_SIZE);
    if (new_table == NULL) return MM_ERR_OUT_OF_MEMORY;

#ifdef DEBUG
    if (!is_page_aligned((uintptr_t)new_table, PDPT_ALIGNMENT)) {
      pmm_free(new_table);
      return MM_ERR_INVALID_PM_ALIGNMENT;
    }
#endif

    kmemset(phys_to_virt(new_table), 0, PDPT_SIZE);
    uint64_t t_flags = MMU_PRESENT | MMU_WRITABLE;
    t_flags |= (mmu_flags & MMU_USER_MEMORY);
    pml4[pml4_idx] = ((uint64_t)new_table) | t_flags;
  }

  // Level 3 (PDPT)
  uint64_t* pdpt = phys_to_virt((void*)(pml4[pml4_idx] & PHYS_MASK));
  uint16_t pdpt_idx = PDPT_INDEX(virt);

  // check if huge page is requested
  if (mm_flags & MM_FLAG_1GB) {
    // if the entry is already present, return the physical address
    if (pdpt[pdpt_idx] & MMU_PRESENT) {
      if (pdpt[pdpt_idx] & MMU_HUGE_PAGE) {
        return MM_ERR_ALREADY_MAPPED;
      }

      return MM_ERR_HUGE_PAGE_CONFLICT;
    }

    uint64_t t_flags = MMU_PRESENT | MMU_HUGE_PAGE | mmu_flags;
    pdpt[pdpt_idx] = (phys & PHYS_MASK) | t_flags;
    return MM_SUCCESS;
  }

  // check if the entry is present
  if (!(pdpt[pdpt_idx] & MMU_PRESENT)) {
    uint64_t* new_table = pmm_alloc(PD_SIZE);
    if (new_table == NULL) return MM_ERR_OUT_OF_MEMORY;

#ifdef DEBUG
    if (!is_page_aligned((uintptr_t)new_table, PD_ALIGNMENT)) {
      pmm_free(new_table);
      return MM_ERR_INVALID_PM_ALIGNMENT;
    }
#endif

    kmemset(phys_to_virt(new_table), 0, PD_SIZE);
    uint64_t t_flags = MMU_PRESENT | MMU_WRITABLE;
    t_flags |= (mmu_flags & MMU_USER_MEMORY);
    pdpt[pdpt_idx] = ((uint64_t)new_table) | t_flags;
  } else {
    // check if the entry is a huge page, if it is, return NULL
    if (pdpt[pdpt_idx] & MMU_HUGE_PAGE) {
      return MM_ERR_HUGE_PAGE_CONFLICT;
    }
  }

  // Level 2 (PD)
  uint64_t* pd = phys_to_virt((void*)(pdpt[pdpt_idx] & PHYS_MASK));
  uint16_t pd_idx = PD_INDEX(virt);

  if (mm_flags & MM_FLAG_2MB) {
    // if the entry is already present, return the physical address
    if (pd[pd_idx] & MMU_PRESENT) {
      if (pd[pd_idx] & MMU_HUGE_PAGE) {
        return MM_ERR_ALREADY_MAPPED;
      }

      return MM_ERR_HUGE_PAGE_CONFLICT;
    }

    uint64_t t_flags = MMU_PRESENT | MMU_HUGE_PAGE | mmu_flags;
    pd[pd_idx] = (phys & PHYS_MASK) | t_flags;
    return MM_SUCCESS;
  }

  // check if the entry is present
  if (!(pd[pd_idx] & MMU_PRESENT)) {
    uint64_t* new_table = pmm_alloc(PT_SIZE);
    if (new_table == NULL) return MM_ERR_OUT_OF_MEMORY;

#ifdef DEBUG
    if (!is_page_aligned((uintptr_t)new_table, PT_ALIGNMENT)) {
      pmm_free(new_table);
      return MM_ERR_INVALID_PM_ALIGNMENT;
    }
#endif

    kmemset(phys_to_virt(new_table), 0, PT_SIZE);
    uint64_t t_flags = MMU_PRESENT | MMU_WRITABLE;
    t_flags |= (mmu_flags & MMU_USER_MEMORY);
    pd[pd_idx] = ((uint64_t)new_table) | t_flags;
  } else {
    if (pd[pd_idx] & MMU_HUGE_PAGE) {
      return MM_ERR_HUGE_PAGE_CONFLICT;
    }
  }

  // Level 1 (PT)
  uint64_t* pt = phys_to_virt((void*)(pd[pd_idx] & PHYS_MASK));
  uint16_t pt_idx = PT_INDEX(virt);

  // if the entry is already present, return the physical address
  if (pt[pt_idx] & MMU_PRESENT) {
    return MM_ERR_ALREADY_MAPPED;
  }

  uint64_t t_flags = MMU_PRESENT | mmu_flags;
  pt[pt_idx] = (phys & PHYS_MASK) | t_flags;

  return MM_SUCCESS;
}

mm_result_t unmap_page(void* root_table, void* virt_addr,
                       uintptr_t* phys_addr) {
  if (root_table == NULL || virt_addr == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  uint64_t virt = (uint64_t)virt_addr;

  // Level 4 (PML4)
  uint64_t* pml4 = (uint64_t*)root_table;
  uint16_t pml4_idx = PML4_INDEX(virt);

  // check if the entry is present, if not return NULL
  if (!(pml4[pml4_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // Level 3 (PDPT)
  uint64_t* pdpt = phys_to_virt((void*)(pml4[pml4_idx] & PHYS_MASK));
  uint16_t pdpt_idx = PDPT_INDEX(virt);

  // check if the entry is present, if not return NULL
  if (!(pdpt[pdpt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page
  if (pdpt[pdpt_idx] & MMU_HUGE_PAGE) {
    *phys_addr = pdpt[pdpt_idx] & PHYS_MASK;
    pdpt[pdpt_idx] = 0;
    tlb_invalidate(virt_addr);

    // check if the pdpt is empty, if it is, free it and remove the entry from
    // pml4
    if (is_page_table_empty(pdpt, PDPT_NUM_ENTRIES)) {
      pml4[pml4_idx] = 0;
      pmm_free(virt_to_phys(pdpt));
    }

    return MM_SUCCESS;
  }

  // Level 2 (PD)
  uint64_t* pd = phys_to_virt((void*)(pdpt[pdpt_idx] & PHYS_MASK));
  uint16_t pd_idx = PD_INDEX(virt);

  // check if the entry is present, if not return NULL
  if (!(pd[pd_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page if it is, unmap it and return the
  // physical address
  if (pd[pd_idx] & MMU_HUGE_PAGE) {
    *phys_addr = pd[pd_idx] & PHYS_MASK;
    pd[pd_idx] = 0;
    tlb_invalidate(virt_addr);

    // check if the pd is empty, if it is, free it and remove the entry from
    // pdpt
    if (is_page_table_empty(pd, PD_NUM_ENTRIES)) {
      pdpt[pdpt_idx] = 0;
      pmm_free(virt_to_phys(pd));
    }

    // check if the pdpt is empty, if it is, free it and remove the entry from
    // pml4
    if (is_page_table_empty(pdpt, PDPT_NUM_ENTRIES)) {
      pml4[pml4_idx] = 0;
      pmm_free(virt_to_phys(pdpt));
    }

    return MM_SUCCESS;
  }

  // Level 1 (PT)
  uint64_t* pt = phys_to_virt((void*)(pd[pd_idx] & PHYS_MASK));
  uint16_t pt_idx = PT_INDEX(virt);

  // check if the entry is present, if not return NULL
  if (!(pt[pt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  *phys_addr = pt[pt_idx] & PHYS_MASK;
  pt[pt_idx] = 0;
  tlb_invalidate(virt_addr);

  // check if the pt is empty, if it is, free it and remove the entry from pd
  if (is_page_table_empty(pt, PT_NUM_ENTRIES)) {
    pd[pd_idx] = 0;
    pmm_free(virt_to_phys(pt));
  }

  // check if the pd is empty, if it is, free it and remove the entry from pdpt
  if (is_page_table_empty(pd, PD_NUM_ENTRIES)) {
    pdpt[pdpt_idx] = 0;
    pmm_free(virt_to_phys(pd));
  }

  // check if the pdpt is empty, if it is, free it and remove the entry from
  // pml4

  if (is_page_table_empty(pdpt, PDPT_NUM_ENTRIES)) {
    pml4[pml4_idx] = 0;
    pmm_free(virt_to_phys(pdpt));
  }

  return MM_SUCCESS;
}

mm_result_t get_mapping(void* root_table, void* virt_addr,
                        uintptr_t* phys_addr) {
  if (root_table == NULL || virt_addr == NULL || phys_addr == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  uint64_t* pml4 = (uint64_t*)root_table;
  uint16_t pml4_idx = PML4_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pml4[pml4_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // Level 3 (PDPT)
  uint64_t* pdpt = phys_to_virt((void*)(pml4[pml4_idx] & PHYS_MASK));
  uint16_t pdpt_idx = PDPT_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pdpt[pdpt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page of 1GB
  // if it is, return the physical address
  if (pdpt[pdpt_idx] & MMU_HUGE_PAGE) {
    uint64_t page_offset = (uint64_t)virt_addr & (MM_SIZE_1GB - 1);
    uint64_t phys_base = pdpt[pdpt_idx] & PHYS_MASK;
    *phys_addr = (phys_base + page_offset);
    return MM_SUCCESS;
  }

  // Level 2 (PD)
  uint64_t* pd = phys_to_virt((void*)(pdpt[pdpt_idx] & PHYS_MASK));
  uint16_t pd_idx = PD_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pd[pd_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page of 2MB
  // if it is, return the physical address
  if (pd[pd_idx] & MMU_HUGE_PAGE) {
    uint64_t page_offset = (uint64_t)virt_addr & (MM_SIZE_XMB(2) - 1);
    uint64_t phys_base = pd[pd_idx] & PHYS_MASK;
    *phys_addr = (phys_base + page_offset);
    return MM_SUCCESS;
  }

  // Level 1 (PT)
  uint64_t* pt = phys_to_virt((void*)(pd[pd_idx] & PHYS_MASK));
  uint16_t pt_idx = PT_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pt[pt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  uint64_t page_offset = (uint64_t)virt_addr & (MM_SIZE_XKB(4) - 1);
  uint64_t phys_base = pt[pt_idx] & PHYS_MASK;
  *phys_addr = (phys_base + page_offset);
  return MM_SUCCESS;
}

mm_result_t change_page_flags(void* root_table, void* virt_addr,
                              mm_flags_t new_flags) {
  if (root_table == NULL || virt_addr == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  uint64_t mmu_flags = mm_get_mmu_flags(new_flags);

  // check if the new flags are valid
  if ((new_flags & MM_FLAG_1GB) && (new_flags & MM_FLAG_2MB)) {
    return MM_ERR_INVALID_FLAGS;
  }

  // Level 4 (PML4)
  uint64_t* pml4 = (uint64_t*)root_table;
  uint16_t pml4_idx = PML4_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pml4[pml4_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // Level 3 (PDPT)
  uint64_t* pdpt = phys_to_virt((void*)(pml4[pml4_idx] & PHYS_MASK));
  uint16_t pdpt_idx = PDPT_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pdpt[pdpt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page of 1GB
  if (pdpt[pdpt_idx] & MMU_HUGE_PAGE) {
    // if the new flags do not include MM_FLAG_1GB, return an error
    if (!(new_flags & MM_FLAG_1GB)) {
      return MM_ERR_INVALID_FLAGS;
    }

    pdpt[pdpt_idx] = (pdpt[pdpt_idx] & PHYS_MASK);
    pdpt[pdpt_idx] |= (MMU_PRESENT | MMU_HUGE_PAGE | mmu_flags);
    tlb_invalidate(virt_addr);

    // propagate the user flags to the parent tables
    pml4[pml4_idx] |= (MMU_PRESENT | MMU_WRITABLE);
    pml4[pml4_idx] |= (mmu_flags & MMU_USER_MEMORY);

    return MM_SUCCESS;
  }

  // Level 2 (PD)
  uint64_t* pd = phys_to_virt((void*)(pdpt[pdpt_idx] & PHYS_MASK));
  uint16_t pd_idx = PD_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pd[pd_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page of 2MB
  if (pd[pd_idx] & MMU_HUGE_PAGE) {
    // if the new flags do not include MM_FLAG_2MB, return an error
    if (!(new_flags & MM_FLAG_2MB)) {
      return MM_ERR_INVALID_FLAGS;
    }

    pd[pd_idx] = (pd[pd_idx] & PHYS_MASK);
    pd[pd_idx] |= (MMU_PRESENT | MMU_HUGE_PAGE | mmu_flags);
    tlb_invalidate(virt_addr);

    // propagate the user flags to the parent tables
    pdpt[pdpt_idx] |= (MMU_PRESENT | MMU_WRITABLE);
    pdpt[pdpt_idx] |= (mmu_flags & MMU_USER_MEMORY);

    pml4[pml4_idx] |= (MMU_PRESENT | MMU_WRITABLE);
    pml4[pml4_idx] |= (mmu_flags & MMU_USER_MEMORY);

    return MM_SUCCESS;
  }

  // Level 1 (PT)
  uint64_t* pt = phys_to_virt((void*)(pd[pd_idx] & PHYS_MASK));
  uint16_t pt_idx = PT_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pt[pt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  pt[pt_idx] = (pt[pt_idx] & PHYS_MASK);
  pt[pt_idx] |= (mmu_flags | MMU_PRESENT);
  tlb_invalidate(virt_addr);

  // propagate the user flags to the parent tables
  pd[pd_idx] |= (MMU_PRESENT | MMU_WRITABLE);
  pd[pd_idx] |= (mmu_flags & MMU_USER_MEMORY);

  pdpt[pdpt_idx] |= (MMU_PRESENT | MMU_WRITABLE);
  pdpt[pdpt_idx] |= (mmu_flags & MMU_USER_MEMORY);

  pml4[pml4_idx] |= (MMU_PRESENT | MMU_WRITABLE);
  pml4[pml4_idx] |= (mmu_flags & MMU_USER_MEMORY);

  return MM_SUCCESS;
}

mm_result_t remap_page(void* root_table, void* virt_addr, void* new_phys_addr,
                       uintptr_t* old_phys_addr, mm_flags_t mm_flags) {
  if (root_table == NULL || virt_addr == NULL || new_phys_addr == NULL ||
      old_phys_addr == NULL) {
    return MM_ERR_INVALID_ADDRESS;
  }

  // check if the flags are valid
  if ((mm_flags & MM_FLAG_1GB) && (mm_flags & MM_FLAG_2MB)) {
    return MM_ERR_INVALID_FLAGS;
  }

  // Validate alignment for huge pages
  if (mm_flags & MM_FLAG_1GB &&
      !is_page_aligned((uintptr_t)new_phys_addr, MM_SIZE_1GB)) {
    return MM_ERR_INVALID_ALIGNMENT;
  }

  if (mm_flags & MM_FLAG_2MB &&
      !is_page_aligned((uintptr_t)new_phys_addr, MM_SIZE_XMB(2))) {
    return MM_ERR_INVALID_ALIGNMENT;
  }

  uint64_t mmu_flags = mm_get_mmu_flags(mm_flags);

  uint64_t* pml4 = (uint64_t*)root_table;
  uint16_t pml4_idx = PML4_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pml4[pml4_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // Level 3 (PDPT)
  uint64_t* pdpt = phys_to_virt((void*)(pml4[pml4_idx] & PHYS_MASK));
  uint16_t pdpt_idx = PDPT_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pdpt[pdpt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page of 1GB
  if (pdpt[pdpt_idx] & MMU_HUGE_PAGE) {
    // check if the new flags include MM_FLAG_1GB
    if (!(mm_flags & MM_FLAG_1GB)) {
      return MM_ERR_INVALID_FLAGS;
    }

    *old_phys_addr = pdpt[pdpt_idx] & PHYS_MASK;
    pdpt[pdpt_idx] = ((uint64_t)new_phys_addr & PHYS_MASK);
    pdpt[pdpt_idx] |= (MMU_PRESENT | MMU_HUGE_PAGE | mmu_flags);

    // propagate the user flags to the parent tables
    pml4[pml4_idx] |= (MMU_PRESENT | MMU_WRITABLE);
    pml4[pml4_idx] |= (mmu_flags & MMU_USER_MEMORY);

    tlb_invalidate(virt_addr);
    return MM_SUCCESS;
  }

  // Level 2 (PD)
  uint64_t* pd = phys_to_virt((void*)(pdpt[pdpt_idx] & PHYS_MASK));
  uint16_t pd_idx = PD_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pd[pd_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  // check if the entry is a huge page of 2MB
  if (pd[pd_idx] & MMU_HUGE_PAGE) {
    // check if the new flags include MM_FLAG_2MB
    if (!(mm_flags & MM_FLAG_2MB)) {
      return MM_ERR_INVALID_FLAGS;
    }

    *old_phys_addr = pd[pd_idx] & PHYS_MASK;
    pd[pd_idx] = ((uint64_t)new_phys_addr & PHYS_MASK);
    pd[pd_idx] |= (MMU_PRESENT | MMU_HUGE_PAGE | mmu_flags);

    // propagate the user flags to the parent tables
    pdpt[pdpt_idx] |= (MMU_PRESENT | MMU_WRITABLE);
    pdpt[pdpt_idx] |= (mmu_flags & MMU_USER_MEMORY);

    pml4[pml4_idx] |= (MMU_PRESENT | MMU_WRITABLE);
    pml4[pml4_idx] |= (mmu_flags & MMU_USER_MEMORY);

    tlb_invalidate(virt_addr);
    return MM_SUCCESS;
  }

  // Level 1 (PT)
  uint64_t* pt = phys_to_virt((void*)(pd[pd_idx] & PHYS_MASK));
  uint16_t pt_idx = PT_INDEX((uint64_t)virt_addr);

  // check if the entry is present
  if (!(pt[pt_idx] & MMU_PRESENT)) {
    return MM_ERR_NOT_MAPPED;
  }

  *old_phys_addr = pt[pt_idx] & PHYS_MASK;
  pt[pt_idx] = ((uint64_t)new_phys_addr & PHYS_MASK);
  pt[pt_idx] |= (MMU_PRESENT | mmu_flags);

  // propagate the user flags to the parent tables
  pd[pd_idx] |= (MMU_PRESENT | MMU_WRITABLE);
  pd[pd_idx] |= (mmu_flags & MMU_USER_MEMORY);

  pdpt[pdpt_idx] |= (MMU_PRESENT | MMU_WRITABLE);
  pdpt[pdpt_idx] |= (mmu_flags & MMU_USER_MEMORY);

  pml4[pml4_idx] |= (MMU_PRESENT | MMU_WRITABLE);
  pml4[pml4_idx] |= (mmu_flags & MMU_USER_MEMORY);

  tlb_invalidate(virt_addr);
  return MM_SUCCESS;
}

void* pre_obj_alloc(size_t size) { return sslub_alloc(&pre_alloc, size); }

void pre_obj_free(void* ptr) { sslub_free(&pre_alloc, ptr); }

static inline size_t size_to_class_index(size_t size, size_t page_size) {
  if (size == 0) return 0;

  return log2(size) - log2(page_size);
}

static inline void push(void* addr, size_t size) {
  struct sgl_node_s* n = pre_obj_alloc(sizeof(struct sgl_node_s));

  if (n == NULL) return;

  n->addr = addr;
  n->size = size;

  size_t idx = size_to_class_index(size, mm_state->page_size);
  struct sgl_node_s** list = mm_state->vmalloc_state.fragment_list;
  n->next = list[idx];
  list[idx] = n;
}

static inline void* pop(size_t size) {
  size_t idx = size_to_class_index(size, mm_state->page_size);
  struct sgl_node_s** list = mm_state->vmalloc_state.fragment_list;
  struct sgl_node_s* n = list[idx];

  if (n == NULL) return NULL;

  size_classes[idx] = n->next;
  void* addr = n->addr;

  pre_obj_free(n);

  return addr;
}

static inline uintptr_t get_end_addr(void* addr, size_t size) {
  return (uintptr_t)addr + size;
}

static inline void* vmalloc_vaddr(size_t size) {
  void* addr = pop(size);

  if (addr != NULL) return addr;

  size_t total_size = mm_state->kernel_vmalloc_size;
  uintptr_t base_addr = mm_state->kernel_vmalloc_base;
  total_size -= ((uintptr_t)mm_state->vmalloc_state.cursor - base_addr);

  if (size > total_size) return NULL;

  addr = mm_state->vmalloc_state.cursor;
  mm_state->vmalloc_state.cursor = (void*)get_end_addr(addr, size);

  return addr;
}

static inline void vfree_vaddr(void* addr, size_t size) {
  void* end_addr = (void*)get_end_addr(addr, size);

  if (end_addr == mm_state->vmalloc_state.cursor) {
    mm_state->vmalloc_state.cursor = addr;
    return;
  }

  push(addr, size);
}

void* valloc_page(void) {
  mm_flags_t flags = MMU_WRITABLE;
  const size_t page_size = mm_state->page_size;
  uint8_t* phy_addr = pmm_alloc(page_size);

  if (phy_addr == NULL) return NULL;

  uint8_t* vir_addr = vmalloc_vaddr(page_size);

  if (vir_addr == NULL) {
    pmm_free(phy_addr);
    return NULL;
  }

  void* root_table = mm_get_root_table();
  mm_result_t result = map_page(root_table, vir_addr, phy_addr, flags);

  if (result != MM_SUCCESS) {
    pmm_free(phy_addr);
    vfree_vaddr(vir_addr, page_size);
#ifdef DEBUG
    log_error("Failed to map page, error code: %d\n", result);
#endif
    return NULL;
  }

  return vir_addr;
}

void vfree_page(void* addr) {
  if (addr == NULL) {
    return;
  }

  void* root_table = mm_get_root_table();

  uintptr_t phys_addr;
  mm_result_t result = unmap_page(root_table, addr, &phys_addr);

  if (result == MM_SUCCESS) {
    pmm_free((void*)phys_addr);
  }

  vfree_vaddr(addr, mm_state->page_size);
}

void* vmalloc(size_t size, mm_flags_t flags, bool continuous) {
  if (size == 0) return NULL;

  const size_t page_size = mm_state->page_size;
  size = page_align_up(size, page_size);
  const size_t no_pages = size / page_size;

  if (!pmm_pages_avaliable(no_pages)) return NULL;

  uint8_t* phy_addr = pmm_alloc(size);
  uint8_t* vir_addr = vmalloc_vaddr(size);

  if (vir_addr == NULL) {
    pmm_free(phy_addr);
    return NULL;
  }

  void* root_table = mm_get_root_table();

  // check for continues pages
  if (phy_addr != NULL) {
    for (size_t i = 0; i < no_pages; i++) {
      uint8_t* p_addr = phy_addr + page_size * i;
      uint8_t* v_addr = vir_addr + page_size * i;

      map_page(root_table, v_addr, p_addr, flags);
    }

    rb_insert(&vmm_tree, vir_addr, size, true);
    return vir_addr;
  }

  if (continuous) return NULL;

  // if not, then allocate non continues physical pages
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
    vfree_vaddr(vir_addr, size);
  }

  rb_insert(&vmm_tree, vir_addr, size, false);
  return vir_addr;
}

void vfree(void* addr) {
  if (addr == NULL) return;

  size_t size;
  bool is_continuous;

  if (!rb_delete(&vmm_tree, addr, &size, &is_continuous)) return;

  vfree_vaddr(addr, size);

  void* root_table = mm_get_root_table();

  const size_t page_size = mm_state->page_size;
  const size_t no_pages = size / page_size;

  if (is_continuous) {
    uintptr_t phys_addr;
    mm_result_t result = unmap_page(root_table, addr, &phys_addr);

    for (size_t i = 1; i < no_pages; i++) {
      uint8_t* v_addr = (uint8_t*)addr + page_size * i;
      unmap_page(root_table, v_addr, &phys_addr);
    }

    if (result != MM_SUCCESS) pmm_free((void*)phys_addr);

    return;
  }

  for (size_t i = 0; i < no_pages; i++) {
    uint8_t* v_addr = (uint8_t*)addr + page_size * i;
    uintptr_t p_addr;
    mm_result_t result = unmap_page(root_table, v_addr, &p_addr);

    if (result == MM_SUCCESS) pmm_free((void*)p_addr);
  }
}

/////////////////////////// DEBUGGING FUNCTIONS ///////////////////////////

#ifdef DEBUG

/* Validation (debug)  */
/* Traversals  */

void inorder(RBTree* t, Node* x) {
  if (x == t->nil) return;

  inorder(t, x->left);
  LOG_PRINT("%p(%s)\n", x->addr, x->color == RED ? "R" : "B");
  inorder(t, x->right);
}

/* Returns black-height; -1 on violation */
static int validate_helper(RBTree* t, Node* x) {
  if (x == t->nil) return 1;

  if (x->color == RED) {
    if (x->left->color == RED) return -1;
    if (x->right->color == RED) return -1;
  }
  int lh = validate_helper(t, x->left);
  int rh = validate_helper(t, x->right);
  if (lh == -1 || rh == -1 || lh != rh) return -1;
  return lh + (x->color == BLACK ? 1 : 0);
}

int rb_validate(RBTree* t) {
  if (t->root->color != BLACK) return 0;
  return validate_helper(t, t->root) != -1;
}

void print_size_classes(void) {
  for (size_t i = 0; i < sizeof(size_classes) / sizeof(size_classes[0]); i++) {
    struct sgl_node_s* n = size_classes[i];

    if (n == NULL) continue;

    LOG_PRINT("Size class %2zu: ", i);
    while (n != NULL) {
      LOG_PRINT("[addr: %p, size: %zu] -> ", n->addr, n->size);
      n = n->next;
    }
    LOG_PRINT("NULL\n");
  }
}

void debug_print_vmm_tree(void) {
  LOG_NEWLINE();
  LOG_DEBUG("=== VMM Tree :\n");
  LOG_PRINT("=== In-order :\n");
  inorder(&vmm_tree, vmm_tree.root);
  LOG_PRINT("=== Valid RB tree? %s\n", rb_validate(&vmm_tree) ? "YES" : "NO");

  // for (size_t i = 0; i < NO_SMALL_PRE_CACHES; i++)
  // print_slub_kmem_cache(&pre_alloc.caches[i]);

  print_slub_kmem_cache(&pre_alloc.caches[5]);
}

void debug_print_size_classes(void) {
  LOG_NEWLINE();
  LOG_DEBUG("=== Size Classes :\n");
  print_size_classes();
}

#endif
