#include <process/locks.h>
#include <process/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void spinlock_init(spinlock_t* lock) { lock->locked = 0; }

void spinlock_acquire(spinlock_t* lock, unsigned long* save_rflags) {
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

  *save_rflags = rflags;
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

static void enqueue_thread(lock_queue_t* queue, thread_t* thread,
                           task_status_t status) {
  thread->queue_next = NULL;
  thread->queue_prev = queue->tail;
  thread->status = status;

  if (queue->tail) {
    queue->tail->queue_next = thread;
  } else {
    queue->head = thread;
  }

  queue->tail = thread;
  queue->count++;
}

static thread_t* dequeue_thread(lock_queue_t* queue, task_status_t status) {
  if (!queue->head) {
    return NULL;
  }

  thread_t* thread = queue->head;
  queue->head = thread->queue_next;

  if (queue->head) {
    queue->head->queue_prev = NULL;
  } else {
    queue->tail = NULL;
  }

  thread->status = status;
  thread->queue_prev = NULL;
  thread->queue_next = NULL;

  queue->count--;
  return thread;
}

void mutex_init(mutex_t* mutex) {
  mutex->is_locked = 0;
  mutex->owner = NULL;
  mutex->wait_queue.head = NULL;
  mutex->wait_queue.tail = NULL;
  mutex->wait_queue.count = 0;
  mutex->prev = NULL;
  mutex->next = NULL;

  spinlock_init(&mutex->internal_lock);
}

static void add_mutex_to_thread(thread_t* thread, mutex_t* mutex) {
  // Add this mutex to the current thread's list of held mutexes
  mutex->prev = NULL;
  mutex->next = thread->mutex_locks_head;

  if (thread->mutex_locks_head) {
    thread->mutex_locks_head->prev = mutex;
  }

  thread->mutex_locks_head = mutex;
}

static void remove_mutex_from_thread(mutex_t* mutex) {
  // Remove this mutex from the current thread's list of held mutexes
  thread_t* current_thread = mutex->owner;

  if (mutex->prev) {
    mutex->prev->next = mutex->next;
  } else {
    current_thread->mutex_locks_head = mutex->next;
  }

  if (mutex->next) {
    mutex->next->prev = mutex->prev;
  }

  mutex->prev = NULL;
  mutex->next = NULL;
}

void mutex_acquire(mutex_t* mutex) {
  unsigned long flags;

  // Lock the mutex's internal state
  spinlock_acquire(&mutex->internal_lock, &flags);

  thread_t* current_thread = scheduler_get_current_thread();

  if (mutex->is_locked == 0) {
    mutex->is_locked = 1;
    mutex->owner = current_thread;

    add_mutex_to_thread(current_thread, mutex);
    spinlock_release(&mutex->internal_lock, flags);
    return;
  }

  // The mutex is already locked by another thread
  current_thread->wait_queue = (thread_queue_t*)&mutex->wait_queue;

  enqueue_thread(&mutex->wait_queue, current_thread, TASK_BLOCKED);
  spinlock_release(&mutex->internal_lock, flags);
  scheduler_yield();  // Yield to allow other threads to run
}

void mutex_release(mutex_t* mutex) {
  unsigned long flags;

  // Lock the mutex's internal state
  spinlock_acquire(&mutex->internal_lock, &flags);
  remove_mutex_from_thread(mutex);

  if (mutex->wait_queue.count == 0) {
    mutex->is_locked = 0;
    mutex->owner = NULL;

    spinlock_release(&mutex->internal_lock, flags);
    return;
  }

  // There are threads waiting for the mutex
  thread_t* next_thread = dequeue_thread(&mutex->wait_queue, TASK_READY);
  mutex->owner = next_thread;  // Transfer ownership to the next thread
  next_thread->wait_queue = NULL;

  // Add it to the next thread
  add_mutex_to_thread(next_thread, mutex);
  scheduler_queue_push(&scheduler_state.ready_queue, next_thread, TASK_READY);
  spinlock_release(&mutex->internal_lock, flags);
}

void semaphore_init(semaphore_t* semaphore, int initial_count) {
  semaphore->count = initial_count;
  semaphore->owner = NULL;
  semaphore->wait_queue.head = NULL;
  semaphore->wait_queue.tail = NULL;
  semaphore->wait_queue.count = 0;
  semaphore->prev = NULL;
  semaphore->next = NULL;

  spinlock_init(&semaphore->internal_lock);
}

static void add_semaphore_to_thread(thread_t* thread, semaphore_t* semaphore) {
  // Add this semaphore to the current thread's list of held semaphores
  semaphore->prev = NULL;
  semaphore->next = thread->semaphore_locks_head;

  if (thread->semaphore_locks_head) {
    thread->semaphore_locks_head->prev = semaphore;
  }

  thread->semaphore_locks_head = semaphore;
}

static void remove_semaphore_from_thread(semaphore_t* semaphore) {
  // Remove this semaphore from the current thread's list of held semaphores
  thread_t* current_thread = semaphore->owner;

  if (semaphore->prev) {
    semaphore->prev->next = semaphore->next;
  } else {
    current_thread->semaphore_locks_head = semaphore->next;
  }

  if (semaphore->next) {
    semaphore->next->prev = semaphore->prev;
  }

  semaphore->prev = NULL;
  semaphore->next = NULL;
}

void semaphore_down(semaphore_t* semaphore) {
  unsigned long flags;
  spinlock_acquire(&semaphore->internal_lock, &flags);

  thread_t* current_thread = scheduler_get_current_thread();

  if (semaphore->count > 0) {
    // A resource/signal is available! Consume it.
    semaphore->count--;
    semaphore->owner = current_thread;

    add_semaphore_to_thread(current_thread, semaphore);
    spinlock_release(&semaphore->internal_lock, flags);
    return;
  }

  // No count available. We must go to sleep.
  current_thread->wait_queue = (thread_queue_t*)&semaphore->wait_queue;

  enqueue_thread(&semaphore->wait_queue, current_thread, TASK_BLOCKED);
  spinlock_release(&semaphore->internal_lock, flags);
  scheduler_yield();  // Yield to allow other threads to run
}

void semaphore_up(semaphore_t* semaphore) {
  unsigned long flags;

  spinlock_acquire(&semaphore->internal_lock, &flags);
  remove_semaphore_from_thread(semaphore);

  if (semaphore->wait_queue.count == 0) {
    semaphore->count++;
    semaphore->owner = NULL;  // No thread owns the semaphore now

    spinlock_release(&semaphore->internal_lock, flags);
    return;
  }

  thread_t* waking_thread = dequeue_thread(&semaphore->wait_queue, TASK_READY);
  semaphore->owner = waking_thread;  // Transfer ownership to the waking thread
  waking_thread->wait_queue = NULL;

  // Add it to the waking thread
  add_semaphore_to_thread(waking_thread, semaphore);
  scheduler_queue_push(&scheduler_state.ready_queue, waking_thread, TASK_READY);
  spinlock_release(&semaphore->internal_lock, flags);
}
