#pragma once

#include <mm/mm.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vmalloc_region {
  uintptr_t virtual_base;
  size_t size;  // Size in bytes
  struct vmalloc_region* next;
  struct vmalloc_region* prev;
} vmalloc_region_t;

int vmm_init(struct mm_state_s* mm_state);

mm_result_t map_page(void* root_table, void* virt_addr, void* phys_addr,
                     mm_flags_t mm_flags);

mm_result_t unmap_page(void* root_table, void* virt_addr, uintptr_t* phys_addr);

mm_result_t get_mapping(void* root_table, void* virt_addr,
                        uintptr_t* phys_addr);

mm_result_t change_page_flags(void* root_table, void* virt_addr,
                              mm_flags_t new_flags);

mm_result_t remap_page(void* root_table, void* virt_addr, void* new_phys_addr,
                       uintptr_t* old_phys_addr, mm_flags_t mm_flags);

void* vmalloc(size_t size, mm_flags_t mm_flags);

void vfree(void* addr);
