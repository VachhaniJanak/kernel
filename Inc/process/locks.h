#pragma once

#include <stdint.h>

#include "scheduler.h"

#define SPIN_LOCK_ACQUIRE(lock_ptr, flags_var) \
  do {                                         \
    (flags_var) = spinlock_acquire(lock_ptr);  \
  } while (0)

#define SPIN_LOCK_RELEASE(lock_ptr, flags_var) \
  do {                                         \
    spinlock_release(lock_ptr, flags_var);     \
  } while (0)

typedef struct {
  volatile int locked;  // 0 = free, 1 = locked
} spinlock_t;

typedef struct wait_list_node_s {
  struct wait_list_node_s* next;
  struct thread_s* thread;  // The thread that is waiting on the mutex
} wait_list_node_t;

typedef struct {
  spinlock_t internal_lock;  // Protects the mutex itself
  int is_locked;             // 0 = free, 1 = taken by someone

  // A linked list of sleeping threads
  wait_list_node_t* wait_queue_head;
  wait_list_node_t* wait_queue_tail;
} mutex_t;

typedef struct {
  spinlock_t internal_lock;  // Protects the counter and wait queue
  int count;        // The number of available resources/signals

  // A linked list of sleeping threads
  wait_list_node_t* wait_queue_head;
  wait_list_node_t* wait_queue_tail;
} semaphore_t;

void spinlock_init(spinlock_t* lock);

unsigned long spinlock_acquire(spinlock_t* lock);

void spinlock_release(spinlock_t* lock, unsigned long rflags);

void mutex_init(mutex_t* mutex);

void mutex_acquire(mutex_t* mutex);

void mutex_release(mutex_t* mutex);

void semaphore_init(semaphore_t* semaphore, int initial_count);

void semaphore_down(semaphore_t* semaphore);

void semaphore_up(semaphore_t* semaphore);
