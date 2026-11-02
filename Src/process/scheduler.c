#include <arch/x86_64/apic.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/switch.h>
#include <arch/x86_64/timer.h>
#include <arch/x86_64/tss.h>
#include <fs/fs.h>
#include <kernel.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <process/cleaner.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall/syscall.h>
#include <tty/tty.h>
#include <utils/log.h>
#include <utils/panic.h>

struct scheduler_state_s scheduler_state = {0};

void scheduler_queue_push(thread_queue_t* queue, thread_t* thread,
                          task_status_t status) {
  if (!queue || !thread) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&queue->lock, &flags);

  thread->queue_next = NULL;
  thread->queue_prev = queue->tail;
  thread->status = status;
  thread->wait_queue = queue;

  if (queue->tail) {
    queue->tail->queue_next = thread;
  } else {
    queue->head = thread;
  }

  queue->tail = thread;
  queue->count++;

  spinlock_release(&queue->lock, flags);
}

thread_t* scheduler_queue_pop(thread_queue_t* queue, task_status_t status) {
  if (!queue) {
    return NULL;
  }

  unsigned long flags;
  spinlock_acquire(&queue->lock, &flags);

  if (!queue->head) {
    spinlock_release(&queue->lock, flags);
    return NULL;
  }

  thread_t* thread = queue->head;
  queue->head = thread->queue_next;

  if (queue->head) {
    queue->head->queue_prev = NULL;
  } else {
    queue->tail = NULL;
  }

  thread->status = status;
  thread->queue_prev = NULL;
  thread->queue_next = NULL;
  thread->wait_queue = NULL;

  queue->count--;
  spinlock_release(&queue->lock, flags);

  return thread;
}

void scheduler_add_process(process_t* process) {
  if (!process) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&scheduler_state.lock, &flags);

  process->next = NULL;
  process->prev = scheduler_state.process_list_tail;

  if (scheduler_state.process_list_tail) {
    scheduler_state.process_list_tail->next = process;
  } else {
    scheduler_state.process_list_head = process;
  }

  scheduler_state.process_list_tail = process;

  __atomic_add_fetch(&scheduler_state.total_processes, 1, __ATOMIC_SEQ_CST);
  spinlock_release(&scheduler_state.lock, flags);
}

void scheduler_remove_process(process_t* process) {
  if (!process) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&scheduler_state.lock, &flags);

  if (process->prev) {
    process->prev->next = process->next;
  } else {
    scheduler_state.process_list_head = process->next;
  }

  if (process->next) {
    process->next->prev = process->prev;
  } else {
    scheduler_state.process_list_tail = process->prev;
  }

  __atomic_sub_fetch(&scheduler_state.total_processes, 1, __ATOMIC_SEQ_CST);
  spinlock_release(&scheduler_state.lock, flags);
}

process_t* scheduler_get_process_by_pid(size_t pid) {
  if (!scheduler_state.process_list_head) {
    return NULL;
  }

  if (scheduler_state.process_list_tail->pid == pid) {
    return scheduler_state.process_list_tail;
  }

  process_t* current = scheduler_state.process_list_head;

  while (current) {
    if (current->pid == pid) {
      return current;
    }
    current = current->next;
  }

  return NULL;
}

void scheduler_add_thread(process_t* process, thread_t* thread) {
  if (!process || !thread) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&process->lock, &flags);

  // insert at head of the thread list
  thread->next = process->thread_list_head;
  thread->parent_process = process;
  process->thread_list_head = thread;
  process->active_threads++;

  spinlock_release(&process->lock, flags);
}

thread_t* scheduler_get_thread(process_t* process, size_t tid) {
  if (!process) {
    return NULL;
  }

  unsigned long flags;
  spinlock_acquire(&process->lock, &flags);

  thread_t* current = process->thread_list_head;

  while (current) {
    if (current->tid == tid) {
      spinlock_release(&process->lock, flags);
      return current;
    }
    current = current->next;
  }

  spinlock_release(&process->lock, flags);
  return NULL;
}

