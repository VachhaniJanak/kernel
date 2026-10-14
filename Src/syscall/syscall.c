#include <arch/x86_64/syscall.h>
#include <process/process.h>
#include <process/thread.h>
#include <stddef.h>
#include <syscall/syscall.h>
#include <utils/log.h>

#include "handlers.h"

// Define a type for our system call functions
typedef void (*syscall_handler_t)(syscall_frame_t* frame);

syscall_handler_t syscall_table[] = {
    [SYS_GETPID] = sys_getpid,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = sys_read,
    [SYS_PTHREAD_CREATE] = sys_pthread_create,
    [SYS_PTHREAD_JOIN] = sys_pthread_join,
    [SYS_PTHREAD_EXIT] = sys_pthread_exit,
    [SYS_PTHREAD_SLEEP] = sys_pthread_sleep,
    [SYS_MUNMAP] = sys_munmap,
    [SYS_MMAP] = sys_mmap,
    [SYS_BRK] = sys_brk,
    [SYS_MPROTECT] = sys_mprotect,
    [SYS_FORK] = sys_fork,
};

void syscall_handler(syscall_frame_t* frame) {
  const size_t num_entries = sizeof(syscall_table) / sizeof(syscall_handler_t);

  if (frame->syscall_num >= num_entries) {
    frame->syscall_num = -ENOSYS;
    return;
  }

  syscall_handler_t handler = syscall_table[frame->syscall_num];

  if (handler == NULL) {
    frame->syscall_num = -ENOSYS;
    return;
  }

  handler(frame);
};

void syscall_init(void* cpu_local_data) { x86_64_syscall_init(cpu_local_data); }
