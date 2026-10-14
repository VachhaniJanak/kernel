#include <libs/string.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <process/waitqueue.h>
#include <stdbool.h>
#include <stdint.h>

// Initialize an empty wait queue
void waitqueue_init(waitqueue_t* wq) {
  memset(wq, 0, sizeof(waitqueue_t));
  spinlock_init(&wq->queue.lock);
}


void waitqueue_wait(waitqueue_t* wq) {
  thread_t* current = scheduler_get_current_thread();

  scheduler_queue_push(&wq->queue, current, TASK_BLOCKED);
  
  current->wait_queue = &wq->queue;  // Set the wait queue reference

  scheduler_yield();
}

bool waitqueue_wake(waitqueue_t* wq) {
  thread_t* thread_to_wake = scheduler_queue_pop(&wq->queue, TASK_READY);

  if (!thread_to_wake) {
    return false;  // No thread to wake
  }

  thread_to_wake->wait_queue = NULL;  // Clear the wait queue reference
  thread_queue_t* ready_queue = &scheduler_state.ready_queue;

  scheduler_queue_push(ready_queue, thread_to_wake, TASK_READY);
  return true;
}
