#include <arch/x86_64/isr.h>
#include <arch/x86_64/syscall.h>
#include <arch/x86_64/timer.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <process/cleaner.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdatomic.h>
#include <utils/log.h>

void thread_release_locks(thread_t* thread) {
  mutex_t* mutex_lock = thread->mutex_locks_head;

  while (mutex_lock) {
    mutex_t* next_mutex = mutex_lock->next;
    mutex_release(mutex_lock);
    mutex_lock = next_mutex;
  }

  semaphore_t* semaphore_lock = thread->semaphore_locks_head;

  while (semaphore_lock) {
    semaphore_t* next_semaphore = semaphore_lock->next;
    semaphore_up(semaphore_lock);
    semaphore_lock = next_semaphore;
  }

  // remove from any wait queue it might be in
  if (thread->wait_queue) {
    scheduler_queue_remove(thread->wait_queue, thread);
    thread->wait_queue = NULL;
  }
}

static void kthread_exit(thread_t* thread) {
  thread_release_locks(thread);

  thread->status = TASK_DEAD;

  scheduler_yield();
  while (true);
}

static void thread_exec_wrapper(void (*function)(void*), void* arg,
                                thread_t* thread) {
  function(arg);
  kthread_exit(thread);
}

thread_t* kthread_init(const char* name, size_t stack_size,
                       void (*entry_point)(void*), void* arg) {
  UNUSED(stack_size);

  if (!name || !entry_point) {
    return NULL;
  }

  thread_t* thread = kmalloc(sizeof(thread_t));

  if (!thread) {
    return NULL;
  }

  memset(thread, 0, sizeof(thread_t));
  strncpy(thread->name, name, THREAD_NAME_LEN - 1);

  mm_result_t result;
  result = mm_allocate_kstack(&thread->kernel_stack_base);

  if (result != MM_SUCCESS) {
    return NULL;
  }

  uintptr_t addr = thread->kernel_stack_base;
  size_t frame_size = sizeof(struct scheduler_frame_s);

  thread->kernel_stack_ptr = addr - frame_size;
  thread->user_stack_ptr = thread->user_stack_base;

  // Set up the initial stack frame for the thread
  struct scheduler_frame_s* frame = (void*)thread->kernel_stack_ptr;

  memset(frame, 0, sizeof(struct scheduler_frame_s));

  frame->regs.rdi = (uint64_t)entry_point;
  frame->regs.rsi = (uint64_t)arg;
  frame->regs.rdx = (uint64_t)thread;
  frame->rip = (uint64_t)thread_exec_wrapper;
  frame->rsp = (uint64_t)thread->kernel_stack_base;
  frame->regs.rbp = (uint64_t)thread->kernel_stack_base;
  frame->rflags = RFLAGS_IF;
  frame->cs = KERNEL_CS_REGS;
  frame->ss = KERNEL_SS_REGS;

  thread->tid = scheduler_get_next_tid();
  return thread;
}

int kthread_create(const char* name, thread_t** thread,
                   void (*entry_point)(void*), void* arg) {
  thread_t* new_thread = kthread_init(name, 0, entry_point, arg);

  if (!new_thread) {
    return -1;
  }

  if (thread) {
    *thread = new_thread;
  }

  scheduler_add_thread(scheduler_state.kernel_process, new_thread);
  scheduler_queue_push(&scheduler_state.ready_queue, new_thread, TASK_READY);
  return 0;
}

void kthread_sleep(size_t milliseconds) {
  thread_t* current_thread = scheduler_get_current_thread();
  timer_tick_t current_time = timer_get_ticks();
  timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(milliseconds);

  current_thread->wakeup_time = wakeup_time;

  scheduler_sleep_queue_push(current_thread);
  scheduler_yield();
}

