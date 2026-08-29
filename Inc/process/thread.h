#pragma once

#include <arch/x86_64/syscall.h>
#include <stdint.h>

#include "scheduler.h"

int kthread_create(const char* name, thread_t** thread,
                   void (*entry_point)(void*), void* arg);

void kthread_sleep(size_t milliseconds);

int kthread_join(thread_t* child_thread);

bool pthread_init(process_t* process, const char* name, thread_t* thread,
                  void* pstack, void (*entry_point)(void*), void* arg);

void sys_pthread_sleep(syscall_frame_t* frame);

void sys_pthread_exit(syscall_frame_t* frame);

void sys_pthread_create(syscall_frame_t* frame);

void sys_pthread_join(syscall_frame_t* frame);
