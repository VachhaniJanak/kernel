#pragma once

#include <stdint.h>
#include <stddef.h>

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

typedef struct thread_s {
  size_t tid;
  char name[THREAD_NAME_LEN];
  task_status_t status;
  void* stack_ptr;
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

void init_scheduler(void);

int create_kthread(const char* name, thread_t** thread, size_t stack_size,
                   void (*entry_point)(void* arg), void* arg);

int join_kthread(thread_t* thread);

void kthread_sleep(size_t milliseconds);

process_t* scheduler_get_current_process(void);

process_t* scheduler_get_kernel_process(void);

thread_t* scheduler_get_current_thread(void);

thread_t* scheduler_get_idle_thread(void);

process_t* scheduler_get_process_list_start(void);

process_t* scheduler_get_process_list_end(void);

size_t scheduler_get_total_processes(void);

size_t scheduler_get_total_threads(void);

void scheduler_yield(void);
