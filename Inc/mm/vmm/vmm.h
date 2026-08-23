#pragma once

#include <mm/mm.h>
#include <stdbool.h>
#include <stdint.h>

struct sgl_node_s {
  void* addr;
  size_t size;
  struct sgl_node_s* next;
};

bool init_vmm(struct mm_state_s* mm_state);

mm_result_t map_page(void* root_table, void* virt_addr, void* phys_addr,
                     mm_flags_t mm_flags);

mm_result_t unmap_page(void* root_table, void* virt_addr, uintptr_t* phys_addr);

mm_result_t get_mapping(void* root_table, void* virt_addr,
                        uintptr_t* phys_addr);

mm_result_t change_page_flags(void* root_table, void* virt_addr,
                              mm_flags_t new_flags);

mm_result_t remap_page(void* root_table, void* virt_addr, void* new_phys_addr,
                       uintptr_t* old_phys_addr, mm_flags_t mm_flags);

void* pre_obj_alloc(size_t size);

void pre_obj_free(void* ptr);

void* valloc_page(void);

void vfree_page(void* addr);

void* vmalloc(size_t size, mm_flags_t flags, bool phys_continuous);

void vfree(void* addr);

void debug_print_vmm_tree(void);

void debug_print_size_classes(void);