int kthread_join(thread_t* child_thread) {
  thread_t* current_thread = scheduler_get_current_thread();

  while (child_thread->status != TASK_DEAD) {
    timer_tick_t current_time = timer_get_ticks();
    timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(100);

    current_thread->wakeup_time = wakeup_time;
    scheduler_sleep_queue_push(current_thread);
    scheduler_yield();
  }

  current_thread->status = TASK_RUNNING;
  return 0;
}

thread_t* pthread_init(const char* name, void* pstack,
                       void (*entry_point)(void*), void* arg) {
  if (!entry_point || !pstack) {
    return NULL;
  }

  thread_t* thread = kmalloc(sizeof(thread_t));

  if (!thread) {
    return NULL;
  }

  memset(thread, 0, sizeof(thread_t));
  strncpy(thread->name, name, THREAD_NAME_LEN - 1);

  mm_result_t result;
  result = mm_allocate_kstack(&thread->kernel_stack_base);

  if (result != MM_SUCCESS) {
    return NULL;
  }

  // initialize the stack pointer for the thread
  uintptr_t addr = thread->kernel_stack_base;
  size_t frame_size = sizeof(struct scheduler_frame_s);
  thread->kernel_stack_ptr = addr - frame_size;

  thread->user_stack_base = (uintptr_t)pstack;
  thread->user_stack_ptr = thread->user_stack_base;

  struct scheduler_frame_s* frame = (void*)thread->kernel_stack_ptr;

  memset(frame, 0, sizeof(struct scheduler_frame_s));

  frame->regs.rdi = (uint64_t)arg;
  frame->rip = (uint64_t)entry_point;
  frame->rsp = (uint64_t)thread->user_stack_base;
  frame->regs.rbp = (uint64_t)thread->user_stack_base;
  frame->rflags = RFLAGS_IF;
  frame->cs = USER_CS_REGS;
  frame->ss = USER_SS_REGS;

  thread->tid = scheduler_get_next_tid();
  return thread;
}

static inline void pthread_sleep(size_t milliseconds) {
  thread_t* current_thread = scheduler_get_current_thread();
  timer_tick_t current_time = timer_get_ticks();
  timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(milliseconds);

  current_thread->wakeup_time = wakeup_time;
  scheduler_sleep_queue_push(current_thread);
  scheduler_yield();
}

static inline void pthread_exit(int exit_code) {
  thread_t* current_thread = scheduler_get_current_thread();
  current_thread->parent_process->exit_code = exit_code;

  thread_release_locks(current_thread);
  reaper_release_resources(current_thread->parent_process);

  current_thread->status = TASK_DEAD;

  scheduler_yield();
}

void thread_exit(int exit_code) { pthread_exit(exit_code); }

static inline int pthread_join(size_t tid) {
  thread_t* current_thread = scheduler_get_current_thread();
  process_t* current_process = current_thread->parent_process;
  thread_t* child_thread = scheduler_get_thread(current_process, tid);

  if (!child_thread) {
    return -1;
  }

  while (child_thread->status != TASK_DEAD) {
    timer_tick_t current_time = timer_get_ticks();
    timer_tick_t wakeup_time = current_time + timer_ms_to_ticks(100);

    current_thread->wakeup_time = wakeup_time;
    scheduler_sleep_queue_push(current_thread);
    scheduler_yield();
  }

  current_thread->status = TASK_RUNNING;
  return 0;
}

void sys_pthread_sleep(syscall_frame_t* frame) {
  size_t milliseconds = frame->arg1;
  pthread_sleep(milliseconds);
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
    return;
  }

  thread_t* thread = pthread_init("user_thread", pstack, entry_point, arg);

  if (!thread) {
    frame->syscall_num = (size_t)-1;
    return;
  }

  frame->syscall_num = thread->tid;

  process_t* current_process = scheduler_get_current_process();
  scheduler_add_thread(current_process, thread);
  scheduler_queue_push(&scheduler_state.ready_queue, thread, TASK_READY);
}

void sys_pthread_join(syscall_frame_t* frame) {
  size_t tid = frame->arg1;
  frame->syscall_num = pthread_join(tid);
}
