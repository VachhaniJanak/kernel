#include <arch/x86_64/isr.h>
#include <arch/x86_64/syscall.h>
#include <arch/x86_64/timer.h>
#include <mm/mm.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <utils/log.h>
#include <utils/utils.h>

// #define THREAD_DEBUG

extern spinlock_t scheduler_state_lock;

static void thread_exit(thread_t* thread) {
  thread->status = THREAD_DEAD;
  while (true);
}

static void thread_exec_wrapper(void (*function)(void*), void* arg,
                                thread_t* thread) {
  function(arg);
  thread_exit(thread);
}

static bool kthread_init(const char* name, thread_t* thread, size_t stack_size,
                         void (*entry_point)(void*), void* arg) {
  kstrcpy(thread->name, name);
  thread->status = THREAD_READY;

  // Allocate stack for the thread
  void* root_table = phys_to_virt(mm_get_kernel_root_table());

  mm_result_t result;
  result =
      mm_allocate_kstack(root_table, (uintptr_t*)&thread->kernel_stack_base);

  if (result != MM_SUCCESS) {
#ifdef THREAD_DEBUG
    log_error("Failed to allocate kernel stacks for thread '%s', Error: %d",
              name, result);
#endif
    return false;
  }

  uintptr_t addr = (uintptr_t)thread->kernel_stack_base;
  size_t frame_size = sizeof(struct scheduler_frame_s);

  thread->kernel_stack_ptr = (void*)(addr - frame_size);
  thread->user_stack_ptr = thread->user_stack_base;

  // Set up the initial stack frame for the thread
  struct scheduler_frame_s* frame = thread->kernel_stack_ptr;

  frame->regs.rdi = (uint64_t)entry_point;
  frame->regs.rsi = (uint64_t)arg;
  frame->regs.rdx = (uint64_t)thread;
  frame->rip = (uint64_t)thread_exec_wrapper;
  frame->rsp = (uint64_t)thread->kernel_stack_base;
  frame->regs.rbp = (uint64_t)thread->kernel_stack_base;
  frame->rflags = RFLAGS_IF;
  frame->cs = KERNEL_CS_REGS;
  frame->ss = KERNEL_SS_REGS;

#ifdef THREAD_DEBUG
  log_print("Kernel Thread Initialized: %s, (TID: %lu)\n", name, thread->tid);
  log_print("  User Stack: 0x%lx\n", (uintptr_t)thread->user_stack_base);
  log_print("  Kernel Stack: 0x%lx\n", (uintptr_t)thread->kernel_stack_base);
  log_print("  Stack Pointer: 0x%lx\n", (uintptr_t)thread->kernel_stack_ptr);
  log_print("  Entry Point: 0x%lx\n", (uintptr_t)entry_point);
  log_print("  Argument: 0x%lx\n", (uintptr_t)arg);
#endif

  return true;
}

int kthread_create(const char* name, thread_t** thread,
                   void (*entry_point)(void*), void* arg) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* kernel_process = scheduler_get_kernel_process();

  if (kernel_process == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Faild to get kernel process");
#endif
    return -1;
  }

  thread_t* new_thread = scheduler_add_thread(kernel_process);

  if (new_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Faild to create thread for process %s", kernel_process->name);
#endif
    return -1;
  }

  size_t stack_size = mm_get_kernel_thread_stack_size();

  if (!kthread_init(name, new_thread, stack_size, entry_point, arg)) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Faild to initialize thread '%s' for process %s", name,
              kernel_process->name);
#endif
    return -1;
  }

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  if (thread != NULL) {
    *thread = new_thread;
  }

  return new_thread->tid;
}

void kthread_sleep(size_t milliseconds) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  thread_t* current_thread = scheduler_get_current_thread();

  if (current_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

#ifdef THREAD_DEBUG
    log_error("Failed to get current thread: kthread_sleep");
#endif
    return;
  }

  current_thread->status = THREAD_SLEEPING;

  timer_tick_t current_time = timer_get_ticks();
  timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(milliseconds);
  current_thread->wakeup_time = wakeup_time;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
  scheduler_yield();
}

