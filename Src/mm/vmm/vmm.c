#include <arch/x86_64/mmu.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <process/locks.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

// State variables
static vmalloc_region_t* vmalloc_list_head;
static spinlock_t vmalloc_lock;
static struct mm_state_s* mm_state;

int vmm_init(struct mm_state_s* state) {
  vmalloc_list_head = NULL;
  mm_state = NULL;
  mm_state = state;

  spinlock_init(&vmalloc_lock);

  if (mm_state == NULL) {
    return -1;
  }

  return 0;
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
    uint64_t* new_table = pmm_alloc(&mm_state->pmm_state, PDPT_SIZE);
    if (new_table == NULL) return MM_ERR_OUT_OF_MEMORY;

#ifdef DEBUG
    if (!is_page_aligned((uintptr_t)new_table, PDPT_ALIGNMENT)) {
      pmm_free(&mm_state->pmm_state, new_table);
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
    uint64_t* new_table = pmm_alloc(&mm_state->pmm_state, PD_SIZE);
    if (new_table == NULL) return MM_ERR_OUT_OF_MEMORY;

#ifdef DEBUG
    if (!is_page_aligned((uintptr_t)new_table, PD_ALIGNMENT)) {
      pmm_free(&mm_state->pmm_state, new_table);
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
    uint64_t* new_table = pmm_alloc(&mm_state->pmm_state, PT_SIZE);
    if (new_table == NULL) return MM_ERR_OUT_OF_MEMORY;

#ifdef DEBUG
    if (!is_page_aligned((uintptr_t)new_table, PT_ALIGNMENT)) {
      pmm_free(&mm_state->pmm_state, new_table);
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
      pmm_free(&mm_state->pmm_state, virt_to_phys(pdpt));
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
      pmm_free(&mm_state->pmm_state, virt_to_phys(pd));
    }

    // check if the pdpt is empty, if it is, free it and remove the entry from
    // pml4
    if (is_page_table_empty(pdpt, PDPT_NUM_ENTRIES)) {
      pml4[pml4_idx] = 0;
      pmm_free(&mm_state->pmm_state, virt_to_phys(pdpt));
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
    pmm_free(&mm_state->pmm_state, virt_to_phys(pt));
  }

  // check if the pd is empty, if it is, free it and remove the entry from pdpt
  if (is_page_table_empty(pd, PD_NUM_ENTRIES)) {
    pdpt[pdpt_idx] = 0;
    pmm_free(&mm_state->pmm_state, virt_to_phys(pd));
  }

  // check if the pdpt is empty, if it is, free it and remove the entry from
  // pml4

  if (is_page_table_empty(pdpt, PDPT_NUM_ENTRIES)) {
    pml4[pml4_idx] = 0;
    pmm_free(&mm_state->pmm_state, virt_to_phys(pdpt));
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

static void* vmalloc_allocate_space(size_t requested_size) {
  if (requested_size == 0) {
    return NULL;
  }

  const size_t vmalloc_start = mm_state->vmalloc_base;
  const size_t vmalloc_end = mm_state->vmalloc_base + mm_state->vmalloc_size;

  const size_t page_size = mm_state->page_size;
  size_t aligned_size = page_align_up(requested_size, page_size);

  // unmapped guard page at the end
  size_t required_gap = aligned_size + page_size;

  unsigned long flags;
  spinlock_acquire(&vmalloc_lock, &flags);

  uintptr_t candidate_base = vmalloc_start;
  vmalloc_region_t* current = vmalloc_list_head;
  vmalloc_region_t* insert_after = NULL;

  // If the list is completely empty
  if (!current) {
    if (vmalloc_end - vmalloc_start < required_gap) {
      goto fail;
    }

    candidate_base = vmalloc_start;
  } else {
    // Check the gap BEFORE the very first allocation
    if (current->virtual_base - vmalloc_start >= required_gap) {
      candidate_base = vmalloc_start;
      insert_after = NULL;
    } else {
      // Walk the list to find a gap between allocations
      bool found = false;
      while (current != NULL) {
        uintptr_t gap_start = current->virtual_base + current->size;

        // The gap ends at the next allocation, or at VMALLOC_END
        uintptr_t gap_end =
            current->next ? current->next->virtual_base : vmalloc_end;

        // Did we find a hole large enough?
        if (gap_end - gap_start >= required_gap) {
          // Start our allocation exactly after the previous one's guard page
          candidate_base = gap_start + page_size;
          insert_after = current;
          found = true;
          break;
        }
        current = current->next;
      }

      if (!found) goto fail;
    }
  }

  // We found a hole! Create the tracking node using your SLUB allocator.
  vmalloc_region_t* new_node =
      (vmalloc_region_t*)kmalloc(sizeof(vmalloc_region_t));
  if (!new_node) goto fail;

  new_node->virtual_base = candidate_base;
  new_node->size = aligned_size;

  // Insert into the sorted doubly-linked list
  if (insert_after == NULL) {
    // Insert at the head
    new_node->prev = NULL;
    new_node->next = vmalloc_list_head;
    if (vmalloc_list_head) vmalloc_list_head->prev = new_node;
    vmalloc_list_head = new_node;
  } else {
    // Insert in the middle or end
    new_node->prev = insert_after;
    new_node->next = insert_after->next;
    if (insert_after->next) insert_after->next->prev = new_node;
    insert_after->next = new_node;
  }

  spinlock_release(&vmalloc_lock, flags);
  return (void*)candidate_base;

fail:
  spinlock_release(&vmalloc_lock, flags);
  return NULL;
}

static void vmalloc_free_space(void* virt_ptr) {
  if (!virt_ptr) {
    return;
  }

  uintptr_t target_base = (uintptr_t)virt_ptr;

  unsigned long flags;
  spinlock_acquire(&vmalloc_lock, &flags);

  vmalloc_region_t* current = vmalloc_list_head;

  while (current != NULL) {
    if (current->virtual_base == target_base) {
      // Unlink from the doubly-linked list
      if (current->prev) {
        current->prev->next = current->next;
      } else {
        vmalloc_list_head = current->next;
      }

      if (current->next) {
        current->next->prev = current->prev;
      }

      spinlock_release(&vmalloc_lock, flags);
      kfree(current);
      return;
    }
    current = current->next;
  }

  spinlock_release(&vmalloc_lock, flags);
}

void* vmalloc(size_t size, mm_flags_t mm_flags) {
  if (size == 0) {
    return NULL;
  }

  void* virt_base = vmalloc_allocate_space(size);

  if (!virt_base) {
    return NULL;
  }

  const size_t page_size = mm_state->page_size;
  const size_t pages_needed = page_align_up(size, page_size) / page_size;

  if (!pmm_is_pages_avaliable(&mm_state->pmm_state, pages_needed)) {
    vmalloc_free_space(virt_base);
    return NULL;
  }

  void* root_table = mm_get_root_table();
  uintptr_t current_virt = (uintptr_t)virt_base;

  for (size_t i = 0; i < pages_needed; ++i) {
    void* phys_frame = pmm_alloc(&mm_state->pmm_state, page_size);

    if (phys_frame) {
      mm_result_t result =
          map_page(root_table, (void*)current_virt, phys_frame, mm_flags);

      if (result == MM_SUCCESS) {
        current_virt += page_size;
        continue;
      }

      pmm_free(&mm_state->pmm_state, phys_frame);
    }

    // rollback if mapping fails or allocation fails
    for (size_t j = 0; j < i; j++) {
      uint8_t* virt_addr = (uint8_t*)virt_base + page_size * j;

      uintptr_t phys_addr;
      mm_result_t result = unmap_page(root_table, virt_addr, &phys_addr);

      if (result == MM_SUCCESS) {
        pmm_free(&mm_state->pmm_state, (void*)phys_addr);
      }
    }

    vmalloc_free_space(virt_base);
    return NULL;
  }

  return virt_base;
}

static vmalloc_region_t* vmalloc_find_region(void* virt_ptr) {
  if (!virt_ptr) {
    return NULL;
  }

  uintptr_t target_base = (uintptr_t)virt_ptr;

  unsigned long flags;
  spinlock_acquire(&vmalloc_lock, &flags);

  vmalloc_region_t* current = vmalloc_list_head;

  while (current != NULL) {
    if (current->virtual_base == target_base) {
      spinlock_release(&vmalloc_lock, flags);
      return current;
    }
    current = current->next;
  }

  spinlock_release(&vmalloc_lock, flags);
  return NULL;
}

static void vmalloc_direct_free(vmalloc_region_t* region) {
  if (!region) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&vmalloc_lock, &flags);

  vmalloc_region_t* current = region;

  // Unlink from the doubly-linked list
  if (current->prev) {
    current->prev->next = current->next;
  } else {
    vmalloc_list_head = current->next;
  }

  if (current->next) {
    current->next->prev = current->prev;
  }

  spinlock_release(&vmalloc_lock, flags);
  kfree(current);
}

void vfree(void* addr) {
  if (addr == NULL) {
    return;
  }

  vmalloc_region_t* region = vmalloc_find_region(addr);

  if (!region) {
    return;
  }

  void* root_table = mm_get_root_table();

  const size_t page_size = mm_state->page_size;
  const size_t num_pages = region->size / page_size;

  for (size_t i = 0; i < num_pages; i++) {
    uint8_t* virt_addr = (uint8_t*)addr + page_size * i;

    uintptr_t phys_addr;
    mm_result_t result = unmap_page(root_table, virt_addr, &phys_addr);

    if (result == MM_SUCCESS && phys_addr != 0) {
      pmm_free(&mm_state->pmm_state, (void*)phys_addr);
    }
  }

  vmalloc_direct_free(region);
}
