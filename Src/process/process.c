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

static uint64_t setup_user_stack(uint64_t user_stack_base,
                                 char *const user_argv[],
                                 char *const user_envp[]);

static inline uintptr_t process_mmap(uintptr_t hint_addr, size_t length,
                                     int prot, int flags, int fd,
                                     size_t offset) {
  UNUSED(hint_addr);
  UNUSED(fd);
  UNUSED(offset);

  process_t *current = scheduler_get_current_process();

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

void sys_mmap(syscall_frame_t *frame) {
  uintptr_t addr = (uintptr_t)frame->arg1; // Hint for where user wants it
  size_t length = (size_t)frame->arg2;
  int prot = (int)frame->arg3;         // Read/Write/Execute permissions
  int flags = (int)frame->arg4;        // Anonymous, Private, etc.
  int fd = (int)frame->arg5;           // File descriptor (ignored for Anon)
  size_t offset = (size_t)frame->arg6; // File offset (ignored for Anon)

  uintptr_t result = process_mmap(addr, length, prot, flags, fd, offset);
  frame->syscall_num = result;
}

static inline void free_vma_pages(uintptr_t start, uintptr_t end,
                                  size_t page_size, process_t *current) {
  const size_t num_pages_to_free = (end - start) / page_size;
  void *root_table = phys_to_virt((void *)current->page_table);

  for (size_t i = 0; i < num_pages_to_free; i++) {
    uintptr_t page_to_free = start + i * page_size;
    uintptr_t phys_addr = 0;

    mm_result_t result =
        unmap_page(root_table, (void *)page_to_free, &phys_addr);

    if (result == MM_SUCCESS && phys_addr != 0) {
      pmm_free(&mm_state.pmm_state, (void *)phys_addr);
      continue;
    }
  }
}

static inline int process_munmap(uintptr_t addr, size_t length) {
  const size_t page_size = mm_get_page_size();
  process_t *current = scheduler_get_current_process();

  if (!current || length == 0 || addr % page_size != 0) {
    return -1;
  }

  vma_t *current_vma = current->mmap_vma;
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
      return -1; // Invalid case, should not happen
    }

    current_vma = current_vma->prev;
  }

  return 0; // No overlapping VMA found
}

void sys_munmap(syscall_frame_t *frame) {
  uintptr_t addr = (uintptr_t)frame->arg1; // Hint for where user wants it
  size_t length = (size_t)frame->arg2;

  int result = process_munmap(addr, length);
  frame->syscall_num = result;

#ifdef PROCESS_DEBUG
  process_t *current_process = scheduler_get_current_process();

  if (current_process) {
    vma_print(current_process);
  }
#endif
}

static inline void update_page_protections(uintptr_t start, uintptr_t end,
                                           size_t page_size, process_t *current,
                                           int prot) {
  const size_t num_pages = (end - start) / page_size;
  void *root_table = phys_to_virt((void *)current->page_table);
  mm_flags_t flags = vma_get_flags(prot) | MM_FLAG_USER;

  for (size_t i = 0; i < num_pages; i++) {
    uintptr_t virt_addr = start + i * page_size;
    mm_result_t result =
        change_page_flags(root_table, (void *)virt_addr, flags);

    if (result != MM_SUCCESS) {
    }
  }
}

static inline int process_mprotect(uintptr_t addr, size_t length,
                                   uint32_t prot) {
  const size_t page_size = mm_get_page_size();
  process_t *current = scheduler_get_current_process();

  if (!current || length == 0 || addr % page_size != 0) {
    return -1;
  }

  vma_t *current_vma = current->mmap_vma;
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
      return -1; // Invalid case, should not happen
    }

    current_vma = current_vma->prev;
  }

  return 0; // Successfully updated permissions
}

