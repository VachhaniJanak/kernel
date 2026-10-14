#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/syscall.h>
#include <fs/fs.h>
#include <kernel.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
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

#include "elf.h"
#include "vma.h"

// #define PROCESS_DEBUG

extern struct mm_state_s mm_state;

static inline uintptr_t process_mmap(uintptr_t hint_addr, size_t length,
                                     int prot, int flags, int fd,
                                     size_t offset) {

  UNUSED(hint_addr);
  UNUSED(fd);
  UNUSED(offset);
                                          
  process_t* current = scheduler_get_current_process();

  if (!current || length == 0) {
    return -1;
  }

  if (flags & VMA_FILE_BACKED) {
    return -1;
  }

  const size_t page_size = mm_get_page_size();
  size_t aligned_length = page_align_up(length, page_size);
  uintptr_t gap_start;

  if (!vma_find_gap(current->mmap_vma, true, aligned_length, &gap_start)) {
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
    return -1;
  }

#ifdef PROCESS_DEBUG
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
  void* root_table = phys_to_virt((void*)current->page_table);

  for (size_t i = 0; i < num_pages_to_free; i++) {
    uintptr_t page_to_free = start + i * page_size;
    uintptr_t phys_addr = 0;

    mm_result_t result =
        unmap_page(root_table, (void*)page_to_free, &phys_addr);

    if (result == MM_SUCCESS && phys_addr != 0) {
      pmm_free(&mm_state.pmm_state, (void*)phys_addr);
      continue;
    }
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

    if (!vma_get_intersection(current_vma, addr, end_addr, &start, &end)) {
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
  void* root_table = phys_to_virt((void*)current->page_table);
  mm_flags_t flags = vma_get_flags(prot) | MM_FLAG_USER;

  for (size_t i = 0; i < num_pages; i++) {
    uintptr_t virt_addr = start + i * page_size;
    mm_result_t result = change_page_flags(root_table, (void*)virt_addr, flags);

    if (result != MM_SUCCESS) {
    }
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

    if (!vma_get_intersection(current_vma, addr, end_addr, &start, &end)) {
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
    return 0;
  }

  vma_t* heap_vma = current_process->heap_vma;

  if (heap_vma == NULL) {
    return 0;
  }

  if (new_brk < heap_vma->vm_start) {
    return current_process->brk;
  }

  const size_t page_size = mm_get_page_size();
  uintptr_t aligned_new_brk = page_align_up(new_brk, page_size);
  vma_t* next_vma = heap_vma->next;

  // check for collision with the next VMA
  if (next_vma && aligned_new_brk > next_vma->vm_start) {
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
    void* root_table = phys_to_virt((void*)current_process->page_table);

    for (size_t i = 0; i < num_pages_to_free; i++) {
      uintptr_t page_to_free = new_end + i * page_size;
      uintptr_t phys_addr = 0;

      mm_result_t result =
          unmap_page(root_table, (void*)page_to_free, &phys_addr);

      if (result == MM_SUCCESS && phys_addr != 0) {
        pmm_free(&mm_state.pmm_state, (void*)phys_addr);
        continue;
      }
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

process_t* kprocess_init(const char* name) {
  if (!name) {
    return NULL;
  }

  process_t* process = kmalloc(sizeof(process_t));

  if (!process) {
    log_error("Failed to allocate memory for process: %s", name);
    return NULL;
  }

  memset(process, 0, sizeof(process_t));
  strncpy(process->name, name, PROCESS_NAME_LEN - 1);

  process->page_table = (uintptr_t)mm_get_kernel_root_table();
  process->thread_list_head = NULL;
  process->vma_head = NULL;
  process->vma_tail = NULL;
  process->heap_vma = NULL;
  process->brk = 0;
  process->mmap_vma = NULL;
  process->active_threads = 0;

  void* file_descriptor = kmalloc(sizeof(void*) * MAX_SYSTEM_OPEN_FILES);

  if (!file_descriptor) {
    kfree(process);
    return NULL;
  }

  memset(file_descriptor, 0, sizeof(void*) * MAX_SYSTEM_OPEN_FILES);

  process->open_files = (struct file**)file_descriptor;

  spinlock_init(&process->lock);

  process->pid = scheduler_get_next_pid();
  return process;
}

int kprocess_add_basic_fd(process_t* process) {
  // open stdin, stdout, stderr for kernel process
  process->open_files[0] = fs_open("/dev/console", VFS_O_RDONLY);

  // stdout points to the same as stdin
  process->open_files[1] = fs_open("/dev/console", VFS_O_WRONLY);

  // stderr points to the same as stdin
  process->open_files[2] = process->open_files[1];

  if (!process->open_files[0] || !process->open_files[1] ||
      !process->open_files[2]) {
    return -1;
  }

  // Increment the reference count for stdout, and stderr
  fs_inc_file_refcount(process->open_files[0], 1);

  return 0;
}

process_t* uprocess_init(const char* name, process_t* parent_process) {
  if (!name || !parent_process) {
    return NULL;
  }

  process_t* process = kmalloc(sizeof(process_t));

  if (!process) {
    log_error("Failed to allocate memory for process: %s", name);
    return NULL;
  }

  memset(process, 0, sizeof(process_t));
  strncpy(process->name, name, PROCESS_NAME_LEN - 1);

  uintptr_t addr;
  mm_result_t result = mm_create_page_table(&addr);

  if (result != MM_SUCCESS) {
    kfree(process);
    return NULL;
  }

  process->page_table = (uintptr_t)addr;
  process->thread_list_head = NULL;
  process->vma_head = NULL;
  process->vma_tail = NULL;
  process->heap_vma = NULL;
  process->brk = 0;
  process->mmap_vma = NULL;
  process->active_threads = 0;

  void* file_descriptor = kmalloc(sizeof(void*) * MAX_SYSTEM_OPEN_FILES);

  if (!file_descriptor) {
    kfree(process);
    return NULL;
  }

  memset(file_descriptor, 0, sizeof(void*) * MAX_SYSTEM_OPEN_FILES);

  process->open_files = (struct file**)file_descriptor;

  for (int i = 0; i < MAX_SYSTEM_OPEN_FILES; i++) {
    if (!parent_process->open_files[i]) {
      continue;
    }

    process->open_files[i] = parent_process->open_files[i];
    fs_inc_file_refcount(process->open_files[i], 1);
  }

  spinlock_init(&process->lock);

  process->pid = scheduler_get_next_pid();
  return process;
}

int load_user_process(process_t** process, const char* elf_path, void* arg) {
  void* entry_point;
  process_t temp_process = {0};

  int result = load_elf_file(&temp_process, elf_path, &entry_point);

  if (result != 0) {
    vma_free(&temp_process);
    return -1;
  }

  vma_add_nullspace(&temp_process, 0x0, (uintptr_t)mm_get_user_virtual_base());

  process_t* current_process = scheduler_get_current_process();
  process_t* new_process = uprocess_init("user-demo", current_process);

  if (!new_process) {
    vma_free(&temp_process);
    return -1;
  }

  // Copy the VMA list from the temporary process to the new process
  new_process->vma_head = temp_process.vma_head;
  new_process->vma_tail = temp_process.vma_tail;

  void (*user_main)(void*) = (void (*)(void*))entry_point;
  void* root_table = phys_to_virt((void*)new_process->page_table);

  // Allocate stack for the thread
  mm_result_t mm_result;
  uintptr_t stack_base;
  mm_result = mm_allocate_pstack(root_table, &stack_base);

  if (mm_result != MM_SUCCESS) {
    vma_free(new_process);
    return -1;
  }

  thread_t* main_thread =
      pthread_init("main", (void*)stack_base, user_main, arg);

  if (!main_thread) {
    vma_free(new_process);
    return -1;
  }

  // vma_heap and vma_mmap initialization
  // add guard page for heap and mmap

  if (!new_process->vma_tail) {
    vma_free(new_process);
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
    vma_free(new_process);
    return -1;
  }

  new_process->brk = heap_vma->vm_start;
  new_process->heap_vma = heap_vma;

  uintptr_t mmap_start = mm_get_user_mmap_base();
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
    vma_free(new_process);
    return -1;
  }

  new_process->mmap_vma = mmap_vma;

  // Initialize the user stack VMA
  const size_t stack_size = mm_get_user_stack_size();
  uintptr_t s_stack = main_thread->user_stack_base;

  s_stack -= page_align_up(stack_size, page_size);

  uintptr_t e_stack = main_thread->user_stack_base;

  // Add a VMA for the user stack
  vma.vm_start = s_stack;
  vma.vm_end = e_stack;
  vma.flags = VMA_ANONYMOUS | VMA_WRITE | VMA_READ | VMA_EXEC | VMA_DOWNWARD;
  vma.file = NULL;
  vma.file_offset = 0;
  vma.file_size = 0;

  if (!vma_add(new_process, &vma)) {
    vma_free(new_process);
    return -1;
  }

#ifdef PROCESS_DEBUG
  vma_print(new_process);
#endif

  if (process != NULL) {
    *process = new_process;
  }

  scheduler_add_process(new_process);
  scheduler_add_thread(new_process, main_thread);
  scheduler_queue_push(&scheduler_state.ready_queue, main_thread, TASK_READY);

  return 0;
}

void send_signal(thread_t* target, int signal_num) {
  if (signal_num < 1 || signal_num > 31) {
    return;
  }

  unsigned long flags;

  spinlock_acquire(&target->lock, &flags);

  target->pending_signals |= (1 << signal_num);

  if (target->status == TASK_BLOCKED && target->wait_queue) {
    scheduler_queue_remove(target->wait_queue, target);
    scheduler_queue_push(&scheduler_state.ready_queue, target, TASK_READY);
  }

  spinlock_release(&target->lock, flags);
}

void sys_fork(syscall_frame_t* frame) {
  // Implementation for the fork system call
  log_info("Fork system call invoked");
  frame->syscall_num = -1;
}