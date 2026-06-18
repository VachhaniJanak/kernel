#pragma once

#include <stddef.h>
#include <stdint.h>

#define MM_DEFAULT_PAGE_SIZE 4096

extern uintptr_t hhdm_offset;

struct vm_area_s {
  void *cursor;
  void *fragment_list;
};

struct mm_state_s {
  uintptr_t physical_memory_base;
  size_t physical_memory_size;

  uintptr_t hhdm_offset;

  size_t page_size;

  uintptr_t kernel_phys_base;
  uintptr_t kernel_virt_base;
  uintptr_t kernel_size;

  uintptr_t kernel_heap_base;
  uintptr_t kernel_heap_size;
  struct vm_area_s heap_state;

  uintptr_t kernel_vmalloc_base;
  uintptr_t kernel_vmalloc_size;
  struct vm_area_s vmalloc_state;

  uintptr_t kernel_stack_base;
  uintptr_t kernel_stack_size;
  struct vm_area_s stack_state;
};

int mm_init(void);

static inline void *virt_to_phys(void *ptr) {
  return (void *)((uintptr_t)ptr - hhdm_offset);
}

static inline void *phys_to_virt(void *ptr) {
  return (void *)((uintptr_t)ptr + hhdm_offset);
}
