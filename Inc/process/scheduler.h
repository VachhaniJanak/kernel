#pragma once

#include <arch/x86_64/switch.h>
#include <arch/x86_64/timer.h>
#include <fs/fs.h>
#include <stddef.h>
#include <stdint.h>

#include "locks.h"

#define PROCESS_NAME_LEN 32
#define THREAD_NAME_LEN 32
#define MAX_SYSTEM_OPEN_FILES 16

struct vma_s;
struct process_s;
typedef struct thread_queue_s thread_queue_t;

extern void cleaner_add_process(struct process_s* process);

typedef enum {
  TASK_NOTREADY,
  TASK_READY,
  TASK_RUNNING,
  TASK_DEAD,
  TASK_SLEEPING,
  TASK_BLOCKED,
  TASK_SYSCALL_WAIT,
} task_status_t;

enum {
  SIGINT = 2,   // Interrupt from keyboard (Ctrl+C)
  SIGKILL = 9,  // Kill signal
};

typedef struct thread_s {
  size_t tid;
  char name[THREAD_NAME_LEN];
  task_status_t status;
  uintptr_t user_stack_base;    // base address of the user stack
  uintptr_t kernel_stack_base;  // base address of the kernel stack
  uintptr_t kernel_stack_ptr;   // current stack pointer for the kernel stack
  uintptr_t user_stack_ptr;     // current stack pointer for the user stack
  timer_tick_t wakeup_time;

  struct process_s* parent_process;  // pointer to the parent process
  struct thread_s* next;  // pointer to sibling thread in the same process

  // pointers used for queue(ready, sleeping, blocked)
  struct thread_s* queue_next;
  struct thread_s* queue_prev;

  mutex_t* mutex_locks_head;
  semaphore_t* semaphore_locks_head;
  thread_queue_t* wait_queue;

  spinlock_t lock;

  uint32_t pending_signals;
  uint32_t signal_mask;
} thread_t;

typedef struct thread_queue_s {
  thread_t* head;
  thread_t* tail;
  size_t count;
  spinlock_t lock;
} thread_queue_t;

typedef struct process_s {
  size_t pid;
  char name[PROCESS_NAME_LEN];
  uintptr_t page_table;
  struct thread_s* thread_list_head;

  struct vma_s* vma_head;
  struct vma_s* vma_tail;
  struct vma_s* heap_vma;
  struct vma_s* mmap_vma;

  uintptr_t brk;

  struct process_s* prev;
  struct process_s* next;

  // Number of threads that are not dead
  int active_threads;

  spinlock_t lock;
  struct file** open_files;
  int exit_code;

  struct process_s* parent;
  struct process_s* children;
  struct process_s* sibling_prev;
  struct process_s* sibling_next;

  thread_queue_t child_waiters;    // threads waiting for this process to exit
  thread_queue_t waiting_threads;  // threads waiting any child process to exit
  struct dentry* exe;
  struct dentry* cwd;
} process_t;

// This structure holds data specific to ONE CPU core.
typedef struct cpu_core_s {
  uint64_t cpu_id;                 // Offset 0x00
  uintptr_t user_sp;               // Offset 0x08
  uintptr_t kernel_sp;             // Offset 0x10
  struct thread_s* active_thread;  // The currently running thread
  struct thread_s* idle_thread;    // The idle thread for this CPU core
} cpu_core_t;

struct scheduler_state_s {
  spinlock_t lock;

  process_t* process_list_head;
  process_t* process_list_tail;

  process_t* kernel_process;

  thread_queue_t ready_queue;
  thread_queue_t sleeping_queue;

  size_t total_processes;

  size_t next_pid;
  size_t next_tid;

  cpu_core_t cores[4];  // Assuming a maximum of 4 CPU cores for now
};

extern struct scheduler_state_s scheduler_state;

static inline thread_t* scheduler_get_current_thread(void) {
  cpu_core_t* core = get_current_cpu();
  return core->active_thread;
}

static inline process_t* scheduler_get_current_process(void) {
  thread_t* current_thread = scheduler_get_current_thread();
  return current_thread ? current_thread->parent_process : NULL;
}

static inline process_t* scheduler_get_kernel_process(void) {
  return scheduler_state.kernel_process;
}

static inline size_t scheduler_get_total_processes(void) {
  return scheduler_state.total_processes;
}

static inline size_t scheduler_get_next_pid(void) {
  return __atomic_add_fetch(&scheduler_state.next_pid, 1, __ATOMIC_SEQ_CST);
}

static inline size_t scheduler_get_next_tid(void) {
  return __atomic_add_fetch(&scheduler_state.next_tid, 1, __ATOMIC_SEQ_CST);
}

static inline struct file* scheduler_get_fd(thread_t* thread, int fd) {
  if (!thread || fd < 0 || fd >= MAX_SYSTEM_OPEN_FILES) {
    return NULL;
  }

  return thread->parent_process->open_files[fd];
}

static inline void scheduler_yield(void) {
  __asm__ volatile("int $32" ::: "memory");
}

void scheduler_init(void);

void scheduler_queue_push(thread_queue_t* queue, thread_t* thread,
                          task_status_t status);

thread_t* scheduler_queue_pop(thread_queue_t* queue, task_status_t status);

thread_t* scheduler_get_thread(process_t* process, size_t tid);

void scheduler_add_process(process_t* process);

void scheduler_remove_process(process_t* process);

process_t* scheduler_get_process_by_pid(size_t pid);

void scheduler_add_thread(process_t* process, thread_t* thread);

void scheduler_sleep_queue_push(thread_t* thread);

void scheduler_queue_remove(thread_queue_t* queue, thread_t* thread);

void scheduler_debug_print(void);
