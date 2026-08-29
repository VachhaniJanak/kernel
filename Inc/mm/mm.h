#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MM_SIZE_1KB (1 << 10)
#define MM_SIZE_1MB (1 << 20)
#define MM_SIZE_1GB (1 << 30)

#define MM_SIZE_XKB(s) ((s) * MM_SIZE_1KB)
#define MM_SIZE_XMB(s) ((s) * MM_SIZE_1MB)
#define MM_SIZE_XGB(s) ((s) * MM_SIZE_1GB)

#define MM_DEFAULT_PAGE_SIZE 4096

typedef enum {
  MM_FLAG_NONE = 0,
  MM_FLAG_READ = 1 << 0,
  MM_FLAG_WRITABLE = 1 << 1,
  MM_FLAG_EXE = 1 << 2,
  MM_FLAG_USER = 1 << 3,
  MM_FLAG_4KB = 1 << 4,
  MM_FLAG_2MB = 1 << 5,
  MM_FLAG_1GB = 1 << 6,
} mm_flags_t;

typedef enum {
  MM_SUCCESS = 0,
  MM_ERR_INVALID_ADDRESS = -1,
  MM_ERR_INVALID_ALIGNMENT = -2,
  MM_ERR_INVALID_PM_ALIGNMENT = -3,
  MM_ERR_OUT_OF_MEMORY = -4,
  MM_ERR_ALREADY_MAPPED = -5,
  MM_ERR_HUGE_PAGE_CONFLICT = -6,
  MM_ERR_INVALID_FLAGS = -7,
  MM_ERR_NOT_MAPPED = -8,
  MM_ERR_INVALID_SIZE = -9,
  MM_ERR_INVALID_PAGE_SIZE = -10,
  MM_ERR_INVALID_PAGE_TABLE = -11,
  MM_ERR_INVALID_PARAMETER = -12,
} mm_result_t;

extern uintptr_t hhdm_offset;

struct vm_area_s {
  void* cursor;
  void* fragment_list;
};

struct mm_state_s {
  uintptr_t physical_memory_base;
  size_t physical_memory_size;

  uintptr_t hhdm_offset;

  size_t page_size;

  void* kernel_root_table;

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
  size_t kernel_thread_stack_size;

  uintptr_t user_virtual_base;
  uintptr_t user_stack_base;
  size_t user_stack_size;
  size_t user_kernel_stack_size;
  uintptr_t user_mmap_base;

  struct vm_area_s stack_state;
};

static inline void* virt_to_phys(void* ptr) {
  return (void*)((uintptr_t)ptr - hhdm_offset);
}

static inline void* phys_to_virt(void* ptr) {
  return (void*)((uintptr_t)ptr + hhdm_offset);
}

void* mm_get_root_table(void);

int mm_init(void);

bool mmap(void* virt_addr, void* phys_addr);

void ummap(void* virt_addr);

bool map_userspace(void* virt_addr, void* phys_addr, mm_flags_t flags);

void unmap_userspace(void* virt_addr);

bool allocate_userspace(void* virt_addr, size_t size, mm_flags_t flags);

void free_userspace(void* addr, size_t size);

uint64_t mm_get_mmu_flags(mm_flags_t flags);

void* mm_get_kernel_root_table(void);

size_t mm_get_kernel_thread_stack_size(void);

size_t mm_get_page_size(void);

void* mm_get_user_stack_base(void);

void* mm_get_user_mmap_base(void);

size_t mm_get_user_stack_size(void);

void* mm_get_user_virtual_base(void);

mm_result_t mm_create_page_table(uintptr_t* user_root_table);

mm_result_t mm_allocate_kstack(void* root_table, uintptr_t* stack_base);

mm_result_t mm_free_kstack(void* root_table, uintptr_t stack_base);

mm_result_t mm_allocate_pstack(void* root_table, uintptr_t* stack_base);

mm_result_t mm_free_pstack(void* root_table, uintptr_t stack_base);
