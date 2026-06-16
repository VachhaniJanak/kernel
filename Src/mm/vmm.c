#include "vmm.h"
#include "mmutils.h"
#include "pmm.h"
#include <arch/x86_64/mmu.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/utils.h>

void map_page(void *virt_addr, void *phys_addr, uint64_t flags,
              void *(*phys_to_virt)(void *)) {

  uint64_t *current_table = (uint64_t *)get_page_table_addr();
  current_table = phys_to_virt(current_table);

  size_t pml4_idx = PML4_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);

  if ((current_table[pml4_idx] == 0) ||
      !(current_table[pml4_idx] & MMU_PRESENT)) {

    uint64_t *addr = pmm_alloc(PDPT_SIZE);

    if (addr == NULL)
      return;

    kmemset(phys_to_virt(addr), 0, PDPT_SIZE);
    addr = (uint64_t *)((uint64_t)addr & PAGE_MASK);
    current_table[pml4_idx] = (uint64_t)addr | MMU_PRESENT | MMU_WRITABLE;
  }

  size_t pdpt_idx = PDPT_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t *)current_table[pml4_idx];
  current_table = (uint64_t *)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pdpt_idx] == 0) ||
      !(current_table[pdpt_idx] & MMU_PRESENT)) {

    uint64_t *addr = pmm_alloc(PAGE_DIRECTORY_SIZE);

    if (addr == NULL)
      return;

    kmemset(phys_to_virt(addr), 0, PAGE_DIRECTORY_SIZE);
    addr = (uint64_t *)((uint64_t)addr & PAGE_MASK);
    current_table[pdpt_idx] = (uint64_t)addr | MMU_PRESENT | MMU_WRITABLE;
  }

  size_t pd_idx = PAGE_DIRECTORY_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t *)current_table[pdpt_idx];
  current_table = (uint64_t *)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pd_idx] == 0) || !(current_table[pd_idx] & MMU_PRESENT)) {
    uint64_t *addr = pmm_alloc(PAGE_TABLE_SIZE);

    if (addr == NULL)
      return;

    kmemset(phys_to_virt(addr), 0, PAGE_TABLE_SIZE);
    addr = (uint64_t *)((uint64_t)addr & PAGE_MASK);
    current_table[pd_idx] = (uint64_t)addr | MMU_PRESENT | MMU_WRITABLE;
  }

  size_t pt_idx = PAGE_TABLE_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t *)current_table[pd_idx];
  current_table = (uint64_t *)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);
  phys_addr = (uint64_t *)((uint64_t)phys_addr & PAGE_MASK);
  current_table[pt_idx] = ((uint64_t)phys_addr) | flags;
}

static inline bool all_page_entry_free(uint64_t *addr, size_t entries) {
  for (size_t i = 0; i < entries; i++)
    if ((addr[i] != 0) && (addr[i] & MMU_PRESENT))
      return false;
  return true;
}

void *unmap_page(void *virt_addr, void *(*phys_to_virt)(void *),
                 void *(*virt_to_phys)(void *)) {

  uint64_t *current_table = (uint64_t *)get_page_table_addr();
  current_table = phys_to_virt(current_table);
  size_t pml4_idx = PML4_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);

  uint64_t *page_stack[4];
  size_t idx = 0;

  tlb_invalided(virt_addr);

  if ((current_table[pml4_idx] == 0) ||
      !(current_table[pml4_idx] & MMU_PRESENT)) {
    return NULL;
  }

  page_stack[idx++] = current_table;

  size_t pdpt_idx = PDPT_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t *)current_table[pml4_idx];
  current_table = (uint64_t *)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pdpt_idx] == 0) ||
      !(current_table[pdpt_idx] & MMU_PRESENT)) {
    return NULL;
  }

  page_stack[idx++] = current_table;

  size_t pd_idx = PAGE_DIRECTORY_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t *)current_table[pdpt_idx];
  current_table = (uint64_t *)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pd_idx] == 0) || !(current_table[pd_idx] & MMU_PRESENT))
    return NULL;

  page_stack[idx++] = current_table;

  size_t pt_idx = PAGE_TABLE_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t *)current_table[pd_idx];
  current_table = (uint64_t *)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  page_stack[idx++] = current_table;

  void *phys_addr = (void *)current_table[pt_idx];
  phys_addr = (void *)((uint64_t)phys_addr & PAGE_MASK);
  current_table[pt_idx] = 0;

  // check if all entries in table is free then remove
  if (!all_page_entry_free(page_stack[--idx], 512))
    return phys_addr;

  page_stack[idx - 1][pd_idx] = 0;
  pmm_free(virt_to_phys(page_stack[idx]));

  if (!all_page_entry_free(page_stack[--idx], 512))
    return phys_addr;

  page_stack[idx - 1][pdpt_idx] = 0;
  pmm_free(virt_to_phys(page_stack[idx]));

  if (!all_page_entry_free(page_stack[--idx], 512))
    return phys_addr;

  page_stack[idx - 1][pml4_idx] = 0;
  pmm_free(virt_to_phys(page_stack[idx]));

  return phys_addr;
}
