#include <process/locks.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <process/waitqueue.h>
#include <stdint.h>

// Initialize an empty wait queue
void waitqueue_init(waitqueue_t* wq) {
  spinlock_init(&wq->lock);

  wq->head = NULL;
  wq->tail = NULL;
}

void waitqueue_wait(waitqueue_t* wq) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&wq->lock, flags);

  thread_t* current = scheduler_get_current_thread();

  // Add this thread to the end of the wait queue
  current->next_waiting = NULL;

  if (wq->tail == NULL) {
    wq->head = current;
    wq->tail = current;
  } else {
    wq->tail->next_waiting = current;
    wq->tail = current;
  }

  current->status = THREAD_BLOCKED;
  SPIN_LOCK_RELEASE(&wq->lock, flags);

  // Yield the CPU to another process.
  scheduler_yield();
}

void waitqueue_wake(waitqueue_t* wq) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&wq->lock, flags);

  if (wq->head == NULL) {
    SPIN_LOCK_RELEASE(&wq->lock, flags);
    return;
  }

  // Pop the first thread off the wait queue
  thread_t* woken_thread = wq->head;
  wq->head = woken_thread->next_waiting;

  if (wq->head == NULL) {
    wq->tail = NULL;
  }

  woken_thread->next_waiting = NULL;

  // Wake it up and put it back in the scheduler
  woken_thread->status = THREAD_READY;
  SPIN_LOCK_RELEASE(&wq->lock, flags);
}
