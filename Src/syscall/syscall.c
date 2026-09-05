#include <arch/x86_64/syscall.h>
#include <process/process.h>
#include <process/thread.h>
#include <stddef.h>
#include <syscall/syscall.h>
#include <tty/tty.h>
#include <utils/log.h>

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
};

void syscall_handler(syscall_frame_t* frame) {
  // log_debug("Syscall invoked: %lu\n", frame->syscall_num);

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

  uint64_t saved_syscall_num = frame->syscall_num;
  handler(frame);
  // log_debug("Syscall %lu executed successfully\n", saved_syscall_num);
};

void syscall_init(void* cpu_local_data) { x86_64_syscall_init(cpu_local_data); }
