#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/syscall.h>
#include <kernel.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <mm/vmm/kheap.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tty/tty_output.h>
#include <utils/log.h>
#include <utils/utils.h>
#include <vfs/vfs.h>

#include "elf.h"

// #define PROCESS_DEBUG
// #define PAGE_FAULT_DEBUG

extern spinlock_t scheduler_state_lock;

static inline size_t max(size_t a, size_t b) { return (a > b) ? a : b; }

static inline size_t min(size_t a, size_t b) { return (a < b) ? a : b; }

static inline bool get_intersection(vma_t* vma, uintptr_t region_start,
                                    uintptr_t region_end, uintptr_t* start,
                                    uintptr_t* end) {
  uintptr_t segment_start = (uintptr_t)vma->vm_start;
  uintptr_t segment_end = (uintptr_t)vma->vm_end;

  *start = max(segment_start, region_start);
  *end = min(segment_end, region_end);

  return *start < *end;
}

static inline mm_flags_t get_vma_flags(uint32_t vma_flags) {
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

static inline uintptr_t process_mmap(uintptr_t hint_addr, size_t length,
                                     int prot, int flags, int fd,
                                     size_t offset) {
  process_t* current = scheduler_get_current_process();

  if (!current || length == 0) {
    return -1;
  }

  if (flags & VMA_FILE_BACKED) {
#ifdef PROCESS_DEBUG
    log_error("File-backed mmap not yet implemented!");
#endif
    return -1;
  }

  const size_t page_size = mm_get_page_size();
  size_t aligned_length = page_align_up(length, page_size);
  uintptr_t gap_start;

  if (!vma_find_gap(current->mmap_vma, true, aligned_length, &gap_start)) {
#ifdef PROCESS_DEBUG
    log_error("Failed to find a suitable gap for mmap");
#endif
    return -1;
  }

  vma_t vma = {.vm_start = gap_start,
               .vm_end = gap_start + aligned_length,
               .flags = flags | prot,
               .file = NULL,
               .file_offset = 0,
               .file_size = 0,
               .prev = NULL,
               .next = NULL};

  if (!vma_add(current, &vma)) {
#ifdef PROCESS_DEBUG
    log_error("Failed to add VMA for mmap");
#endif
    return -1;
  }

#ifdef PROCESS_DEBUG
  log_print(
      "mmap: Allocated VMA from 0x%lx to 0x%lx for process '%s' (PID: %zu)\n",
      vma.vm_start, vma.vm_end, current->name, current->pid);
  log_print(" Flags: 0x%x, Prot: 0x%x, FD: %d, Offset: %zu\n", flags, prot, fd,
            offset);

  vma_print(current);
#endif
  return gap_start;
}

void sys_mmap(syscall_frame_t* frame) {
  uintptr_t addr = (uintptr_t)frame->arg1;  // Hint for where user wants it
  size_t length = (size_t)frame->arg2;
  int prot = (int)frame->arg3;          // Read/Write/Execute permissions
  int flags = (int)frame->arg4;         // Anonymous, Private, etc.
  int fd = (int)frame->arg5;            // File descriptor (ignored for Anon)
  size_t offset = (size_t)frame->arg6;  // File offset (ignored for Anon)

  uintptr_t result = process_mmap(addr, length, prot, flags, fd, offset);
  frame->syscall_num = result;
}

static inline void free_vma_pages(uintptr_t start, uintptr_t end,
                                  size_t page_size, process_t* current) {
  const size_t num_pages_to_free = (end - start) / page_size;
  void* root_table = phys_to_virt(current->page_table);

  for (size_t i = 0; i < num_pages_to_free; i++) {
    uintptr_t page_to_free = start + i * page_size;
    uintptr_t phys_addr = 0;

    mm_result_t result =
        unmap_page(root_table, (void*)page_to_free, &phys_addr);

    if (result == MM_SUCCESS && phys_addr != 0) {
      pmm_free((void*)phys_addr);
      continue;
    }

#ifdef PROCESS_DEBUG
    log_error("Failed to unmap page at 0x%lx, result: %d", page_to_free,
              result);
#endif
  }
}

static inline int process_munmap(uintptr_t addr, size_t length) {
  const size_t page_size = mm_get_page_size();
  process_t* current = scheduler_get_current_process();

  if (!current || length == 0 || addr % page_size != 0) {
    return -1;
  }

  vma_t* current_vma = current->mmap_vma;
  uintptr_t end_addr = addr;
  end_addr += page_align_up(length, page_size);

  while (current_vma != NULL) {
    uintptr_t start, end;

    if (!get_intersection(current_vma, addr, end_addr, &start, &end)) {
      current_vma = current_vma->prev;
      continue;
    }

    // case 1: exact match
    if (start == current_vma->vm_start && end == current_vma->vm_end) {
      vma_remove(current, current_vma);
      free_vma_pages(start, end, page_size, current);
    }

    // case 2: unmap start of VMA
    else if (start == current_vma->vm_start && end < current_vma->vm_end) {
      free_vma_pages(start, end, page_size, current);
      current_vma->vm_start = end;
    }

    // case 3: unmap end of VMA
    else if (start > current_vma->vm_start && end == current_vma->vm_end) {
      free_vma_pages(start, end, page_size, current);
      current_vma->vm_end = start;
    }

    // case 4: unmap middle of VMA
    else if (start > current_vma->vm_start && end < current_vma->vm_end) {
      uintptr_t original_end = current_vma->vm_end;
      current_vma->vm_end = start;

      vma_t r_vma = {.vm_start = end,
                     .vm_end = original_end,
                     .flags = current_vma->flags,
                     .file = NULL,
                     .file_offset = 0,
                     .file_size = 0};

      vma_add(current, &r_vma);
      free_vma_pages(start, end, page_size, current);
    } else {
      return -1;  // Invalid case, should not happen
    }

    current_vma = current_vma->prev;
  }

  return 0;  // No overlapping VMA found
}

void sys_munmap(syscall_frame_t* frame) {
  uintptr_t addr = (uintptr_t)frame->arg1;  // Hint for where user wants it
  size_t length = (size_t)frame->arg2;

  int result = process_munmap(addr, length);
  frame->syscall_num = result;

#ifdef PROCESS_DEBUG
  process_t* current_process = scheduler_get_current_process();

  if (current_process) {
    vma_print(current_process);
  }
#endif
}

static inline void update_page_protections(uintptr_t start, uintptr_t end,
                                           size_t page_size, process_t* current,
                                           int prot) {
  const size_t num_pages = (end - start) / page_size;
  void* root_table = phys_to_virt(current->page_table);
  mm_flags_t flags = get_vma_flags(prot) | MM_FLAG_USER;

  for (size_t i = 0; i < num_pages; i++) {
    uintptr_t virt_addr = start + i * page_size;
    mm_result_t result = change_page_flags(root_table, (void*)virt_addr, flags);
#ifdef PROCESS_DEBUG
    if (result != MM_SUCCESS) {
      log_error(
          "Failed to change page flags for virtual address 0x%lx, result: %d",
          virt_addr, result);
    }
#endif
  }
}

static inline int process_mprotect(uintptr_t addr, size_t length,
                                   uint32_t prot) {
  const size_t page_size = mm_get_page_size();
  process_t* current = scheduler_get_current_process();

  if (!current || length == 0 || addr % page_size != 0) {
    return -1;
  }

  vma_t* current_vma = current->mmap_vma;
  uintptr_t end_addr = addr;
  end_addr += page_align_up(length, page_size);

  while (current_vma != NULL) {
    uintptr_t start, end;

    if (!get_intersection(current_vma, addr, end_addr, &start, &end)) {
      current_vma = current_vma->prev;
      continue;
    }

    // case 1: exact match
    if (start == current_vma->vm_start && end == current_vma->vm_end) {
      current_vma->flags = (current_vma->flags & ~0x0F) | prot;
      update_page_protections(start, end, page_size, current, prot);
    }

    // case 2: start of VMA
    else if (start == current_vma->vm_start && end < current_vma->vm_end) {
      vma_t l_vma = {.vm_start = start,
                     .vm_end = end,
                     .flags = (current_vma->flags & ~0x0F) | prot,
                     .file = NULL,
                     .file_offset = 0,
                     .file_size = 0};

      current_vma->vm_start = end;
      vma_add(current, &l_vma);
      update_page_protections(start, end, page_size, current, prot);
    }

    // case 3: end of VMA
    else if (start > current_vma->vm_start && end == current_vma->vm_end) {
      vma_t r_vma = {.vm_start = start,
                     .vm_end = end,
                     .flags = (current_vma->flags & ~0x0F) | prot,
                     .file = NULL,
                     .file_offset = 0,
                     .file_size = 0};

      current_vma->vm_end = start;
      vma_add(current, &r_vma);
      update_page_protections(start, end, page_size, current, prot);
    }

    // case 4: middle of VMA
    else if (start > current_vma->vm_start && end < current_vma->vm_end) {
      uintptr_t original_end = current_vma->vm_end;
      current_vma->vm_end = start;

      vma_t r_vma = {.vm_start = end,
                     .vm_end = original_end,
                     .flags = current_vma->flags,
                     .file = NULL,
                     .file_offset = 0,
                     .file_size = 0,
                     .prev = NULL,
                     .next = NULL};

      vma_t c_vma = {.vm_start = start,
                     .vm_end = end,
                     .flags = (current_vma->flags & ~0x0F) | prot,
                     .file = NULL,
                     .file_offset = 0,
                     .file_size = 0,
                     .prev = NULL,
                     .next = NULL};

      vma_add(current, &r_vma);
      vma_add(current, &c_vma);
      update_page_protections(start, end, page_size, current, prot);
    } else {
      return -1;  // Invalid case, should not happen
    }

    current_vma = current_vma->prev;
  }

  return 0;  // Successfully updated permissions
}

void sys_mprotect(syscall_frame_t* frame) {
  uintptr_t addr = (uintptr_t)frame->arg1;
  size_t length = (size_t)frame->arg2;
  uint32_t prot = (uint32_t)frame->arg3;

  int result = process_mprotect(addr, length, prot);
  frame->syscall_num = result;

#ifdef PROCESS_DEBUG
  process_t* current_process = scheduler_get_current_process();

  if (current_process) {
    vma_print(current_process);
  }
#endif
}

static inline uintptr_t process_set_brk(uintptr_t new_brk) {
  process_t* current_process = scheduler_get_current_process();

  if (current_process == NULL) {
#ifdef PROCESS_DEBUG
    log_error("No current process found while setting brk");
#endif
    return 0;
  }

  vma_t* heap_vma = current_process->heap_vma;

  if (heap_vma == NULL) {
#ifdef PROCESS_DEBUG
    log_error("Current process has no heap VMA");
#endif
    return 0;
  }

  if (new_brk < heap_vma->vm_start) {
#ifdef PROCESS_DEBUG
    log_error("Requested brk is below process virtual memory start");
#endif
    return current_process->brk;
  }

  const size_t page_size = mm_get_page_size();
  uintptr_t aligned_new_brk = page_align_up(new_brk, page_size);
  vma_t* next_vma = heap_vma->next;

#ifdef PROCESS_DEBUG
  log_print("Current brk: 0x%lx, Requested brk: 0x%lx, Aligned brk: 0x%lx\n",
            current_process->brk, new_brk, aligned_new_brk);
#endif

  // check for collision with the next VMA
  if (next_vma && aligned_new_brk > next_vma->vm_start) {
#ifdef PROCESS_DEBUG
    log_error("Requested brk collides with next VMA");
#endif
    return current_process->brk;
  }

  // Expanding the heap
  if (aligned_new_brk > heap_vma->vm_end) {
    heap_vma->vm_end = aligned_new_brk;
    current_process->brk = new_brk;
    return current_process->brk;
  }

  // Shrinking the heap
  if (aligned_new_brk < heap_vma->vm_end) {
    uintptr_t old_end = heap_vma->vm_end;
    uintptr_t new_end = aligned_new_brk;
    size_t num_pages_to_free = (old_end - new_end) / page_size;
    void* root_table = phys_to_virt(current_process->page_table);

    for (size_t i = 0; i < num_pages_to_free; i++) {
      uintptr_t page_to_free = new_end + i * page_size;
      uintptr_t phys_addr = 0;

      mm_result_t result =
          unmap_page(root_table, (void*)page_to_free, &phys_addr);

      if (result == MM_SUCCESS && phys_addr != 0) {
        pmm_free((void*)phys_addr);
        continue;
      }

#ifdef PROCESS_DEBUG
      log_error("Failed to unmap page at 0x%lx, result: %d", page_to_free,
                result);
#endif
    }

    // Update the heap VMA end address
    heap_vma->vm_end = aligned_new_brk;
  }

  current_process->brk = new_brk;
  return current_process->brk;
}

void sys_brk(syscall_frame_t* frame) {
  uintptr_t new_brk = (uintptr_t)frame->arg1;
  uintptr_t result = process_set_brk(new_brk);
  frame->syscall_num = result;

#ifdef PROCESS_DEBUG
  process_t* current_process = scheduler_get_current_process();
  log_print("sys_brk: New brk set to 0x%lx for process '%s' (PID: %zu)\n",
            result, current_process->name, current_process->pid);
  if (current_process) {
    vma_print(current_process);
  }
#endif
}

void sys_getpid(syscall_frame_t* frame) {
  process_t* current_process = scheduler_get_current_process();

  if (current_process == NULL) {
    frame->syscall_num = (size_t)-1;
    return;
  }

  frame->syscall_num = current_process->pid;
}

void kprocess_init(process_t* process) {
  kstrcpy(process->name, "kernel");

  process->page_table = (void*)mm_get_kernel_root_table();
  process->thread_list_start = NULL;
  process->thread_list_end = NULL;
  process->vma_head = NULL;
  process->vma_tail = NULL;
  process->heap_vma = NULL;
  process->brk = 0;
  process->mmap_vma = NULL;
  process->alive_threads = 0;

  spinlock_init(&process->lock);
}

static bool user_process_init(process_t* process, char* name) {
  if (process == NULL) {
    return false;
  }

  kstrcpy(process->name, name);

  uintptr_t addr;
  mm_result_t result = mm_create_page_table(&addr);

  if (result != MM_SUCCESS) {
#ifdef PROCESS_DEBUG
    log_error("Failed to create page table for user process");
#endif
    return false;
  }

  process->page_table = (void*)addr;
  process->thread_list_start = NULL;
  process->thread_list_end = NULL;
  process->vma_head = NULL;
  process->vma_tail = NULL;
  process->heap_vma = NULL;
  process->brk = 0;
  process->mmap_vma = NULL;
  process->alive_threads = 0;

  spinlock_init(&process->lock);

  return true;
}

int load_user_process(process_t** process, const char* elf_path, void* arg) {
  void* entry_point;
  process_t temp_process = {0};

#ifdef PROCESS_DEBUG
  log_print("Loading user process from ELF file: %s\n", elf_path);
#endif

  int result = load_elf_file(&temp_process, elf_path, &entry_point);

  if (result != 0) {
    vma_free(&temp_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to load ELF file: %d", result);
#endif
    return -1;
  }

  vma_add_nullspace(&temp_process, 0x0, (uintptr_t)mm_get_user_virtual_base());

#ifdef PROCESS_DEBUG
  log_print("ELF file loaded successfully. Entry point: 0x%lx\n",
            (uintptr_t)entry_point);
#endif

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* new_process = scheduler_add_process();

  if (new_process == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(&temp_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to create user process");
#endif
    return -1;
  }

  if (!user_process_init(new_process, "user-demo")) {
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(&temp_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to initialize user process");
#endif
    return -1;
  }

#ifdef PROCESS_DEBUG
  log_print("User Process Initialized: %s, (PID: %lu)\n", new_process->name,
            new_process->pid);
#endif

  // Copy the VMA list from the temporary process to the new process
  new_process->vma_head = temp_process.vma_head;
  new_process->vma_tail = temp_process.vma_tail;

  void (*user_main)(void*) = (void (*)(void*))entry_point;
  thread_t* main_thread = scheduler_add_thread(new_process);

  if (main_thread == NULL) {
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to create main thread for user process");
#endif
    return -1;
  }

  void* root_table = phys_to_virt(new_process->page_table);

  // Allocate stack for the thread
  mm_result_t mm_result;
  uintptr_t stack_base;
  mm_result = mm_allocate_pstack(root_table, &stack_base);

  if (mm_result != MM_SUCCESS) {
    scheduler_remove_thread(new_process, main_thread->tid);
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to allocate user stacks for main thread, error code: %d",
              mm_result);
#endif
    return false;
  }

  if (!pthread_init(new_process, "main", main_thread, (void*)stack_base,
                    user_main, arg)) {
    scheduler_remove_thread(new_process, main_thread->tid);
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to create and initialize main thread");
#endif
    return -1;
  }

  // vma_heap and vma_mmap initialization
  // add guard page for heap and mmap

  if (new_process->vma_tail == NULL) {
    scheduler_remove_thread(new_process, main_thread->tid);
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to initialize heap and mmap VMA for user process");
#endif
    return -1;
  }

  const size_t page_size = mm_get_page_size();
  uintptr_t heap_start = new_process->vma_tail->vm_end;

  heap_start = page_align_up(heap_start, page_size);
  heap_start += page_size;  // Skip the guard page

  vma_t vma = {
      .vm_start = heap_start,
      .vm_end = heap_start,
      .flags = VMA_HEAP | VMA_ANONYMOUS | VMA_READ | VMA_WRITE | VMA_EXEC,
      .file = NULL,
      .file_offset = 0,
      .file_size = 0,
  };

  vma_t* heap_vma = vma_add(new_process, &vma);

  if (!heap_vma) {
    scheduler_remove_thread(new_process, main_thread->tid);
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to add VMA for user heap");
#endif
    return -1;
  }

  new_process->brk = heap_vma->vm_start;
  new_process->heap_vma = heap_vma;

  uintptr_t mmap_start = (uintptr_t)mm_get_user_mmap_base();
  mmap_start = page_align_down(mmap_start, page_size);
  mmap_start -= page_size;  // Skip the guard page

  vma.vm_start = mmap_start;
  vma.vm_end = mmap_start + page_size;
  vma.flags = VMA_GUARD;  // No specific flags for mmap
  vma.file = NULL;
  vma.file_offset = 0;
  vma.file_size = 0;

  vma_t* mmap_vma = vma_add(new_process, &vma);

  if (!mmap_vma) {
    scheduler_remove_thread(new_process, main_thread->tid);
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to add VMA for user mmap");
#endif
    return -1;
  }

  new_process->mmap_vma = mmap_vma;

  // Initialize the user stack VMA
  const size_t stack_size = mm_get_user_stack_size();
  uintptr_t s_stack = (uintptr_t)main_thread->user_stack_base;

  s_stack -= page_align_up(stack_size, page_size);

  uintptr_t e_stack = (uintptr_t)main_thread->user_stack_base;

  // Add a VMA for the user stack
  vma.vm_start = s_stack;
  vma.vm_end = e_stack;
  vma.flags = VMA_ANONYMOUS | VMA_WRITE | VMA_READ | VMA_EXEC | VMA_DOWNWARD;
  vma.file = NULL;
  vma.file_offset = 0;
  vma.file_size = 0;

  if (!vma_add(new_process, &vma)) {
    scheduler_remove_thread(new_process, main_thread->tid);
    scheduler_remove_process(new_process->pid);
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    vma_free(new_process);
#ifdef PROCESS_DEBUG
    log_error("Failed to add VMA for user stack");
#endif
    return -1;
  }

#ifdef PROCESS_DEBUG
  vma_print(new_process);
#endif

  new_process->alive_threads = 1;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  if (process != NULL) {
    *process = new_process;
  }

  return 0;
}

static inline bool handle_user_page_fault(uintptr_t faulting_address) {
#ifdef PAGE_FAULT_DEBUG
  log_debug("User Page Fault Handler:\n");
#endif
  const size_t page_size = mm_get_page_size();
  process_t* current_process = scheduler_get_current_process();

  if (current_process == NULL) {
#ifdef PAGE_FAULT_DEBUG
    log_error("  No current process found while handling page fault");
#endif
    return false;
  }

#ifdef PAGE_FAULT_DEBUG
  log_print("  Current process: '%s' (PID: %zu)\n", current_process->name,
            current_process->pid);
  log_print("  Current thread: '%s' (TID: %zu)\n",
            scheduler_get_current_thread()->name,
            scheduler_get_current_thread()->tid);
#endif

  void* page_start_addr = (void*)page_align_down(faulting_address, page_size);
  void* page_phys_addr = pmm_alloc(page_size);

  if (page_phys_addr == NULL) {
#ifdef PAGE_FAULT_DEBUG
    log_error(
        "Failed to allocate physical page for user page fault at address "
        "0x%lx",
        faulting_address);
#endif
    return false;
  }

  void* buffer = kmalloc(page_size);

  if (buffer == NULL) {
#ifdef PAGE_FAULT_DEBUG
    log_error("Failed to allocate buffer for user page fault at address 0x%lx",
              faulting_address);
#endif
    pmm_free(page_phys_addr);
    return false;
  }

  void* page_virt_addr = phys_to_virt(page_phys_addr);

  // Check if the faulting address falls within any of the process's VMAs
  vma_t* current_vma = current_process->vma_head;
  mm_flags_t flags = MM_FLAG_USER;
  bool is_any_segment_found = false;

#ifdef PAGE_FAULT_DEBUG
  log_print("  Allocating virtual page at address 0x%lx\n",
            (uintptr_t)page_start_addr);
#endif

  while (current_vma != NULL) {
    uintptr_t start, end;

    // Check if the faulting address falls within the current VMA
    if (!get_intersection(current_vma, (uintptr_t)page_start_addr,
                          (uintptr_t)page_start_addr + page_size, &start,
                          &end)) {
      current_vma = current_vma->next;
      continue;
    }

    is_any_segment_found = true;
    size_t overlap_size = end - start;
    flags |= get_vma_flags(current_vma->flags);

#ifdef PAGE_FAULT_DEBUG
    log_print("  Found overlapping VMA:\n");
    log_print("    VMA Start: 0x%lx\n", current_vma->vm_start);
    log_print("    VMA End: 0x%lx\n", current_vma->vm_end);
    log_print("    Overlap Start: 0x%lx\n", start);
    log_print("    Overlap End: 0x%lx\n", end);
    log_print("    Overlap Size: %zu bytes\n", overlap_size);
    log_print("    VMA Flags: 0x%x\n", current_vma->flags);
#endif

    if (current_vma->flags & VMA_FILE_BACKED) {
      size_t file_offset =
          current_vma->file_offset + (start - current_vma->vm_start);

      vfs_seek(current_vma->file, file_offset);
      int ret = vfs_read(current_vma->file, buffer, overlap_size);

      if (ret < 0) {
#ifdef PAGE_FAULT_DEBUG
        log_error(
            "Failed to read from file for file-backed VMA at address 0x%lx",
            faulting_address);
#endif
        kfree(buffer);
        pmm_free(page_phys_addr);
        return false;
      }

      uintptr_t addr_offset = start - (uintptr_t)page_start_addr;
      void* dest_addr = (void*)((uintptr_t)page_virt_addr + addr_offset);

#ifdef PAGE_FAULT_DEBUG
      log_print("  File-Backed VMA:\n", ret);
      log_print("    File Offset: 0x%lx\n", file_offset);
      log_print("    Bytes Read: %d\n", ret);
      log_print("    Destination Address: 0x%lx\n", (uintptr_t)dest_addr);
      log_print("    Overlap Size: %zu bytes\n", overlap_size);
#endif
      kmemcpy(dest_addr, buffer, overlap_size);

    } else if (current_vma->flags & VMA_ANONYMOUS) {
      if (current_vma->flags & VMA_NONE) {
        // pages not accessible, return false
        return false;
      }

      uintptr_t addr_offset = start - (uintptr_t)page_start_addr;
      void* dest_addr = (void*)((uintptr_t)page_virt_addr + addr_offset);

#ifdef PAGE_FAULT_DEBUG
      log_print("  Anonymous VMA:\n");
      log_print("    Destination Address: 0x%lx\n", (uintptr_t)dest_addr);
      log_print("    Overlap Size: %zu bytes\n", overlap_size);
#endif
      kmemset(dest_addr, 0, overlap_size);
    }

    // Move to the next VMA in the list
    current_vma = current_vma->next;
  }

  if (!is_any_segment_found) {
#ifdef PAGE_FAULT_DEBUG
    log_error(
        "No valid VMA found for user page fault at address 0x%lx in process "
        "'%s' (PID: %zu)",
        faulting_address, current_process->name, current_process->pid);
#endif
    kfree(buffer);
    pmm_free(page_phys_addr);
    return false;
  }

  void* root_table = phys_to_virt(current_process->page_table);
  mm_result_t result =
      map_page(root_table, page_start_addr, page_phys_addr, flags);

  if (result != MM_SUCCESS) {
#ifdef PAGE_FAULT_DEBUG
    log_error(
        "Failed to map page for user page fault at address 0x%lx, Error: %d",
        faulting_address, result);
#endif
    pmm_free(page_phys_addr);
    kfree(buffer);
    return false;
  }

  kfree(buffer);
  return true;
}

void page_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  uintptr_t faulting_address = page_fault_addr();

#ifdef PAGE_FAULT_DEBUG
  log_debug("Page Fault Exception:\n");
  log_print("  RIP: 0x%lx\n", frame->rip);
  log_print("  Previous RSP: 0x%lx\n", frame->rsp);
  log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  log_print("  Current RSP: 0x%lx\n", get_stack_top());
  log_print("  Address: 0x%lx\n", faulting_address);
  log_print("  Error code: 0x%lx\n", frame->error_code);

  if (frame->error_code & MMU_PF_PLV_MASK) {
    log_print("  Page-level protection violation\n");
  } else {
    log_print("  Non-present page\n");
  }

  if (frame->error_code & MMU_PF_WA_MASK) {
    log_print("  Write access\n");
  } else {
    log_print("  Read access\n");
  }

  if (frame->error_code & MMU_PF_UM_MASK) {
    log_print("  User-mode access\n");
  } else {
    log_print("  Kernel-mode access\n");
  }

  log_newline();
#endif

  // handle user space page fault
  if (frame->error_code & MMU_PF_UM_MASK &&
      !(frame->error_code & MMU_PF_PLV_MASK)) {
    if (faulting_address < (uintptr_t)mm_get_user_virtual_base()) {
      tty_printf(
          ANSI_COLOR_BRIGHT_RED
          "Segmentation fault: Invalid user space address 0x%lx\n" ANSI_RESET,
          faulting_address);
      log_error(
          ANSI_COLOR_BRIGHT_RED
          "Segmentation fault: Invalid user space address 0x%lx\n" ANSI_RESET,
          faulting_address);
      scheduler_terminate_current_process();

#ifdef PAGE_FAULT_DEBUG
      log_error("Segmentation fault: Invalid user space address 0x%lx",
                faulting_address);
#endif
    }

    if (handle_user_page_fault(faulting_address)) {
#ifdef PAGE_FAULT_DEBUG
      log_info("Handled user page fault at address 0x%lx", faulting_address);
#endif
      return;
    }

    log_debug(ANSI_COLOR_BRIGHT_RED
              "Failed to handle user page fault at address 0x%lx\n" ANSI_RESET,
              faulting_address);
    scheduler_terminate_current_process();

#ifdef PAGE_FAULT_DEBUG
    log_error("Failed to handle user page fault at address 0x%lx",
              faulting_address);
#endif
  }

  tty_printf("Segmentation fault at address 0x%lx, RIP: 0x%lx",
             faulting_address, frame->rip);
  log_error("Segmentation fault at address 0x%lx, RIP: 0x%lx", faulting_address,
            frame->rip);
  while (1);
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
      vfs_close(current->file);
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
