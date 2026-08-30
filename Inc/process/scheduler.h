#pragma once

#include <arch/x86_64/timer.h>
#include <stddef.h>
#include <stdint.h>
#include <vfs/vfs.h>

#define PROCESS_NAME_LEN 32
#define THREAD_NAME_LEN 32

typedef enum {
  THREAD_NOTREADY,
  THREAD_READY,
  THREAD_RUNNING,
  THREAD_DEAD,
  THREAD_SLEEPING,
  THREAD_BLOCKED
} task_status_t;

typedef enum {
  VMA_READ = 0x01,
  VMA_WRITE = 0x02,
  VMA_EXEC = 0x04,
  VMA_NONE = 0x08,
} vma_flags_t;

typedef enum {
  VMA_ANONYMOUS = 0x10,    // No file backing (Stack, Heap, BSS gap)
  VMA_FILE_BACKED = 0x20,  // Needs to be read from a file (ELF code/data)
  VMA_DOWNWARD = 0x40,
  VMA_SHARED = 0x80,
  VMA_PRIVATE = 0x100,
  VMA_GUARD = 0x200,
  VMA_MMAP = 0x400,
  VMA_HEAP = 0x800
} vma_type_t;

typedef enum { GS_USER = 0x0, GS_KERNEL = 0x1 } gs_state_t;

typedef struct vma_s {
  uint64_t vm_start;
  uint64_t vm_end;
  uint32_t flags;
  vfs_t* file;
  uint64_t file_offset;
  uint64_t file_size;
  struct vma_s* prev;
  struct vma_s* next;
} vma_t;

typedef struct thread_s {
  size_t tid;
  char name[THREAD_NAME_LEN];
  task_status_t status;
  void* user_stack_base;    // base address of the user stack
  void* kernel_stack_base;  // base address of the kernel stack
  void* kernel_stack_ptr;   // current stack pointer for the kernel stack
  void* user_stack_ptr;     // current stack pointer for the user stack
  timer_tick_t wakeup_time;
  struct thread_s* next;
  int exit_code;
  gs_state_t gs_state;  // thread is in user or kernel mode
} thread_t;

typedef struct process_s {
  size_t pid;
  char name[PROCESS_NAME_LEN];
  void* page_table;
  struct thread_s* thread_list_start;
  struct thread_s* thread_list_end;
  struct vma_s* vma_head;
  struct vma_s* vma_tail;
  struct vma_s* heap_vma;
  uintptr_t brk;
  struct vma_s* mmap_vma;
  struct process_s* next;
} process_t;

struct scheduler_state_s {
  process_t* process_list_start;
  process_t* process_list_end;
  process_t* kernel_process;
  process_t* current_process;
  struct thread_s* current_thread;
  struct thread_s* idle_thread;
  size_t total_processes;
  size_t total_threads;
  size_t next_pid;
  size_t next_tid;
};

// This structure holds data specific to ONE CPU core.
typedef struct {
  uint64_t user_sp;    // Offset 0x00
  uint64_t kernel_sp;  // Offset 0x08

  /*
   * GS state expected when this thread resumes.
   *
   * GS_USER   -> CPU must have user GS active
   * GS_KERNEL -> CPU must have kernel GS active
   */
  gs_state_t gs_state;  // Offset 0x10
  uint64_t cpu_id;      // Offset 0x18
} cpu_local_data_t;

typedef struct {
  uint64_t rpt;
  uint64_t stack;
} context_switch_t;

void scheduler_init(void);

process_t* scheduler_get_current_process(void);

process_t* scheduler_get_kernel_process(void);

thread_t* scheduler_get_current_thread(void);

thread_t* scheduler_get_idle_thread(void);

size_t scheduler_get_total_processes(void);

size_t scheduler_get_total_threads(void);

void scheduler_yield(void);

process_t* scheduler_add_process(void);

thread_t* scheduler_add_thread(process_t* process);

bool scheduler_remove_thread(process_t* process, size_t tid);

thread_t* scheduler_get_thread(process_t* process, size_t tid);

bool scheduler_remove_process(size_t pid);
