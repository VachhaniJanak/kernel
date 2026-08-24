#include <arch/x86_64/apic.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/timer.h>
#include <arch/x86_64/tss.h>
#include <drivers/screen/screen.h>
#include <kernel.h>
#include <mm/mm.h>
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
#include <syscall/syscall.h>
#include <utils/log.h>
#include <utils/utils.h>
#include <vfs/vfs.h>

#include "elf.h"

// #define SCHEDULER_DEBUG

spinlock_t scheduler_state_lock = {0};

static struct scheduler_state_s scheduler_state = {0};
static volatile size_t system_tick = 0;
static cpu_local_data_t bsp_local_data = {0};

size_t get_system_time(void) { return system_tick; }

process_t* scheduler_get_current_process(void) {
  return scheduler_state.current_process;
}

process_t* scheduler_get_kernel_process(void) {
  return scheduler_state.kernel_process;
}

thread_t* scheduler_get_current_thread(void) {
  return scheduler_state.current_thread;
}

thread_t* scheduler_get_idle_thread(void) {
  return scheduler_state.idle_thread;
}

size_t scheduler_get_total_processes(void) {
  return scheduler_state.total_processes;
}

size_t scheduler_get_total_threads(void) {
  return scheduler_state.total_threads;
}

void scheduler_yield(void) { __asm__ volatile("int $32"); }

process_t* scheduler_add_process(void) {
  if (scheduler_state.process_list_start == NULL) {
    process_t* process = (process_t*)kmalloc(sizeof(process_t));

    if (process == NULL) {
      return NULL;  // Memory allocation failed
    }

    process->pid = scheduler_state.next_pid++;
    process->next = NULL;

    scheduler_state.process_list_start = process;
    scheduler_state.process_list_end = process;

    scheduler_state.total_processes++;
    return process;
  }

  process_t* current = scheduler_state.process_list_end;
  current->next = (process_t*)kmalloc(sizeof(process_t));

  if (current->next == NULL) {
    return NULL;  // Memory allocation failed
  }

  current->next->pid = scheduler_state.next_pid++;
  current->next->next = NULL;

  scheduler_state.process_list_end = current->next;
  scheduler_state.total_processes++;

  return scheduler_state.process_list_end;
}

bool scheduler_remove_process(size_t pid) {
  if (scheduler_state.process_list_start == NULL) {
    return false;
  }

  if (scheduler_state.process_list_start->pid == pid) {
    process_t* to_remove = scheduler_state.process_list_start;
    scheduler_state.process_list_start = to_remove->next;

    if (scheduler_state.process_list_start == NULL) {
      scheduler_state.process_list_end = NULL;
    }

    kfree(to_remove);
    scheduler_state.total_processes--;
    return true;
  }

  process_t* current = scheduler_state.process_list_start;
  while (current->next != NULL) {
    if (current->next->pid == pid) {
      process_t* to_remove = current->next;

      if (to_remove == scheduler_state.process_list_end) {
        scheduler_state.process_list_end = current;
      }

      current->next = to_remove->next;
      kfree(to_remove);
      scheduler_state.total_processes--;
      return true;
    }

    current = current->next;
  }

  return false;
}

process_t* scheduler_get_process_by_pid(size_t pid) {
  if (scheduler_state.process_list_end != NULL &&
      scheduler_state.process_list_end->pid == pid) {
    return scheduler_state.process_list_end;
  }

  process_t* current = scheduler_state.process_list_start;

  while (current != NULL) {
    if (current->pid == pid) {
      return current;
    }
    current = current->next;
  }

  return NULL;
}

thread_t* scheduler_add_thread(process_t* process) {
  if (process == NULL) {
    return NULL;
  }

  process_t* current_process = process;

  if (current_process->thread_list_start == NULL) {
    thread_t* thread = (thread_t*)kmalloc(sizeof(thread_t));

    if (thread == NULL) {
      return NULL;  // Memory allocation failed
    }

    thread->tid = scheduler_state.next_tid++;
    thread->next = NULL;

    current_process->thread_list_start = thread;
    current_process->thread_list_end = thread;

    scheduler_state.total_threads++;
    return thread;
  }

  thread_t* current_thread = current_process->thread_list_end;
  thread_t* new_thread = (thread_t*)kmalloc(sizeof(thread_t));

  if (new_thread == NULL) {
    return NULL;  // Memory allocation failed
  }

  current_thread->next = new_thread;

  new_thread->tid = scheduler_state.next_tid++;
  new_thread->next = NULL;

  current_process->thread_list_end = new_thread;

  scheduler_state.total_threads++;
  return new_thread;
}