void sys_mprotect(syscall_frame_t *frame) {
  uintptr_t addr = (uintptr_t)frame->arg1;
  size_t length = (size_t)frame->arg2;
  uint32_t prot = (uint32_t)frame->arg3;

  int result = process_mprotect(addr, length, prot);
  frame->syscall_num = result;

#ifdef PROCESS_DEBUG
  process_t *current_process = scheduler_get_current_process();

  if (current_process) {
    vma_print(current_process);
  }
#endif
}

static inline uintptr_t process_set_brk(uintptr_t new_brk) {
  process_t *current_process = scheduler_get_current_process();

  if (current_process == NULL) {
    return 0;
  }

  vma_t *heap_vma = current_process->heap_vma;

  if (heap_vma == NULL) {
    return 0;
  }

  if (new_brk < heap_vma->vm_start) {
    return current_process->brk;
  }

  const size_t page_size = mm_get_page_size();
  uintptr_t aligned_new_brk = page_align_up(new_brk, page_size);
  vma_t *next_vma = heap_vma->next;

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
    void *root_table = phys_to_virt((void *)current_process->page_table);

    for (size_t i = 0; i < num_pages_to_free; i++) {
      uintptr_t page_to_free = new_end + i * page_size;
      uintptr_t phys_addr = 0;

      mm_result_t result =
          unmap_page(root_table, (void *)page_to_free, &phys_addr);

      if (result == MM_SUCCESS && phys_addr != 0) {
        pmm_free(&mm_state.pmm_state, (void *)phys_addr);
        continue;
      }
    }

    // Update the heap VMA end address
    heap_vma->vm_end = aligned_new_brk;
  }

  current_process->brk = new_brk;
  return current_process->brk;
}

void sys_brk(syscall_frame_t *frame) {
  uintptr_t new_brk = (uintptr_t)frame->arg1;
  uintptr_t result = process_set_brk(new_brk);
  frame->syscall_num = result;

#ifdef PROCESS_DEBUG
  process_t *current_process = scheduler_get_current_process();
  if (current_process) {
    vma_print(current_process);
  }
#endif
}

void sys_getpid(syscall_frame_t *frame) {
  process_t *current_process = scheduler_get_current_process();

  if (current_process == NULL) {
    frame->syscall_num = (size_t)-1;
    return;
  }

  frame->syscall_num = current_process->pid;
}

process_t *kprocess_init(const char *name) {
  if (!name) {
    return NULL;
  }

  process_t *process = kmalloc(sizeof(process_t));

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

  void *file_descriptor = kmalloc(sizeof(void *) * MAX_SYSTEM_OPEN_FILES);

  if (!file_descriptor) {
    kfree(process);
    return NULL;
  }

  memset(file_descriptor, 0, sizeof(void *) * MAX_SYSTEM_OPEN_FILES);

  process->open_files = (struct file **)file_descriptor;

  spinlock_init(&process->lock);

  process->pid = scheduler_get_next_pid();
  return process;
}

