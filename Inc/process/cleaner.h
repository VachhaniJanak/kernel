#pragma once

#include <stdbool.h>

#include "locks.h"
#include "scheduler.h"
#include "waitqueue.h"

struct reaper_state_s {
  spinlock_t lock;
  process_t *head;
  process_t *tail;
  size_t count;
  waitqueue_t wait_queue;
};

void cleaner_init(void);

void cleaner_add_process(process_t *process);

