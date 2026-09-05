#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <mm/vmm/kheap.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <process/cleanup.h>
#include <process/locks.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <vfs/vfs.h>

// #define CLEANUP_DEBUG

extern spinlock_t scheduler_state_lock;

static struct reaper_state_s reaper_state = {0};

static inline bool is_queue_empty(void);

static inline bool enqueue(process_t* process);

static inline process_t* dequeue(void);

static inline void reaper_cleanup_process(process_t* process) {
  if (process == NULL) {
    return;
  }

  // Perform cleanup operations for the process
  const size_t page_size = mm_get_page_size();
  uintptr_t root_table = (uintptr_t)phys_to_virt((void*)process->page_table);

  thread_t* current_thread = process->thread_list_start;

  while (current_thread != NULL) {
    // Free the kernel stack of the thread
    void* kernel_stack_base = current_thread->kernel_stack_base;

    if (kernel_stack_base != NULL) {
      mm_free_kstack((void*)root_table, (uintptr_t)kernel_stack_base);
    }

    thread_t* temp_thread = current_thread;
    current_thread = current_thread->next;

    // Free the thread structure
    kfree(temp_thread);
  }

  // Free the vma structures
  // user stack free with vma structure
  struct vma_s* current_vma = process->vma_head;

  while (current_vma != NULL) {
    if (current_vma->flags & VMA_NONE) {
      struct vma_s* temp_vma = current_vma;
      current_vma = current_vma->next;

      // Free the vma structure
      kfree(temp_vma);
      continue;
    }

    const size_t vma_size = current_vma->vm_end - current_vma->vm_start;
    const size_t num_pages = page_align_up(vma_size, page_size) / page_size;

    for (size_t i = 0; i < num_pages; i++) {
      uintptr_t virt_addr = current_vma->vm_start + i * page_size;
      uintptr_t phys_addr = 0;

      mm_result_t result =
          unmap_page((void*)root_table, (void*)virt_addr, &phys_addr);

      if (result == MM_SUCCESS && phys_addr != 0) {
        pmm_free((void*)phys_addr);
        continue;
      }
    }

    if (current_vma->file != NULL) {
      vfs_close(current_vma->file);
    }

    struct vma_s* temp_vma = current_vma;
    current_vma = current_vma->next;

    // Free the vma structure
    kfree(temp_vma);
  }

  // Free the page table
  if (process->page_table != NULL) {
    pmm_free((void*)process->page_table);
  }

#ifdef CLEANUP_DEBUG
  log_print("Cleaned up process with PID: %zu\n", process->pid);
  log_print("Process name: %s\n", process->name);
#endif

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  scheduler_remove_process(process->pid);

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
}

// Reaper thread function to clean up terminated processes
void process_reaper_thread(void* arg) {
  UNUSED(arg);

  while (1) {
    if (!is_queue_empty()) {
      process_t* process_to_cleanup = dequeue();
      reaper_cleanup_process(process_to_cleanup);
      continue;
    }

    // Sleep for a while before checking again
    kthread_sleep(256);
  }
}

void cleanup_init(void) {
  spinlock_init(&reaper_state.reaper_lock);
  reaper_state.head = NULL;
  reaper_state.tail = NULL;

  kthread_create("process_reaper", NULL, process_reaper_thread, NULL);
}

bool cleanup_add_process(process_t* process) {
  if (process == NULL) {
    return false;
  }

  return enqueue(process);
}

static inline bool is_queue_empty(void) { return reaper_state.head == NULL; }

bool enqueue(process_t* process) {
  struct reaper_node_s* new_node = NULL;
  new_node = (struct reaper_node_s*)kmalloc(sizeof(struct reaper_node_s));

  if (new_node == NULL) {
    return false;  // Memory allocation failed
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&reaper_state.reaper_lock, flags);

  new_node->process = process;
  new_node->next = NULL;

  if (is_queue_empty()) {
    reaper_state.head = new_node;
    reaper_state.tail = new_node;

    SPIN_LOCK_RELEASE(&reaper_state.reaper_lock, flags);
    return true;
  }

  (reaper_state.tail)->next = new_node;
  reaper_state.tail = new_node;

  SPIN_LOCK_RELEASE(&reaper_state.reaper_lock, flags);
  return true;
}

static inline process_t* dequeue(void) {
  if (is_queue_empty()) {
    return NULL;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&reaper_state.reaper_lock, flags);

  struct reaper_node_s* temp = reaper_state.head;
  process_t* process = temp->process;

  reaper_state.head = reaper_state.head->next;

  if (reaper_state.head == NULL) {
    reaper_state.tail = NULL;
  }

  SPIN_LOCK_RELEASE(&reaper_state.reaper_lock, flags);
  kfree(temp);
  return process;
}
