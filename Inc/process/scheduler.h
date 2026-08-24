#pragma once

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

typedef enum { VMA_READ = 0x01, VMA_WRITE = 0x02, VMA_EXEC = 0x04 } vma_flags_t;

typedef enum {
  VMA_ANONYMOUS = 0x10,    // No file backing (Stack, Heap, BSS gap)
  VMA_FILE_BACKED = 0x20,  // Needs to be read from a file (ELF code/data)
  VMA_SHARED = 0x40,       // Shared memory
  VMA_PRIVATE = 0x80       // Private memory
} vma_type_t;

typedef struct vma_s {
  uint64_t vm_start;
  uint64_t vm_end;
  uint32_t flags;
  vfs_t* file;
  uint64_t file_offset;
  uint64_t file_size;
  struct vma_s* next;
} vma_t;

typedef struct thread_s {
  size_t tid;
  char name[THREAD_NAME_LEN];
  task_status_t status;
  void* user_stack;
  void* kernel_stack;
  void* current_stack_ptr;
  size_t wakeup_time;
  struct thread_s* next;
} thread_t;

typedef struct process_s {
  size_t pid;
  char name[PROCESS_NAME_LEN];
  void* page_table;
  struct thread_s* thread_list_start;
  struct thread_s* thread_list_end;
  struct vma_s* vma_head;
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
  uint64_t user_rsp;    // Offset 0x00
  uint64_t kernel_rsp;  // Offset 0x08
  uint64_t cpu_id;      // Offset 0x10
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

process_t* scheduler_get_process_list_start(void);

process_t* scheduler_get_process_list_end(void);

size_t scheduler_get_total_processes(void);

size_t scheduler_get_total_threads(void);

void scheduler_yield(void);

process_t* scheduler_add_process(void);

thread_t* scheduler_add_thread(process_t* process);

bool scheduler_remove_thread(process_t* process, size_t tid);

thread_t* scheduler_get_thread(process_t* process, size_t tid);

bool scheduler_remove_process(size_t pid);

size_t get_system_time(void);