int kthread_join(thread_t* child_thread) {
  if (child_thread == NULL) {
    return -1;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  thread_t* current_thread = scheduler_get_current_thread();

  if (current_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to get current thread: kthread_join");
#endif
    return -1;
  }

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  while (child_thread->status != THREAD_DEAD) {
    current_thread->status = THREAD_SLEEPING;
    
    // Sleep for 100 ms
    timer_tick_t current_time = timer_get_ticks();
    timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(100);

    current_thread->wakeup_time = wakeup_time;
    
    scheduler_yield();
  }

  current_thread->status = THREAD_RUNNING;
  return child_thread->exit_code;
}

bool pthread_init(process_t* process, const char* name, thread_t* thread,
                  void* pstack, void (*entry_point)(void*), void* arg) {
  if (process == NULL || thread == NULL || entry_point == NULL ||
      pstack == NULL) {
    return false;
  }

  kstrcpy(thread->name, name);
  thread->status = THREAD_READY;
  thread->user_stack_base = pstack;

  void* root_table = phys_to_virt(process->page_table);

  // Allocate stack for the thread
  mm_result_t result;
  result =
      mm_allocate_kstack(root_table, (uintptr_t*)&thread->kernel_stack_base);

  if (result != MM_SUCCESS) {
    mm_free_pstack(root_table, (uintptr_t)thread->user_stack_base);
#ifdef THREAD_DEBUG
    log_error("Failed to allocate kernel stacks for thread '%s', Error: %d",
              name, result);
#endif
    return false;
  }

  // initialize the stack pointer for the thread
  size_t frame_size = sizeof(struct scheduler_frame_s);
  thread->kernel_stack_ptr =
      (void*)((uintptr_t)thread->kernel_stack_base - frame_size);
  thread->user_stack_ptr = thread->user_stack_base;

  uintptr_t phys_addr;
  result = get_mapping(root_table, thread->kernel_stack_ptr, &phys_addr);

  if (result != MM_SUCCESS) {
#ifdef THREAD_DEBUG
    log_error("Failed to get physical address for thread '%s' stack, Error: %d",
              name, result);
#endif
    return false;
  }

#ifdef THREAD_DEBUG
  log_print("User Thread Initialized: %s, (PID: %lu) (TID: %lu)\n", name,
            process->pid, thread->tid);
  log_print("  User Stack: 0x%lx\n", (uintptr_t)thread->user_stack_base);
  log_print("  Kernel Stack: 0x%lx\n", (uintptr_t)thread->kernel_stack_base);
  log_print("  Stack Pointer: 0x%lx\n", (uintptr_t)thread->kernel_stack_ptr);
  log_print("  Physical Address of Stack Pointer: 0x%lx\n", phys_addr);
  log_print("  Entry Point: 0x%lx\n", (uintptr_t)entry_point);
  log_print("  Argument: 0x%lx\n", (uintptr_t)arg);
#endif

  struct scheduler_frame_s* frame = phys_to_virt((void*)phys_addr);

  frame->regs.rdi = (uint64_t)arg;
  frame->rip = (uint64_t)entry_point;
  frame->rsp = (uint64_t)thread->user_stack_base;
  frame->regs.rbp = (uint64_t)thread->user_stack_base;
  frame->rflags = RFLAGS_IF;
  frame->cs = USER_CS_REGS;
  frame->ss = USER_SS_REGS;
  return true;
}

static inline void pthread_sleep(size_t milliseconds) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  thread_t* current_thread = scheduler_get_current_thread();

  if (current_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to get current thread: sys_pthread_sleep");
#endif
    return;
  }

  current_thread->status = THREAD_SLEEPING;

  timer_tick_t current_time = timer_get_ticks();
  timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(milliseconds);
  current_thread->wakeup_time = wakeup_time;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  scheduler_yield();
}

static inline void pthread_exit(int exit_code) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  thread_t* current_thread = scheduler_get_current_thread();

  if (current_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to get current thread: sys_pthread_exit");
#endif
    return;
  }

  current_thread->exit_code = exit_code;
  current_thread->status = THREAD_DEAD;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  scheduler_yield();
}

static inline int pthread_create(size_t* tid, void (*entry_point)(void*),
                                 void* arg, void* pstack) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* process = scheduler_get_current_process();

  if (process == NULL || entry_point == NULL || pstack == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to get current process: sys_pthread_create");
#endif
    return -1;
  }

  thread_t* new_thread = scheduler_add_thread(process);

  if (new_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to create thread for process '%s' (PID: %zu)",
              process->name, process->pid);
#endif
    return -1;
  }

  if (!pthread_init(process, process->name, new_thread, pstack, entry_point,
                    arg)) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to initialize thread '%s' for process '%s' (PID: %zu)",
              process->name, process->name, process->pid);
#endif
    return -1;
  }

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  if (tid != NULL) {
    *tid = new_thread->tid;
  }

  return 0;
}

static inline int pthread_join(size_t tid) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  process_t* current_process = scheduler_get_current_process();
  thread_t* current_thread = scheduler_get_current_thread();

  if (current_process == NULL || current_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return -1;
  }

  thread_t* child_thread = scheduler_get_thread(current_process, tid);

  if (child_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
    return -1;
  }

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  while (child_thread->status != THREAD_DEAD) {
    current_thread->status = THREAD_SLEEPING;

    timer_tick_t current_time = timer_get_ticks();
    timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(100);

    // Sleep for the specified duration
    current_thread->wakeup_time = wakeup_time;

    // Give the CPU to someone else
    scheduler_yield();
  }

  // SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);
  current_thread->status = THREAD_RUNNING;
  // SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  return child_thread->exit_code;
}

void sys_pthread_sleep(syscall_frame_t* frame) {
  size_t milliseconds = frame->arg1;
  pthread_sleep(milliseconds);
  // log_debug("sys_thread_sleep exited\n");
}

void sys_pthread_exit(syscall_frame_t* frame) {
  int exit_code = frame->arg1;
  pthread_exit(exit_code);
}

void sys_pthread_create(syscall_frame_t* frame) {
  void (*entry_point)(void*) = (void (*)(void*))frame->arg1;
  void* arg = (void*)frame->arg2;
  void* pstack = (void*)frame->arg3;

  const size_t page_size = mm_get_page_size();

  if (!is_page_aligned((uintptr_t)pstack, page_size)) {
    frame->syscall_num = (size_t)-1;
#ifdef THREAD_DEBUG
    log_error("User stack pointer is not page-aligned: 0x%lx",
              (uintptr_t)pstack);
#endif
    return;
  }

  size_t tid;
  int result = pthread_create(&tid, entry_point, arg, pstack);

  if (result != 0) {
    frame->syscall_num = (size_t)-1;
#ifdef THREAD_DEBUG
    log_error("Failed to create thread, error code: %d", result);
#endif
    return;
  }

  frame->syscall_num = tid;
#ifdef THREAD_DEBUG
  log_print("Thread created with ID: %zu\n", tid);
#endif
}

void sys_pthread_join(syscall_frame_t* frame) {
  size_t tid = frame->arg1;
  frame->syscall_num = pthread_join(tid);
}