int kprocess_add_basic_fd(process_t *process) {
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

process_t *uprocess_init(const char *name, process_t *parent_process) {
  if (!name || !parent_process) {
    return NULL;
  }

  process_t *process = kmalloc(sizeof(process_t));

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

  void *file_descriptor = kmalloc(sizeof(void *) * MAX_SYSTEM_OPEN_FILES);

  if (!file_descriptor) {
    kfree(process);
    return NULL;
  }

  memset(file_descriptor, 0, sizeof(void *) * MAX_SYSTEM_OPEN_FILES);

  process->open_files = (struct file **)file_descriptor;

  for (int i = 0; i < MAX_SYSTEM_OPEN_FILES; i++) {
    if (!parent_process->open_files[i]) {
      continue;
    }

    process->open_files[i] = parent_process->open_files[i];
    fs_inc_file_refcount(process->open_files[i], 1);
  }

  process->pid = scheduler_get_next_pid();

  spinlock_init(&process->lock);
  process_add_child(parent_process, process);
  return process;
}

int load_user_process(process_t **process, const char *elf_path, void *arg) {
  void *entry_point;
  process_t temp_process = {0};

  int result = load_elf_file(&temp_process, elf_path, &entry_point);

  if (result != 0) {
    vma_free(&temp_process);
    return -1;
  }

  vma_add_nullspace(&temp_process, 0x0, (uintptr_t)mm_get_user_virtual_base());

  process_t *current_process = scheduler_get_current_process();
  process_t *new_process = uprocess_init("user-demo", current_process);

  if (!new_process) {
    vma_free(&temp_process);
    return -1;
  }

  // copy exe and cwd
  new_process->exe = temp_process.exe;
  new_process->cwd = temp_process.cwd;

  // Copy the VMA list from the temporary process to the new process
  new_process->vma_head = temp_process.vma_head;
  new_process->vma_tail = temp_process.vma_tail;

  void (*user_main)(void *) = (void (*)(void *))entry_point;
  void *root_table = phys_to_virt((void *)new_process->page_table);

  // Allocate stack for the thread
  mm_result_t mm_result;
  uintptr_t stack_base;
  mm_result = mm_allocate_pstack(root_table, &stack_base);

  if (mm_result != MM_SUCCESS) {
    vma_free(new_process);
    return -1;
  }

  thread_t *main_thread =
      pthread_init("main", (void *)stack_base, user_main, arg);

  if (!main_thread) {
    vma_free(new_process);
    return -1;
  }

  main_thread->user_stack_ptr -= 9 * sizeof(uint64_t);
  struct scheduler_frame_s* frame = (void*)main_thread->kernel_stack_ptr;

  frame->rsp = main_thread->user_stack_ptr;

  // vma_heap and vma_mmap initialization
  // add guard page for heap and mmap

  if (!new_process->vma_tail) {
    vma_free(new_process);
    return -1;
  }

  const size_t page_size = mm_get_page_size();
  uintptr_t heap_start = new_process->vma_tail->vm_end;

  heap_start = page_align_up(heap_start, page_size);
  heap_start += page_size; // Skip the guard page

  vma_t vma = {
      .vm_start = heap_start,
      .vm_end = heap_start,
      .flags = VMA_HEAP | VMA_ANONYMOUS | VMA_READ | VMA_WRITE | VMA_EXEC,
      .file = NULL,
      .file_offset = 0,
      .file_size = 0,
  };

  vma_t *heap_vma = vma_add(new_process, &vma);

  if (!heap_vma) {
    vma_free(new_process);
    return -1;
  }

  new_process->brk = heap_vma->vm_start;
  new_process->heap_vma = heap_vma;

  uintptr_t mmap_start = mm_get_user_mmap_base();
  mmap_start = page_align_down(mmap_start, page_size);
  mmap_start -= page_size; // Skip the guard page

  vma.vm_start = mmap_start;
  vma.vm_end = mmap_start + page_size;
  vma.flags = VMA_GUARD; // No specific flags for mmap
  vma.file = NULL;
  vma.file_offset = 0;
  vma.file_size = 0;

  vma_t *mmap_vma = vma_add(new_process, &vma);

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

void send_signal(thread_t *target, int signal_num) {
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

void clone_page_table(process_t *parent, process_t *child) {
  void *parent_root_table = phys_to_virt((void *)parent->page_table);
  void *child_root_table = phys_to_virt((void *)child->page_table);

  for (vma_t *current_vma = parent->vma_head; current_vma != NULL;
       current_vma = current_vma->next) {
    uintptr_t start = current_vma->vm_start;
    uintptr_t end = current_vma->vm_end;
    const size_t page_size = mm_get_page_size();

    if (current_vma->flags & VMA_GUARD || current_vma->flags == 0) {
      continue;
    }

    for (uintptr_t addr = start; addr < end; addr += page_size) {
      uintptr_t phys_addr = 0;
      mm_result_t result =
          get_mapping(parent_root_table, (void *)addr, &phys_addr);

      if (result != MM_SUCCESS || phys_addr == 0) {
        continue;
      }

      mm_flags_t flags = vma_get_flags(current_vma->flags) | MM_FLAG_USER;

      if (current_vma->flags & VMA_WRITE) {
        flags |= MM_FLAG_COW;
        flags &= ~MM_FLAG_WRITABLE;
      }

      result = change_page_flags(parent_root_table, (void *)addr, flags);

      if (result != MM_SUCCESS) {
        continue;
      }

      result =
          map_page(child_root_table, (void *)addr, (void *)phys_addr, flags);

      if (result != MM_SUCCESS) {
        continue;
      }

      pmm_increment_page_refcount(&mm_state.pmm_state, (void *)phys_addr);
    }
  }
}

thread_t *clone_thread(thread_t *parent_thread, syscall_frame_t *sys_frame) {
  const char *name = parent_thread->name;
  const uintptr_t user_stack_base = parent_thread->user_stack_base;
  void (*fake_entry_point)(void *) = (void (*)(void *))0x01;

  thread_t *child_thread =
      pthread_init(name, (void *)user_stack_base, fake_entry_point, NULL);

  if (!child_thread) {
    return NULL;
  }

  cpu_core_t *core = get_current_cpu();

  child_thread->user_stack_base = parent_thread->user_stack_base;
  child_thread->user_stack_ptr = core->user_sp;
  child_thread->signal_mask = parent_thread->signal_mask;
  child_thread->pending_signals = parent_thread->pending_signals;

  // Copy the register state from the parent thread to the child thread
  struct scheduler_frame_s *frame = (void *)child_thread->kernel_stack_ptr;

  // Copy general-purpose registers
  memcpy(&frame->regs, &sys_frame, sizeof(syscall_frame_t));

  frame->rip = sys_frame->rcx;
  frame->rsp = core->user_sp;
  frame->rflags = sys_frame->r11;
  frame->regs.rax = 0; // Child process returns 0 from fork

  return child_thread;
}

void sys_fork(syscall_frame_t *frame) {
  process_t *parent_process = scheduler_get_current_process();
  process_t *child_process = uprocess_init("clone", parent_process);

  if (!child_process) {
    frame->syscall_num = -1;
    return;
  }

  // Copy the VMA list from the parent process to the child process
  vma_t *current_vma = parent_process->vma_head;

  while (current_vma) {
    vma_t new_vma = *current_vma;
    vma_t *child_vma = vma_add(child_process, &new_vma);

    if (!child_vma) {
      vma_free(child_process);
      frame->syscall_num = -1;
      return;
    }

    if (current_vma == parent_process->heap_vma) {
      child_process->heap_vma = child_vma;
    } else if (current_vma == parent_process->mmap_vma) {
      child_process->mmap_vma = child_vma;
    }

    current_vma = current_vma->next;
  }

  child_process->brk = parent_process->brk;
  child_process->active_threads = child_process->active_threads;

  mm_result_t result = mm_create_page_table(&child_process->page_table);

  if (result != MM_SUCCESS) {
    vma_free(child_process);
    frame->syscall_num = -1;
    return;
  }

  clone_page_table(parent_process, child_process);
  thread_t *child_thread =
      clone_thread(parent_process->thread_list_head, frame);

  if (!child_thread) {
    vma_free(child_process);
    frame->syscall_num = -1;
    return;
  }

  scheduler_add_process(child_process);
  scheduler_add_thread(child_process, child_thread);
  scheduler_queue_push(&scheduler_state.ready_queue, child_thread, TASK_READY);

  frame->syscall_num = (int)child_process->pid;
}

process_t *process_get_dead_child(process_t *parent) {
  process_t *child_process = parent->children;

  while (child_process) {
    if (child_process->active_threads == 0) {
      return child_process;
    }
    child_process = child_process->next;
  }

  return NULL;
}

void sys_waitpid(syscall_frame_t *frame) {
  int requested_pid = (int)frame->arg1;
  int *status = (int *)frame->arg2;
  int options = frame->arg3;

  if (!status) {
    mm_result_t result = mm_verify_process_addr(status);

    if (result != MM_SUCCESS) {
      frame->syscall_num = -1;
      return;
    }
  }

  thread_t *current_thread = scheduler_get_current_thread();
  process_t *current_process = current_thread->parent_process;

  if (requested_pid > 0) {
    while (1) {
      unsigned long pflags;
      spinlock_acquire(&current_process->lock, &pflags);

      process_t *child_process =
          process_get_child_by_pid(current_process, requested_pid);

      if (!child_process) {
        frame->syscall_num = -1;
        spinlock_release(&current_process->lock, pflags);
        return;
      }

      unsigned long cflags;
      spinlock_acquire(&child_process->lock, &cflags);

      if (child_process->active_threads == 0) {
        int exit_code = child_process->exit_code & 0xFF;
        int pid = child_process->pid;

        if (status) {
          *status = exit_code << 8;
        }

        frame->syscall_num = pid;
        scheduler_remove_process(child_process);
        process_remove_child(current_process, child_process);
        cleaner_add_process(child_process);

        spinlock_release(&child_process->lock, cflags);
        spinlock_release(&current_process->lock, pflags);
        return;
      }

      if (options & WNOHANG) {
        frame->syscall_num = 0;

        spinlock_release(&child_process->lock, cflags);
        spinlock_release(&current_process->lock, pflags);
        return;
      }

      scheduler_queue_push(&child_process->child_waiters, current_thread,
                           TASK_BLOCKED);
      spinlock_release(&child_process->lock, cflags);
      spinlock_release(&current_process->lock, pflags);

      scheduler_yield();
    }
  }

  if (requested_pid == -1) {
    while (1) {
      unsigned long pflags;
      spinlock_acquire(&current_process->lock, &pflags);

      if (!current_process->children) {
        frame->syscall_num = -1;
        spinlock_release(&current_process->lock, pflags);
        return;
      }

      process_t *dead_child = process_get_dead_child(current_process);

      unsigned long cflags;
      spinlock_acquire(&dead_child->lock, &cflags);

      if (dead_child) {
        int exit_code = dead_child->exit_code & 0xFF;
        int pid = dead_child->pid;

        if (status) {
          *status = exit_code << 8;
        }

        frame->syscall_num = pid;

        scheduler_remove_process(dead_child);
        process_remove_child(current_process, dead_child);
        cleaner_add_process(dead_child);

        spinlock_release(&dead_child->lock, cflags);
        spinlock_release(&current_process->lock, pflags);
        return;
      }

      if (options & WNOHANG) {
        frame->syscall_num = 0;
        spinlock_release(&dead_child->lock, cflags);
        spinlock_release(&current_process->lock, pflags);
        return;
      }

      scheduler_queue_push(&current_process->waiting_threads, current_thread,
                           TASK_BLOCKED);
      spinlock_release(&dead_child->lock, cflags);
      spinlock_release(&current_process->lock, pflags);
      scheduler_yield();
    }
  }

  if (requested_pid == 0) {
    frame->syscall_num = -1;
    return;
  }

  if (requested_pid < -1) {
    frame->syscall_num = -1;
    return;
  }
}

static char *push_string(uint64_t *rsp, const char *str) {
  size_t len = strlen(str) + 1; // Include null terminator
  *rsp -= len;

  // Copy the string to the user stack
  memcpy((void *)*rsp, str, len);

  return (char *)*rsp;
}

static uint64_t setup_user_stack(uint64_t user_stack_base,
                                 char *const user_argv[],
                                 char *const user_envp[]) {
  uint64_t rsp = user_stack_base;

  // Count arguments and environment variables
  int argc = 0, envc = 0;
  while (user_argv[argc])
    argc++;
  while (user_envp[envc])
    envc++;

  // Arrays to hold the new virtual addresses of the strings
  uint64_t argv_ptrs[argc];
  uint64_t envp_ptrs[envc];

  // Push all strings onto the stack (from highest address down)
  for (int i = envc - 1; i >= 0; i--) {
    envp_ptrs[i] = (uint64_t)push_string(&rsp, user_envp[i]);
  }

  for (int i = argc - 1; i >= 0; i--) {
    argv_ptrs[i] = (uint64_t)push_string(&rsp, user_argv[i]);
  }

  // Align the stack pointer down to a 16-byte boundary
  // The System V ABI requires RSP to be 16-byte aligned before calling main
  rsp &= ~0xF;

  // Calculate how much space we need for the arrays
  // argc (1) + argv ptrs (argc) + NULL (1) + envp ptrs (envc) + NULL (1) + auxv
  // (3)
  size_t pointer_array_size =
      (1 + argc + 1 + envc + 1 + (3 * 2)) * sizeof(uint64_t);

  // Move RSP down to allocate space for all pointers
  rsp -= pointer_array_size;

  // Fill the arrays from bottom to top using a temporary pointer
  uint64_t *stack_ptr = (uint64_t *)rsp;

  // Push argc
  *stack_ptr++ = argc;

  // Push argv pointers
  for (int i = 0; i < argc; i++) {
    *stack_ptr++ = argv_ptrs[i];
  }
  *stack_ptr++ = 0; // NULL terminator for argv

  // Push envp pointers
  for (int i = 0; i < envc; i++) {
    *stack_ptr++ = envp_ptrs[i];
  }
  *stack_ptr++ = 0; // NULL terminator for envp

  // Push Auxiliary Vector
  *stack_ptr++ = 6; // AT_PAGESZ
  *stack_ptr++ = mm_get_page_size();

  *stack_ptr++ = 25;      // AT_RANDOM
  *stack_ptr++ = rsp + 8; // Point to some semi-random data on stack for now

  *stack_ptr++ = 0; // AT_NULL
  *stack_ptr++ = 0;

  return rsp;
}

void replace_current_process(process_t *new_process, void *entry_point,
                             char *const user_argv[], char *const user_envp[]) {
  process_t *target_process = scheduler_get_current_process();

  unsigned long rflags;
  spinlock_acquire(&target_process->lock, &rflags);

  thread_t *parent_thread = scheduler_get_current_thread();
  const size_t page_size = mm_get_page_size();

  // free threads of the target process, and their kernel stacks
  thread_t *current_thread = target_process->thread_list_head;

  while (current_thread) {
    if (current_thread == parent_thread) {
      current_thread = current_thread->next;
      continue;
    }

    uintptr_t kernel_stack_base = current_thread->kernel_stack_base;

    if (kernel_stack_base) {
      mm_free_kstack(kernel_stack_base);
    }

    thread_t *temp_thread = current_thread;
    current_thread = current_thread->next;

    kfree(temp_thread);
  }

  // copy user_args and user_envp, before freeing the vma structures
  vma_t *stack_vma = target_process->vma_tail;

  vma_detach(target_process, stack_vma);

  const size_t vma_size = stack_vma->vm_end - stack_vma->vm_start;
  const size_t num_pages = page_align_up(vma_size, page_size) / page_size;
  void *root_table = phys_to_virt((void *)target_process->page_table);

  for (size_t i = 0; i < num_pages; i++) {
    uintptr_t virt_addr = stack_vma->vm_start + i * page_size;
    uintptr_t phys_addr = 0;

    mm_result_t result = unmap_page(root_table, (void *)virt_addr, &phys_addr);

    if (result == MM_SUCCESS && phys_addr != 0) {
      pmm_free(&mm_state.pmm_state, (void *)phys_addr);
    }
  }

  void *phys_addr = pmm_alloc(&mm_state.pmm_state, page_size);

  if (!phys_addr) {
    spinlock_release(&target_process->lock, rflags);
    return;
  }

  // map single page of user stack for the current thread
  uintptr_t user_stack_base = stack_vma->vm_end - page_size;
  mm_flags_t flags = vma_get_flags(stack_vma->flags) | MM_FLAG_USER;
  mm_result_t result =
      map_page(root_table, (void *)user_stack_base, phys_addr, flags);

  if (result != MM_SUCCESS) {
    pmm_free(&mm_state.pmm_state, phys_addr);
    spinlock_release(&target_process->lock, rflags);
    return;
  }

  uintptr_t user_stack_ptr =
      setup_user_stack(stack_vma->vm_end, user_argv, user_envp);

  // Free the vma structures
  // user stack free with vma structure
  struct vma_s *current_vma = target_process->vma_head;

  while (current_vma) {
    if (current_vma->flags & VMA_NONE) {
      struct vma_s *temp_vma = current_vma;
      current_vma = current_vma->next;

      kfree(temp_vma);
      continue;
    }

    const size_t vma_size = current_vma->vm_end - current_vma->vm_start;
    const size_t num_pages = page_align_up(vma_size, page_size) / page_size;

    for (size_t i = 0; i < num_pages; i++) {
      uintptr_t virt_addr = current_vma->vm_start + i * page_size;
      uintptr_t phys_addr = 0;

      mm_result_t result =
          unmap_page(root_table, (void *)virt_addr, &phys_addr);

      if (result == MM_SUCCESS && phys_addr != 0) {
        pmm_free(&mm_state.pmm_state, (void *)phys_addr);
        continue;
      }
    }

    if (current_vma->file != NULL) {
      fs_close(current_vma->file);
    }

    struct vma_s *temp_vma = current_vma;
    current_vma = current_vma->next;

    // Free the vma structure
    kfree(temp_vma);
  }

  // copy vma from new_process to target_process
  target_process->vma_head = new_process->vma_head;
  target_process->vma_tail = new_process->vma_tail;
  target_process->heap_vma = new_process->heap_vma;
  target_process->mmap_vma = new_process->mmap_vma;

  // copy exe and cwd from new_process to target_process
  target_process->exe = new_process->exe;
  target_process->cwd = new_process->cwd;

  if (!vma_add(target_process, stack_vma)) {
    spinlock_release(&target_process->lock, rflags);
    return;
  }

  target_process->brk = new_process->brk;
  target_process->active_threads = 1;

  parent_thread->pending_signals = 0;
  parent_thread->signal_mask = 0;
  parent_thread->user_stack_base = stack_vma->vm_end;
  parent_thread->user_stack_ptr = user_stack_ptr;

  uintptr_t addr = parent_thread->kernel_stack_base;
  size_t frame_size = sizeof(struct scheduler_frame_s);
  parent_thread->kernel_stack_ptr = addr - frame_size;
  struct scheduler_frame_s *frame = (void *)parent_thread->kernel_stack_ptr;

  memset(frame, 0, sizeof(struct scheduler_frame_s));

  frame->rip = (uint64_t)entry_point;
  frame->rsp = (uint64_t)parent_thread->user_stack_ptr;
  frame->rflags = RFLAGS_IF;
  frame->cs = USER_CS_REGS;
  frame->ss = USER_SS_REGS;

  uintptr_t kernel_stack_ptr = parent_thread->kernel_stack_ptr;
  uintptr_t kernel_stack_base = parent_thread->kernel_stack_base;

  spinlock_release(&target_process->lock, rflags);
  switch_context(target_process->page_table, kernel_stack_ptr,
                 kernel_stack_base);
}

void sys_execve(syscall_frame_t *frame) {
  const char *path = (const char *)frame->arg1;
  char *const *argv = (char *const *)frame->arg2;
  char *const *envp = (char *const *)frame->arg3;

  void *entry_point;
  process_t temp_process = {0};

  int result = load_elf_file(&temp_process, path, &entry_point);

  if (result != 0) {
    vma_free(&temp_process);
    frame->syscall_num = -1;
    return;
  }

  vma_add_nullspace(&temp_process, 0x0, mm_get_user_virtual_base());

  // vma_heap and vma_mmap initialization
  const size_t page_size = mm_get_page_size();
  uintptr_t heap_start = temp_process.vma_tail->vm_end;

  heap_start = page_align_up(heap_start, page_size);
  heap_start += page_size; // Skip the guard page

  vma_t vma = {
      .vm_start = heap_start,
      .vm_end = heap_start,
      .flags = VMA_HEAP | VMA_ANONYMOUS | VMA_READ | VMA_WRITE | VMA_EXEC,
      .file = NULL,
      .file_offset = 0,
      .file_size = 0,
  };

  vma_t *heap_vma = vma_add(&temp_process, &vma);

  if (!heap_vma) {
    vma_free(&temp_process);
    frame->syscall_num = -1;
    log_error("Failed to add heap VMA for process: %s", path);
    return;
  }

  temp_process.brk = heap_vma->vm_start;
  temp_process.heap_vma = heap_vma;

  uintptr_t mmap_start = mm_get_user_mmap_base();
  mmap_start = page_align_down(mmap_start, page_size);
  mmap_start -= page_size; // Skip the guard page

  vma.vm_start = mmap_start;
  vma.vm_end = mmap_start + page_size;
  vma.flags = VMA_GUARD; // No specific flags for mmap
  vma.file = NULL;
  vma.file_offset = 0;
  vma.file_size = 0;

  vma_t *mmap_vma = vma_add(&temp_process, &vma);

  if (!mmap_vma) {
    vma_free(&temp_process);
    frame->syscall_num = -1;
    log_error("Failed to add mmap VMA for process: %s", path);
    return;
  }

  temp_process.mmap_vma = mmap_vma;

  // user stack initialization not needed,
  // it use the current thread's user stack
  replace_current_process(&temp_process, entry_point, argv, envp);
  frame->syscall_num = -1;
  return;
}

void process_add_child(process_t *parent, process_t *child) {
  child->parent = parent;
  child->sibling_prev = NULL;
  child->sibling_next = parent->children;

  if (parent->children) {
    parent->children->sibling_prev = child;
  }

  parent->children = child;
}

void process_remove_child(process_t *parent, process_t *child) {
  if (child->sibling_prev) {
    child->sibling_prev->sibling_next = child->sibling_next;
  } else {
    parent->children = child->sibling_next;
  }

  if (child->sibling_next) {
    child->sibling_next->sibling_prev = child->sibling_prev;
  }

  child->parent = NULL;
  child->sibling_prev = NULL;
  child->sibling_next = NULL;
}

process_t *process_get_child_by_pid(process_t *parent, size_t pid) {
  process_t *current_child = parent->children;

  while (current_child) {
    if (current_child->pid == pid) {
      return current_child;
    }
    current_child = current_child->sibling_next;
  }

  return NULL; // Child with the specified PID not found
}

void send_signal_to_process(process_t* proc, int signal_num) {
  unsigned long flags;
  spinlock_acquire(&proc->lock, &flags);

  thread_t* current = proc->thread_list_head;
  
  while (current != NULL) {
    if ((current->signal_mask & (1 << signal_num)) == 0) {
      send_signal(current, signal_num);          
    }
    current = current->next;
  }

  spinlock_release(&proc->lock, flags);
}

void terminate_process(process_t *process) {
  send_signal_to_process(process, SIGKILL);
  scheduler_yield();
}

void terminate_current_process(void) {
  thread_t *current_thread = scheduler_get_current_thread();
  process_t *current_process = current_thread->parent_process;
  terminate_process(current_process);
}