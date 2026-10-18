#include <fs/fs.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <process/cleaner.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>

#include "vma.h"

extern struct mm_state_s mm_state;

static struct reaper_state_s reaper_state = {0};

static void queue_push(struct reaper_state_s *queue, process_t *process) {
  if (!queue || !process) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&queue->lock, &flags);

  process->next = NULL;
  process->prev = queue->tail;

  if (queue->tail) {
    queue->tail->next = process;
  } else {
    queue->head = process;
  }

  queue->tail = process;
  queue->count++;

  spinlock_release(&queue->lock, flags);
}

static process_t *queue_pop(struct reaper_state_s *queue) {
  if (!queue) {
    return NULL;
  }

  unsigned long flags;
  spinlock_acquire(&queue->lock, &flags);

  if (!queue->head) {
    spinlock_release(&queue->lock, flags);
    return NULL;
  }

  process_t *process = queue->head;
  queue->head = process->next;

  if (queue->head) {
    queue->head->prev = NULL;
  } else {
    queue->tail = NULL;
  }

  process->prev = NULL;
  process->next = NULL;

  queue->count--;
  spinlock_release(&queue->lock, flags);

  return process;
}

static inline void reaper_cleanup_process(process_t *process) {
  process_remove_child(process->parent, process);

  thread_t *current_thread = process->thread_list_head;

  // Free the kernel stack of the thread
  while (current_thread) {
    uintptr_t kernel_stack_base = current_thread->kernel_stack_base;

    if (kernel_stack_base) {
      mm_free_kstack(kernel_stack_base);
    }

    thread_t *temp_thread = current_thread;
    current_thread = current_thread->next;

    kfree(temp_thread);
  }

  // free process struct
  kfree(process);
}

// Reaper thread function to clean up terminated processes
static void reaper_thread(void *arg) {
  UNUSED(arg);

  while (1) {
    process_t *target = queue_pop(&reaper_state);

    if (target) {
      reaper_cleanup_process(target);
      continue;
    }

    waitqueue_wait(&reaper_state.wait_queue);
  }
}

void cleaner_init(void) {
  spinlock_init(&reaper_state.lock);
  waitqueue_init(&reaper_state.wait_queue);

  reaper_state.head = NULL;
  reaper_state.tail = NULL;

  int result = kthread_create("reaper", NULL, reaper_thread, NULL);

  if (result < 0) {
    log_error("Failed to create reaper thread");
  }
}

void cleaner_add_process(process_t *process) {
  queue_push(&reaper_state, process);
  waitqueue_wake(&reaper_state.wait_queue);
}

static void process_close_files(process_t *process) {
  if (!process->open_files) {
    return;
  }

  for (size_t i = 0; i < MAX_SYSTEM_OPEN_FILES; i++) {
    if (!process->open_files[i]) {
      continue;
    }

    fs_close(process->open_files[i]);
    process->open_files[i] = NULL;
  }

  kfree(process->open_files);
  process->open_files = NULL;
}

static void process_release_address_space(process_t *process) {
  const size_t page_size = mm_get_page_size();
  uintptr_t root_table = (uintptr_t)phys_to_virt((void *)process->page_table);

  struct vma_s *current_vma = process->vma_head;

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
          unmap_page((void *)root_table, (void *)virt_addr, &phys_addr);

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

  // Free the page table
  if (process->page_table) {
    pmm_free(&mm_state.pmm_state, (void *)process->page_table);
  }
}

void reaper_release_resources(process_t *process) {
  unsigned long flags;
  spinlock_acquire(&process->lock, &flags);

  process->active_threads--;

  if (process->active_threads > 0) {
    spinlock_release(&process->lock, flags);
    return;
  }

  process_close_files(process);
  process_release_address_space(process);

  // Wake up any threads waiting for this process to exit
  while (1) {
    thread_t *target_thread =
        scheduler_queue_pop(&process->wait_queue, TASK_READY);

    if (!target_thread) {
      break;
    }

    scheduler_queue_push(&scheduler_state.ready_queue, target_thread,
                         TASK_READY);
  }

  spinlock_release(&process->lock, flags);

  // add children processes to grandparent process
  process_t *child = process->children;

  while (child) {
    process_t *next_child = child->sibling_next;
    process_remove_child(process, child);
    process_add_child(process->parent, child);
    child = next_child;
  }
}
