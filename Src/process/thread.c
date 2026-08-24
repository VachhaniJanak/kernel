#include <arch/x86_64/isr.h>
#include <mm/mm.h>
#include <mm/vmm/vmm.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <utils/log.h>
#include <utils/utils.h>

#define THREAD_DEBUG

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
  mm_flags_t flags = MM_FLAG_WRITABLE;
  thread->kernel_stack = vmalloc(stack_size, flags, false);

  if (thread->kernel_stack == NULL) {
    return false;
  }

  // Stack grows downwards
  size_t stack_top = (size_t)thread->kernel_stack + stack_size;
  thread->kernel_stack = (void*)stack_top;

  size_t frame_size = sizeof(struct scheduler_frame_s);
  thread->current_stack_ptr = (void*)(stack_top - frame_size);

  // Set up the initial stack frame for the thread
  struct scheduler_frame_s* frame = thread->current_stack_ptr;

  frame->regs.rdi = (uint64_t)entry_point;
  frame->regs.rsi = (uint64_t)arg;
  frame->regs.rdx = (uint64_t)thread;
  frame->rip = (uint64_t)thread_exec_wrapper;
  frame->rsp = (uint64_t)thread->kernel_stack;
  frame->regs.rbp = (uint64_t)thread->kernel_stack;
  frame->rflags = RFLAGS_IF;
  frame->cs = KERNEL_CS_REGS;
  frame->ss = KERNEL_SS_REGS;

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

  scheduler_get_current_thread()->status = THREAD_SLEEPING;
  scheduler_get_current_thread()->wakeup_time =
      get_system_time() + milliseconds;

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  scheduler_yield();
}

int kthread_join(thread_t* thread) {
  if (thread == NULL) {
    return -1;
  }

  while (thread->status != THREAD_DEAD) {
    kthread_sleep(100);
  }

  return 0;
}

bool user_thread_init(process_t* process, const char* name, thread_t* thread,
                      void (*entry_point)(void*), void* arg) {
  if (process == NULL || thread == NULL || entry_point == NULL) {
    return false;
  }

  kstrcpy(thread->name, name);
  thread->status = THREAD_READY;

  void* root_table = phys_to_virt(process->page_table);

  // Allocate stack for the thread
  mm_result_t result =
      mm_allocate_user_stacks(root_table, (uintptr_t*)&thread->user_stack,
                              (uintptr_t*)&thread->kernel_stack);

  if (result != MM_SUCCESS) {
#ifdef THREAD_DEBUG
    log_error("Failed to allocate user stacks for thread '%s', Error: %d", name,
              result);
#endif
    return false;
  }

  // initialize the stack pointer for the thread
  size_t frame_size = sizeof(struct scheduler_frame_s);
  thread->current_stack_ptr =
      (void*)((uintptr_t)thread->kernel_stack - frame_size);

  uintptr_t phys_addr;
  result = get_mapping(root_table, thread->current_stack_ptr, &phys_addr);

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
  log_print("  User Stack: 0x%lx\n", (uintptr_t)thread->user_stack);
  log_print("  Kernel Stack: 0x%lx\n", (uintptr_t)thread->kernel_stack);
  log_print("  Stack Pointer: 0x%lx\n", (uintptr_t)thread->current_stack_ptr);
  log_print("  Physical Address of Stack Pointer: 0x%lx\n", phys_addr);
  log_print("  Entry Point: 0x%lx\n", (uintptr_t)entry_point);
  log_print("  Argument: 0x%lx\n", (uintptr_t)arg);
#endif

  struct scheduler_frame_s* frame = phys_to_virt((void*)phys_addr);

  frame->regs.rdi = (uint64_t)arg;
  frame->rip = (uint64_t)entry_point;
  frame->rsp = (uint64_t)thread->user_stack;
  frame->regs.rbp = (uint64_t)thread->user_stack;
  frame->rflags = RFLAGS_IF;
  frame->cs = USER_CS_REGS;
  frame->ss = USER_SS_REGS;
  return true;
}

int user_thread_create(process_t* process, const char* name, thread_t** thread,
                       void (*entry_point)(void*), void* arg) {
  if (process == NULL || entry_point == NULL) {
    return -1;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&scheduler_state_lock, flags);

  thread_t* new_thread = scheduler_add_thread(process);

  if (new_thread == NULL) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to create thread for process '%s' (PID: %zu)",
              process->name, process->pid);
#endif
    return -1;
  }

  if (!user_thread_init(process, name, new_thread, entry_point, arg)) {
    SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);
#ifdef THREAD_DEBUG
    log_error("Failed to initialize thread '%s' for process '%s' (PID: %zu)",
              name, process->name, process->pid);
#endif
    return -1;
  }

  SPIN_LOCK_RELEASE(&scheduler_state_lock, flags);

  if (thread != NULL) {
    *thread = new_thread;
  }

  return new_thread->tid;
}