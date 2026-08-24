#pragma once

#include <stdint.h>

#include "scheduler.h"

int kthread_create(const char* name, thread_t** thread,
                   void (*entry_point)(void*), void* arg);

void kthread_sleep(size_t milliseconds);

int kthread_join(thread_t* thread);

bool user_thread_init(process_t* process, const char* name, thread_t* thread,
                      void (*entry_point)(void*), void* arg);

int user_thread_create(process_t* process, const char* name, thread_t** thread,
                       void (*entry_point)(void*), void* arg);
