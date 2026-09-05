#pragma once

#include <process/locks.h>
#include <process/thread.h>

typedef struct {
  thread_t* head;
  thread_t* tail;
  spinlock_t lock;
} waitqueue_t;

void waitqueue_init(waitqueue_t* wq);

void waitqueue_wait(waitqueue_t* wq);

void waitqueue_wake(waitqueue_t* wq);
