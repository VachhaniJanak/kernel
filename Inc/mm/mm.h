#pragma once

#include <stdint.h>
#include <stddef.h>

#define MM_DEFAULT_PAGE_SIZE 4096

struct vm_state_s {
  void *cursor;
  size_t total_size;
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
  struct vm_state_s heap_state;

  uintptr_t kernel_vmalloc_base;
  uintptr_t kernel_vmalloc_size;
  struct vm_state_s vmalloc_state;

  uintptr_t kernel_stack_base;
  uintptr_t kernel_stack_size;
};

int mm_init(void);