bool scheduler_remove_thread(process_t* process, size_t tid) {
  if (process == NULL) {
    return false;
  }

  process_t* current_process = process;

  if (current_process->thread_list_start == NULL) {
    return false;
  }

  if (current_process->thread_list_start->tid == tid) {
    thread_t* to_remove = current_process->thread_list_start;
    current_process->thread_list_start = to_remove->next;

    if (current_process->thread_list_start == NULL) {
      current_process->thread_list_end = NULL;
    }

    kfree(to_remove);
    scheduler_state.total_threads--;
    return true;
  }

  thread_t* current_thread = current_process->thread_list_start;

  while (current_thread->next != NULL) {
    if (current_thread->next->tid == tid) {
      thread_t* to_remove = current_thread->next;

      if (to_remove == current_process->thread_list_end) {
        current_process->thread_list_end = current_thread;
      }

      current_thread->next = to_remove->next;
      kfree(to_remove);
      scheduler_state.total_threads--;
      return true;
    }

    current_thread = current_thread->next;
  }

  return false;
}

thread_t* scheduler_get_thread(process_t* process, size_t tid) {
  if (process == NULL) {
    return NULL;
  }

  process_t* current_process = process;
  thread_t* current_thread = current_process->thread_list_start;

  while (current_thread != NULL) {
    if (current_thread->tid == tid) {
      return current_thread;
    }
    current_thread = current_thread->next;
  }

  return NULL;
}

static inline bool get_next_thread(process_t** nprocess, thread_t** nthread,
                                   process_t* cprocess, thread_t* cthread) {
  thread_t* current_thread = cthread;

  if (cprocess == NULL || cthread == NULL) {
    return false;
  }

  // If the current thread is the last thread in the current process, move to
  // the next process
  if (current_thread == cprocess->thread_list_end) {
    process_t* current_process = cprocess;

    if (current_process == scheduler_state.process_list_end) {
      *nprocess = scheduler_state.process_list_start;
      *nthread = (*nprocess)->thread_list_start;
      return true;
    }

    *nprocess = current_process->next;
    *nthread = (*nprocess)->thread_list_start;
    return true;
  }

  *nprocess = cprocess;
  *nthread = current_thread->next;
  return true;
}

static inline void scheduler(struct scheduler_frame_s* frame,
                             context_switch_t* context_switch) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* current_process = scheduler_state.current_process;
  thread_t* current_thread = scheduler_state.current_thread;

  if (current_process == NULL || scheduler_state.total_threads == 0) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    context_switch->rpt = (uint64_t)get_page_table_addr();
    context_switch->stack = (uint64_t)frame;
    return;
  }

  process_t* next_process = NULL;
  thread_t* next_thread = NULL;

  size_t attempts = 0;
  process_t* temp_process = current_process;
  thread_t* temp_thread = current_thread;

  // Loop through all threads to find the next READY or SLEEPING thread
  while (attempts < scheduler_state.total_threads) {
    if (!get_next_thread(&next_process, &next_thread, temp_process,
                         temp_thread)) {
      SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
      context_switch->rpt = (uint64_t)get_page_table_addr();
      context_switch->stack = (uint64_t)frame;
      return;
    }

    if (next_thread->status == THREAD_READY) {
      next_thread->status = THREAD_RUNNING;
      break;
    }

    if (next_thread->status == THREAD_SLEEPING &&
        next_thread->wakeup_time <= get_system_time()) {
      next_thread->wakeup_time = 0;
      next_thread->status = THREAD_RUNNING;
      break;
    }

    temp_process = next_process;
    temp_thread = next_thread;
    attempts++;
  }

  // If no READY or SLEEPING thread was found after checking all threads, switch
  // to the idle thread
  if (attempts >= scheduler_state.total_threads) {
    next_process = scheduler_state.kernel_process;
    next_thread = scheduler_state.idle_thread;
    next_thread->status = THREAD_RUNNING;
  }

  // save the current thread's stack pointer
  if (current_thread->status == THREAD_RUNNING) {
    current_thread->status = THREAD_READY;
  }

  current_thread->current_stack_ptr = (void*)frame;

  // switch to the next thread
  scheduler_state.current_process = next_process;
  scheduler_state.current_thread = next_thread;

  bsp_local_data.kernel_rsp = (uint64_t)next_thread->kernel_stack;
  set_tss_ring_x_stack(next_thread->kernel_stack, 0);

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

