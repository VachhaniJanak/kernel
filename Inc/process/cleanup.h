#pragma once

#include <stdbool.h>

#include "locks.h"
#include "scheduler.h"

struct reaper_node_s {
  struct reaper_node_s* next;
  struct process_s* process;
};

struct reaper_state_s {
  spinlock_t reaper_lock;
  struct reaper_node_s* head;
  struct reaper_node_s* tail;
};

void cleanup_init(void);

bool cleanup_add_process(process_t* process);
