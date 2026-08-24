#include <mm/vmm/kheap.h>
#include <process/locks.h>
#include <process/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void spinlock_init(spinlock_t* lock) { lock->locked = 0; }

unsigned long spinlock_acquire(spinlock_t* lock) {
  unsigned long rflags;

  // Save RFLAGS and disable local interrupts
  asm volatile(
      "pushfq\n\t"  // Push RFLAGS onto stack
      "pop %0\n\t"  // Pop into our variable
      "cli"         // Clear Interrupt Flag (disable interrupts)
      : "=r"(rflags)
      :
      : "memory");

  // The Test-and-Test-and-Set Loop
  while (1) {
    int val = 1;

    // Attempt to atomically grab the lock
    asm volatile("xchg %0, %1"
                 : "=r"(val), "+m"(lock->locked)
                 : "0"(val)
                 : "memory");

    if (val == 0) {
      break;
    }

    // Spin locally in cache (Shared state) to prevent Cache Bouncing
    while (lock->locked == 1) {
      // Save power, prevent pipeline flushes
      asm volatile("pause" ::: "memory");
    }
  }

  return rflags;
}

void spinlock_release(spinlock_t* lock, unsigned long rflags) {
  // Release the lock.
  // The empty asm block acts as a compiler barrier, ensuring all
  // our kernel data writes finish BEFORE the lock is opened.
  asm volatile("" ::: "memory");
  lock->locked = 0;

  // Restore the original interrupt state
  asm volatile(
      "push %0\n\t"  // Push our saved RFLAGS to stack
      "popfq"        // Pop stack back into hardware RFLAGS register
      :
      : "r"(rflags)
      : "memory", "cc");
}

static inline void queue_init(wait_list_node_t** head,
                              wait_list_node_t** tail) {
  *head = NULL;
  *tail = NULL;
}

static inline bool is_queue_empty(wait_list_node_t* head) {
  return head == NULL;
}

static bool enqueue_thread(wait_list_node_t** head, wait_list_node_t** tail,
                           thread_t* thread) {
  wait_list_node_t* new_node = NULL;
  new_node = (wait_list_node_t*)kmalloc(sizeof(wait_list_node_t));

  if (new_node == NULL) {
    return false;  // Memory allocation failed
  }

  new_node->thread = thread;
  new_node->next = NULL;

  if (is_queue_empty(*head)) {
    *head = new_node;
    *tail = new_node;
    return true;
  }

  (*tail)->next = new_node;
  *tail = new_node;
  return true;
}

static thread_t* dequeue_thread(wait_list_node_t** head,
                                wait_list_node_t** tail) {
  if (is_queue_empty(*head)) {
    return NULL;
  }

  wait_list_node_t* temp = *head;
  thread_t* thread = temp->thread;

  *head = (*head)->next;

  if (*head == NULL) {
    *tail = NULL;
  }

  kfree(temp);
  return thread;
}

void mutex_init(mutex_t* mutex) {
  spinlock_init(&mutex->internal_lock);
  queue_init(&mutex->wait_queue_head, &mutex->wait_queue_tail);
  mutex->is_locked = 0;
}

void mutex_acquire(mutex_t* mutex) {
  unsigned long flags;

  // Lock the mutex's internal state
  flags = spinlock_acquire(&mutex->internal_lock);

  if (mutex->is_locked == 0) {
    mutex->is_locked = 1;
    spinlock_release(&mutex->internal_lock, flags);
    return;
  }

  // The mutex is already locked, so we need to block the current thread
  thread_t* current_thread = scheduler_get_current_thread();
  enqueue_thread(&mutex->wait_queue_head, &mutex->wait_queue_tail,
                 current_thread);
  current_thread->status = THREAD_BLOCKED;

  spinlock_release(&mutex->internal_lock, flags);
  scheduler_yield();  // Yield to allow other threads to run
}

void mutex_release(mutex_t* mutex) {
  unsigned long flags;

  // Lock the mutex's internal state
  flags = spinlock_acquire(&mutex->internal_lock);

  if (is_queue_empty(mutex->wait_queue_head)) {
    mutex->is_locked = 0;
    spinlock_release(&mutex->internal_lock, flags);
    return;
  }

  // There are threads waiting for the mutex, so we need to wake one up
  thread_t* next_thread =
      dequeue_thread(&mutex->wait_queue_head, &mutex->wait_queue_tail);
  next_thread->status = THREAD_READY;

  spinlock_release(&mutex->internal_lock, flags);
}

void semaphore_init(semaphore_t* semaphore, int initial_count) {
  spinlock_init(&semaphore->internal_lock);
  queue_init(&semaphore->wait_queue_head, &semaphore->wait_queue_tail);
  semaphore->count = initial_count;
}

void semaphore_down(semaphore_t* semaphore) {
  unsigned long flags;
  flags = spinlock_acquire(&semaphore->internal_lock);

  if (semaphore->count > 0) {
    // A resource/signal is available! Consume it.
    semaphore->count--;
    spinlock_release(&semaphore->internal_lock, flags);
    return;
  }

  // No count available. We must go to sleep.
  thread_t* current_thread = scheduler_get_current_thread();

  enqueue_thread(&semaphore->wait_queue_head, &semaphore->wait_queue_tail,
                 current_thread);
  current_thread->status = THREAD_BLOCKED;

  spinlock_release(&semaphore->internal_lock, flags);
  scheduler_yield();  // Yield to allow other threads to run
}

void semaphore_up(semaphore_t* semaphore) {
  unsigned long flags;
  flags = spinlock_acquire(&semaphore->internal_lock);

  if (is_queue_empty(semaphore->wait_queue_head)) {
    semaphore->count++;
    spinlock_release(&semaphore->internal_lock, flags);
    return;
  }

  thread_t* waking_thread =
      dequeue_thread(&semaphore->wait_queue_head, &semaphore->wait_queue_tail);
  waking_thread->status = THREAD_READY;
  spinlock_release(&semaphore->internal_lock, flags);
}