void scheduler_sleep_queue_push(thread_t* thread) {
  if (!thread) {
    return;
  }

  thread->status = TASK_SLEEPING;

  unsigned long flags;
  spinlock_acquire(&scheduler_state.sleeping_queue.lock, &flags);

  // add in sorted order based on wakeup_time
  thread_t* current = scheduler_state.sleeping_queue.head;

  while (current && current->wakeup_time <= thread->wakeup_time) {
    current = current->queue_next;
  }

  if (current) {
    thread->queue_next = current;
    thread->queue_prev = current->queue_prev;

    if (current->queue_prev) {
      current->queue_prev->queue_next = thread;
    } else {
      scheduler_state.sleeping_queue.head = thread;
    }

    current->queue_prev = thread;
  } else {
    // Insert at the end of the queue
    thread->queue_next = NULL;
    thread->queue_prev = scheduler_state.sleeping_queue.tail;

    if (scheduler_state.sleeping_queue.tail) {
      scheduler_state.sleeping_queue.tail->queue_next = thread;
    } else {
      scheduler_state.sleeping_queue.head = thread;
    }

    scheduler_state.sleeping_queue.tail = thread;
  }

  thread->wait_queue = &scheduler_state.sleeping_queue;
  scheduler_state.sleeping_queue.count++;

  spinlock_release(&scheduler_state.sleeping_queue.lock, flags);
}

static inline void save_context(cpu_core_t* core,
                                struct scheduler_frame_s* frame) {
  thread_t* current_thread = core->active_thread;
  current_thread->kernel_stack_ptr = (uintptr_t)frame;
  current_thread->user_stack_ptr = core->user_sp;
}

static inline void restore_context(cpu_core_t* core, thread_t* new_thread) {
  core->active_thread = new_thread;
  core->kernel_sp = (uintptr_t)new_thread->kernel_stack_base;
  core->user_sp = (uintptr_t)new_thread->user_stack_ptr;
}

void check_pending_signals(void) {
  thread_t* current = scheduler_get_current_thread();

  if (!current) {
    return;
  }

  uint32_t active_signals = current->pending_signals & ~(current->signal_mask);

  if (active_signals == 0) {
    return;
  }

  // Check for SIGINT (Ctrl+C) or SIGKILL
  if ((active_signals & (1 << SIGINT)) || (active_signals & (1 << SIGKILL))) {
    // Clear the signal bit
    current->pending_signals &= ~(1 << SIGINT);
    current->pending_signals &= ~(1 << SIGKILL);

    // Terminate the thread.
    thread_exit(-1);
  }
}

static inline void scheduler(struct scheduler_frame_s* frame) {
  cpu_core_t* core = get_current_cpu();
  thread_t* prev_thread = core->active_thread;
  thread_queue_t* ready_queue = &scheduler_state.ready_queue;
  thread_t* next_thread = scheduler_queue_pop(ready_queue, TASK_RUNNING);

  if (!next_thread) {
    thread_t* idle_thread = core->idle_thread;

    if (prev_thread->status == TASK_RUNNING && prev_thread != idle_thread) {
      next_thread = prev_thread;
    } else {
      next_thread = idle_thread;
    }
  }

  if (prev_thread == next_thread) {
    return;
  }

  // save context of the previous thread
  save_context(core, frame);

  if (prev_thread->status == TASK_RUNNING) {
    scheduler_queue_push(ready_queue, prev_thread, TASK_READY);
  }

  // switch to the next thread
  restore_context(core, next_thread);

  // check if the previous thread and next thread belong to the same process
  if (prev_thread->parent_process == next_thread->parent_process) {
    switch_thread((uint64_t)next_thread->kernel_stack_ptr,
                  (uint64_t)next_thread->kernel_stack_base);
  }

  switch_context((uint64_t)next_thread->parent_process->page_table,
                 (uint64_t)next_thread->kernel_stack_ptr,
                 (uint64_t)next_thread->kernel_stack_base);
}

static inline void check_sleeping_threads(void) {
  while (scheduler_state.sleeping_queue.head &&
         scheduler_state.sleeping_queue.head->wakeup_time <=
             timer_get_ticks()) {
    thread_queue_t* sleeping_queue = &scheduler_state.sleeping_queue;
    thread_queue_t* ready_queue = &scheduler_state.ready_queue;

    // pop the thread from the sleeping queue
    thread_t* thread = scheduler_queue_pop(sleeping_queue, TASK_READY);

    // push the thread to the ready queue
    scheduler_queue_push(ready_queue, thread, TASK_READY);
  }
}

void timer_irq_isr_handler(struct scheduler_frame_s* frame) {
  check_sleeping_threads();
  scheduler(frame);
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

  while (1) {
    log_print("Thread 1: Counter = %zu\n", counter++);
    kthread_sleep(30000);
  }
}

