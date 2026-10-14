#include "handlers.h"

#include <arch/x86_64/syscall.h>
#include <fs/fs.h>
#include <process/scheduler.h>
#include <stdint.h>
#include <syscall/syscall.h>
#include <utils/log.h>

void sys_write(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;
  const char* buffer = (const char*)frame->arg2;
  size_t count = frame->arg3;

  if (!buffer || count == 0) {
    frame->syscall_num = -1;
    return;
  }

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -1;
    return;
  }

  frame->syscall_num = fs_write(file, buffer, count);
}

void sys_read(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;
  void* buf = (void*)frame->arg2;
  size_t count = frame->arg3;

  if (!buf || count == 0) {
    frame->syscall_num = -1;
    return;
  }

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -1;
    return;
  }

  frame->syscall_num = fs_read(file, buf, count);
}
