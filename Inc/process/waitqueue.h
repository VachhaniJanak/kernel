#pragma once

#include <process/locks.h>
#include <stdbool.h>

#include "scheduler.h"

typedef struct {
  thread_queue_t queue;  // The underlying thread queue
} waitqueue_t;

void waitqueue_init(waitqueue_t* wq);

void waitqueue_wait(waitqueue_t* wq);

bool waitqueue_wake(waitqueue_t* wq);