void test_thread2(void* arg) {
  UNUSED(arg);

  static size_t counter = 0;

  while (counter < 20) {
    log_print("Thread 2: Counter = %zu\n", counter++);
    kthread_sleep(1000);
  }
}

static void load_userspace_init_program(void) {
  int ret = load_user_process(NULL, "/bin/init", NULL);

  if (ret) {
    kernel_panic("Failed to load user process: %d", ret);
  }

  // ret = load_user_process(NULL, "/bin/echo", NULL);

  // if (ret) {
  //   kernel_panic("Failed to load user process: %d", ret);
  // }
}

void scheduler_init(void) {
  memset(&scheduler_state, 0, sizeof(scheduler_state));

  // init core state for each CPU core
  size_t cpu_id = apic_get_id();
  cpu_core_t* core_state = &scheduler_state.cores[cpu_id];

  core_state->cpu_id = cpu_id;

  syscall_init(core_state);
  spinlock_init(&scheduler_state.lock);

  process_t* kernel_process = kprocess_init("kernel");
  thread_t* idle_thread = kthread_init("idle", 0, idle, NULL);

  scheduler_add_process(kernel_process);
  scheduler_add_thread(kernel_process, idle_thread);

  core_state->idle_thread = idle_thread;
  core_state->active_thread = idle_thread;

  scheduler_state.kernel_process = kernel_process;

  // Create test threads
  thread_t* thread1 = kthread_init("test_thread1", 0, test_thread1, NULL);
  // thread_t* thread2 = kthread_init("test_thread2", 0, test_thread2, NULL);

  scheduler_add_thread(kernel_process, thread1);
  // scheduler_add_thread(kernel_process, thread2);

  // scheduler_queue_push(&scheduler_state.ready_queue, idle_thread,
  // TASK_READY);
  scheduler_queue_push(&scheduler_state.ready_queue, thread1, TASK_READY);
  // scheduler_queue_push(&scheduler_state.ready_queue, thread2, TASK_READY);

  log_info("idle thread parent process: %s, pid: %zu",
           idle_thread->parent_process->name, idle_thread->parent_process->pid);

  int result = fs_init();

  if (result != 0) {
    log_error("Failed to initialize filesystem: %d", result);
    return;
  }

  result = kprocess_add_basic_fd(kernel_process);

  if (result != 0) {
    log_error("Failed to add basic file descriptors to kernel process: %d",
              result);
    return;
  }

  tty_init();
  cleaner_init();
  load_userspace_init_program();

#ifdef SCHEDULER_DEBUG
  log_info("Scheduler initialized");
#endif

  // Initialize the APIC timer
  init_apic_timer();

  switch_thread((uint64_t)idle_thread->kernel_stack_ptr,
                (uint64_t)idle_thread->kernel_stack_base);

  idle(NULL);
}

void scheduler_queue_remove(thread_queue_t* queue, thread_t* thread) {
  if (!thread->queue_next && !thread->queue_prev && queue->head != thread) {
    return;
  }

  unsigned long flags;
  spinlock_acquire(&queue->lock, &flags);

  if (thread->queue_prev) {
    thread->queue_prev->queue_next = thread->queue_next;
  } else {
    queue->head = thread->queue_next;
  }

  if (thread->queue_next) {
    thread->queue_next->queue_prev = thread->queue_prev;
  } else {
    queue->tail = thread->queue_prev;
  }

  thread->queue_next = NULL;
  thread->queue_prev = NULL;
  thread->wait_queue = NULL;
  queue->count--;

  spinlock_release(&queue->lock, flags);
}

void scheduler_debug_print(void) {
  log_print("Scheduler State:\n");
  log_print("Total Processes: %zu\n", scheduler_state.total_processes);

  process_t* current_process = scheduler_state.process_list_head;

  while (current_process) {
    log_print("Process: %s, PID: %zu, Active Threads: %zu\n",
              current_process->name, current_process->pid,
              current_process->active_threads);

    thread_t* current_thread = current_process->thread_list_head;

    while (current_thread) {
      log_print("  Thread: %s, TID: %zu, Status: %d\n", current_thread->name,
                current_thread->tid, current_thread->status);
      current_thread = current_thread->next;
    }

    current_process = current_process->next;
  }
}
