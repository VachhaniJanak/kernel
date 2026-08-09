#include <arch/x86_64/apic.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/timer.h>
#include <drivers/screen/screen.h>
#include <kernel.h>
#include <mm/vmm/kheap.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <scheduler/locks.h>
#include <scheduler/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

#define KERNEL_CS_REGS 0x8
#define KERNEL_SS_REGS 0x10
#define USER_CS_REGS 0x1B
#define USER_SS_REGS 0x23
#define RFLAGS_IF 0x202

static struct scheduler_state_s scheduler_state = {0};
static spinlock_t scheduler_state_lock = {0};
static volatile size_t system_tick = 0;

static inline size_t get_system_time(void) { return system_tick; }

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

process_t* scheduler_get_process_list_start(void) {
  return scheduler_state.process_list_start;
}

process_t* scheduler_get_process_list_end(void) {
  return scheduler_state.process_list_end;
}

size_t scheduler_get_total_processes(void) {
  return scheduler_state.total_processes;
}

size_t scheduler_get_total_threads(void) {
  return scheduler_state.total_threads;
}

void scheduler_yield(void) { __asm__ volatile("int $32"); }

static process_t* add_process(void) {
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

static bool remove_process(size_t pid) {
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

static process_t* get_process_by_pid(size_t pid) {
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

static thread_t* add_thread(process_t* process) {
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
    thread->status = THREAD_NOTREADY;
    thread->stack_ptr = NULL;
    thread->current_stack_ptr = NULL;
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
  new_thread->status = THREAD_NOTREADY;
  new_thread->stack_ptr = NULL;
  new_thread->current_stack_ptr = NULL;
  new_thread->next = NULL;

  current_process->thread_list_end = new_thread;

  scheduler_state.total_threads++;
  return new_thread;
}

bool remove_thread(process_t* process, size_t tid) {
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

thread_t* get_thread(process_t* process, size_t tid) {
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

void init_kernel_process(process_t* process) {
  if (process == NULL) {
    return;
  }

  kstrcpy(process->name, "kernel");
  process->page_table = (void*)get_page_table_addr();
  process->thread_list_start = NULL;
  process->thread_list_end = NULL;
}

static void thread_exit(thread_t* thread) {
  thread->status = THREAD_DEAD;
  while (true);
}

static void thread_execution_wrapper(void (*function)(void*), void* arg,
                                     thread_t* thread) {
  function(arg);
  thread_exit(thread);
}

bool init_kernel_thread(const char* name, thread_t* thread, size_t stack_size,
                        void (*entry_point)(void*), void* arg) {
  if (thread == NULL) {
    return false;
  }

  kstrcpy(thread->name, name);
  thread->status = THREAD_READY;

  // Allocate stack for the thread
  uint64_t flags = MMU_PRESENT | MMU_WRITABLE;
  thread->stack_ptr = vmalloc(stack_size, flags, false);

  if (thread->stack_ptr == NULL) {
    return false;
  }

  // Stack grows downwards
  size_t stack_top = (size_t)thread->stack_ptr + stack_size;
  thread->stack_ptr = (void*)stack_top;

  size_t frame_size = sizeof(struct scheduler_frame_s);
  thread->current_stack_ptr = (void*)(stack_top - frame_size);

  // Set up the initial stack frame for the thread
  struct scheduler_frame_s* frame = thread->current_stack_ptr;

  frame->regs.rdi = (uint64_t)entry_point;
  frame->regs.rsi = (uint64_t)arg;
  frame->regs.rdx = (uint64_t)thread;
  frame->rip = (uint64_t)thread_execution_wrapper;
  frame->rsp = (uint64_t)thread->stack_ptr;
  frame->regs.rbp = (uint64_t)thread->stack_ptr;
  frame->rflags = RFLAGS_IF;
  frame->cs = KERNEL_CS_REGS;
  frame->ss = KERNEL_SS_REGS;

  return true;
}

int create_kthread(const char* name, thread_t** thread, size_t stack_size,
                   void (*entry_point)(void*), void* arg) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* kernel_process = scheduler_state.kernel_process;

  if (kernel_process == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return -1;
  }

  thread_t* new_thread = add_thread(kernel_process);

  if (new_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return -1;
  }

  if (!init_kernel_thread(name, new_thread, stack_size, entry_point, arg)) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return -1;
  }

  *thread = new_thread;
  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
  return new_thread->tid;
}

int join_kthread(thread_t* thread) {
  if (thread == NULL) {
    return -1;
  }

  while (thread->status != THREAD_DEAD) {
    // Busy wait until the thread exits
  }

  return 0;
}

void kthread_sleep(size_t milliseconds) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  scheduler_state.current_thread->status = THREAD_SLEEPING;
  scheduler_state.current_thread->wakeup_time =
      get_system_time() + milliseconds;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  scheduler_yield();
}

static inline bool get_next_thread(process_t** nprocess, thread_t** nthread,
                                   process_t* cprocess, thread_t* cthread) {
  thread_t* current_thread = cthread;

  if (cprocess == NULL || current_thread == NULL) {
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

static inline uint64_t scheduler(struct scheduler_frame_s* frame) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* current_process = scheduler_state.current_process;
  thread_t* current_thread = scheduler_state.current_thread;

  if (current_process == NULL || scheduler_state.total_threads == 0) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return (uint64_t)frame;
  }

  process_t* next_process = NULL;
  thread_t* next_thread = NULL;
  size_t attempts = 0;

  while (attempts < scheduler_state.total_threads) {
    process_t* temp_process = current_process;
    thread_t* temp_thread = current_thread;

    if (!get_next_thread(&next_process, &next_thread, temp_process,
                         temp_thread)) {
      SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
      return (uint64_t)frame;
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

  set_page_table_addr((uint64_t)next_process->page_table);
  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
  return (uint64_t)next_thread->current_stack_ptr;
}

uint64_t timer_irq_isr_handler(struct scheduler_frame_s* frame) {
  lapic_eoi();

  system_tick += 10;

  return scheduler(frame);
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

void init_scheduler(void) {
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
  process_t* kernel_process = add_process();
  init_kernel_process(kernel_process);

  scheduler_state.kernel_process = kernel_process;
  scheduler_state.current_process = kernel_process;

  // Create the idle thread
  thread_t* idle_thread = add_thread(kernel_process);

  if (idle_thread == NULL) {
    log_error("Failed to create idle thread");
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return;
  }

  kstrcpy(idle_thread->name, "idle");
  idle_thread->status = THREAD_READY;
  idle_thread->stack_ptr = (void*)KERNEL_STACK_BASE;
  idle_thread->current_stack_ptr = 0;

  scheduler_state.idle_thread = idle_thread;
  scheduler_state.current_thread = scheduler_state.idle_thread;

#ifdef SCHEDULER_DEBUG
  log_info("Scheduler initialized");
#endif

  // Create test threads
  thread_t* thread1;
  create_kthread("test_thread1", &thread1, 1024, test_thread1, NULL);

  thread_t* thread2;
  create_kthread("test_thread2", &thread2, 1024, test_thread2, NULL);

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  // switch to the first thread
  idle(NULL);
}
