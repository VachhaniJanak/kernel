#pragma once

#include <stddef.h>
#include <stdint.h>

#define SPIN_LOCK_ACQUIRE(lock_ptr, flags_var) \
  do {                                         \
    spinlock_acquire(lock_ptr, &(flags_var));  \
  } while (0)

#define SPIN_LOCK_RELEASE(lock_ptr, flags_var) \
  do {                                         \
    spinlock_release(lock_ptr, flags_var);     \
  } while (0)

// forward declarations
typedef struct thread_s thread_t;

typedef struct {
  volatile int locked;  // 0 = free, 1 = locked
} spinlock_t;

typedef struct lock_queue_s {
  thread_t* head;
  thread_t* tail;
  size_t count;
  spinlock_t lock;
} lock_queue_t;

typedef struct mutex_s {
  spinlock_t internal_lock;  // Protects the mutex itself
  int is_locked;             // 0 = free, 1 = taken by someone
  thread_t* owner;           // The thread that currently owns the mutex
  lock_queue_t wait_queue;   // A linked list of waiting threads
  struct mutex_s* prev;      // previous mutex in the thread's list
  struct mutex_s* next;      // next mutex in the thread's list
} mutex_t;

typedef struct semaphore_s {
  spinlock_t internal_lock;  // Protects the counter and wait queue
  int count;                 // The number of available resources/signals
  thread_t* owner;           // The thread that currently owns the semaphore
  lock_queue_t wait_queue;   // A linked list of waiting threads
  struct semaphore_s* prev;  // previous semaphore in the thread's list
  struct semaphore_s* next;  // next semaphore in the thread's list
} semaphore_t;

void spinlock_init(spinlock_t* lock);

void spinlock_acquire(spinlock_t* lock, unsigned long* save_rflags);

void spinlock_release(spinlock_t* lock, unsigned long rflags);

void mutex_init(mutex_t* mutex);

void mutex_acquire(mutex_t* mutex);

void mutex_release(mutex_t* mutex);

void semaphore_init(semaphore_t* semaphore, int initial_count);

void semaphore_down(semaphore_t* semaphore);

void semaphore_up(semaphore_t* semaphore);
