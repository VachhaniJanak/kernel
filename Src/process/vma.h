#pragma once
#include <mm/mm.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declaration of process_t to avoid circular dependency
struct process_s;
typedef struct process_s process_t;

typedef enum {
  VMA_READ = 0x01,
  VMA_WRITE = 0x02,
  VMA_EXEC = 0x04,
  VMA_NONE = 0x08,
} vma_flags_t;

typedef enum {
  VMA_ANONYMOUS = 0x10,    // No file backing (Stack, Heap, BSS gap)
  VMA_FILE_BACKED = 0x20,  // Needs to be read from a file (ELF code/data)
  VMA_DOWNWARD = 0x40,
  VMA_SHARED = 0x80,
  VMA_PRIVATE = 0x100,
  VMA_GUARD = 0x200,
  VMA_MMAP = 0x400,
  VMA_HEAP = 0x800
} vma_type_t;

typedef struct vma_s {
  uint64_t vm_start;
  uint64_t vm_end;
  uint32_t flags;
  struct file* file;
  uint64_t file_offset;
  uint64_t file_size;
  struct vma_s* prev;
  struct vma_s* next;
} vma_t;

bool vma_get_intersection(vma_t* vma, uintptr_t region_start,
                          uintptr_t region_end, uintptr_t* start,
                          uintptr_t* end);

mm_flags_t vma_get_flags(uint32_t vma_flags);

vma_t* vma_add(process_t* process, vma_t* vma);

bool vma_remove(process_t* process, vma_t* vma);

void vma_free(process_t* process);

void vma_print(process_t* process);

bool vma_add_nullspace(process_t* process, uintptr_t start, uintptr_t end);

bool vma_find_gap(vma_t* vma_head, bool reverse, uintptr_t size,
                  uintptr_t* gap_start);
