#include "vma.h"

#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <process/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>

static inline size_t max(size_t a, size_t b) { return (a > b) ? a : b; }

static inline size_t min(size_t a, size_t b) { return (a < b) ? a : b; }

bool vma_get_intersection(vma_t* vma, uintptr_t region_start,
                          uintptr_t region_end, uintptr_t* start,
                          uintptr_t* end) {
  uintptr_t segment_start = (uintptr_t)vma->vm_start;
  uintptr_t segment_end = (uintptr_t)vma->vm_end;

  *start = max(segment_start, region_start);
  *end = min(segment_end, region_end);

  return *start < *end;
}

mm_flags_t vma_get_flags(uint32_t vma_flags) {
  mm_flags_t flags = 0;

  if (vma_flags & VMA_READ) {
    flags |= MM_FLAG_READ;
  }

  if (vma_flags & VMA_WRITE) {
    flags |= MM_FLAG_WRITABLE;
  }

  if (vma_flags & VMA_EXEC) {
    flags |= MM_FLAG_EXE;
  }

  return flags;
}

vma_t* vma_add(process_t* process, vma_t* vma) {
  vma_t* new_vma = kmalloc(sizeof(vma_t));

  if (new_vma == NULL) {
    return NULL;
  }

  *new_vma = *vma;

  // add node in sorted order based on vm_start

  // If the list is empty, set the new VMA as the head and tail
  if (process->vma_head == NULL) {
    process->vma_head = new_vma;
    process->vma_tail = new_vma;
    new_vma->prev = NULL;
    new_vma->next = NULL;
    return new_vma;
  }

  // If the new VMA should be inserted before the head
  if (new_vma->vm_start < process->vma_head->vm_start) {
    new_vma->next = process->vma_head;
    new_vma->prev = NULL;
    process->vma_head->prev = new_vma;
    process->vma_head = new_vma;
    return new_vma;
  }

  vma_t* current = process->vma_head;

  while (current->next != NULL) {
    if (current->next->vm_start > new_vma->vm_start) {
      break;
    }
    current = current->next;
  }

  new_vma->next = current->next;
  new_vma->prev = current;
  current->next = new_vma;

  if (new_vma->next != NULL) {
    new_vma->next->prev = new_vma;
    return new_vma;
  }

  process->vma_tail = new_vma;  // Update tail if added at the end
  return new_vma;
}

bool vma_add_nullspace(process_t* process, uintptr_t start, uintptr_t end) {
  if (process == NULL || start >= end) {
    return false;
  }

  vma_t* new_vma = kmalloc(sizeof(vma_t));

  if (new_vma == NULL) {
    return false;
  }

  new_vma->vm_start = start;
  new_vma->vm_end = end;
  new_vma->flags = 0;
  new_vma->file = NULL;
  new_vma->file_offset = 0;
  new_vma->file_size = 0;

  return vma_add(process, new_vma);
}

bool vma_remove(process_t* process, vma_t* vma) {
  if (process->vma_head == NULL) {
    return false;
  }

  if (process->vma_head == vma) {
    process->vma_head = vma->next;

    if (process->vma_head != NULL) {
      process->vma_head->prev = NULL;
    } else {
      process->vma_tail = NULL;  // List is now empty
    }

    kfree(vma);
    return true;
  }

  vma_t* current = process->vma_head;

  while (current->next != NULL) {
    if (current->next == vma) {
      current->next = vma->next;

      if (vma->next != NULL) {
        vma->next->prev = current;
      } else {
        process->vma_tail = current;
      }

      kfree(vma);
      return true;
    }
    current = current->next;
  }

  return false;
}

void vma_free(process_t* process) {
  vma_t* current = process->vma_head;

  while (current != NULL) {
    vma_t* next = current->next;

    if (current->file != NULL) {
      fs_close(current->file);
    }

    kfree(current);
    current = next;
  }

  process->vma_head = NULL;
}

void vma_print(process_t* process) {
  vma_t* current = process->vma_head;

  log_print("VMAs for process '%s' (PID: %zu):\n", process->name, process->pid);

  while (current != NULL) {
    log_print("  VMA Start: 0x%lx, VMA End: 0x%lx, Flags: 0x%x\n",
              current->vm_start, current->vm_end, current->flags);
    current = current->next;
  }

  // print tail
  log_print("  VMA Tail: 0x%lx\n",
            process->vma_tail ? process->vma_tail->vm_start : 0);
}

bool vma_find_gap(vma_t* vma_head, bool reverse, uintptr_t size,
                  uintptr_t* gap_start) {
  if (vma_head == NULL || size == 0) {
    return false;
  }

  if (!reverse) {
    vma_t* current = vma_head;

    // Start searching from the provided start address
    uint64_t start_addr = current->vm_start;

    while (current != NULL) {
      uint64_t gap_size = current->vm_start - start_addr;

      if (gap_size >= size) {
        *gap_start = start_addr;
        return true;
      }

      start_addr = current->vm_end;
      current = current->next;
    }

    return false;
  }

  // find gap from top
  vma_t* current = vma_head;

  // Start searching from the provided start address
  uint64_t end_addr = current->vm_end;

  while (current != NULL) {
    uint64_t gap_size = end_addr - current->vm_end;

    if (gap_size >= size) {
      *gap_start = end_addr - size;
      return true;
    }

    end_addr = current->vm_start;
    current = current->prev;
  }

  return false;
}