#ifdef SCHEDULER_DEBUG
  log_print("[SCHEDULER] Frame:");
  log_print("{thread %zu -> thread %zu}\n", current_thread->tid,
            next_thread->tid);
  log_print("  Current thread: %zu\n", current_thread->tid);
  log_print("    RIP: 0x%lx\n", frame->rip);
  log_print("    RSP: 0x%lx\n", frame->rsp);
  log_print("    RFLAGS: 0x%lx\n", frame->rflags);
  log_print("    CS: 0x%lx\n", frame->cs);
  log_print("    SS: 0x%lx\n", frame->ss);
  log_print("    Current RSP: 0x%lx\n", (uint64_t)frame);
  log_newline();
#endif
  context_switch->rpt = (uint64_t)next_process->page_table;
  context_switch->stack = (uint64_t)next_thread->current_stack_ptr;
}

uint64_t timer_irq_isr_handler(struct scheduler_frame_s* frame,
                               context_switch_t* context_switch) {
  system_tick += 10;
  scheduler(frame, context_switch);
  lapic_eoi();
  return 0;
}

static void idle(void* arg) {
  UNUSED(arg);

  while (true) {
    __asm__ volatile("hlt");
  }
}

void test_thread1(void* arg) {
  UNUSED(arg);

  static size_t counter = 0;

  while (true) {
    log_print("Thread 1: Counter = %zu\n", counter++);
    kthread_sleep(1000);
  }
}

void test_thread2(void* arg) {
  UNUSED(arg);

  static size_t counter = 0;

  while (true) {
    log_print("Thread 2: Counter = %zu\n", counter++);
    kthread_sleep(5000);
  }
}

void foo(void) {
  vfs_list_directory("/userprograms/");
  int ret = load_user_process(NULL, "/userprograms/demo.elf", NULL);

  if (ret != 0) {
    log_error("Failed to load user process: %d", ret);
    return;
  }

  return;
}

void scheduler_init(void) {
  syscall_init(&bsp_local_data);
  spinlock_init(&scheduler_state_lock);

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  // Initialize the scheduler state
  scheduler_state.process_list_start = NULL;
  scheduler_state.process_list_end = NULL;
  scheduler_state.kernel_process = NULL;
  scheduler_state.current_process = NULL;
  scheduler_state.current_thread = NULL;

  scheduler_state.total_processes = 0;
  scheduler_state.total_threads = 0;
  scheduler_state.next_pid = 1;
  scheduler_state.next_tid = 1;

  // Add kernel process
  process_t* kernel_process = scheduler_add_process();
  kprocess_init(kernel_process);

  scheduler_state.kernel_process = kernel_process;
  scheduler_state.current_process = kernel_process;

  // Create the idle thread
  thread_t* idle_thread = scheduler_add_thread(kernel_process);

  if (idle_thread == NULL) {
    log_error("Failed to create idle thread");
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return;
  }

  kstrcpy(idle_thread->name, "idle");
  idle_thread->status = THREAD_READY;
  idle_thread->kernel_stack = (void*)KERNEL_STACK_BASE;
  idle_thread->current_stack_ptr = 0;

  scheduler_state.idle_thread = idle_thread;
  scheduler_state.current_thread = scheduler_state.idle_thread;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  // Create test threads
  thread_t* thread1;
  kthread_create("test_thread1", &thread1, test_thread1, NULL);

  thread_t* thread2;
  kthread_create("test_thread2", &thread2, test_thread2, NULL);

  foo();

#ifdef SCHEDULER_DEBUG
  log_info("Scheduler initialized");
#endif

  // switch to the first thread
  idle(NULL);
}
