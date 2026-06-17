#pragma once

#include <mm/mm.h>
#include <stdbool.h>
#include <stdint.h>

struct sgl_node_s {
  void *addr;
  size_t size;
  struct sgl_node_s *next;
};

bool init_vmm(struct mm_state_s *mm_state);

bool map_page(void *virt_addr, void *phys_addr, uint64_t flags,
              void *(*phys_to_virt)(void *));

void *unmap_page(void *virt_addr, void *(*phys_to_virt)(void *),
                 void *(*virt_to_phys)(void *));

void *pre_obj_alloc(size_t size);

void pre_obj_free(void *ptr);

void *valloc_page(void);

void vfree_page(void *addr);

void *vmalloc(size_t size, uint64_t flags, bool phys_continuous);

void vfree(void *addr);

void debug_print_vmm_tree(void);

void debug_print_size_classes(void);
