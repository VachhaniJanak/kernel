#include <arch/x86_64/syscall.h>
#include <libs/errno.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stddef.h>
#include <syscall/syscall.h>
#include <syscall/unistd_64.h>
#include <utils/log.h>

#include "handlers.h"

// Define a type for our system call functions
typedef void (*syscall_handler_t)(syscall_frame_t* frame);

syscall_handler_t syscall_table[] = {
    [__NR_exit] = sys_exit,
    [__NR_kill] = sys_kill,
    [__NR_fork] = sys_fork,
    [__NR_execve] = sys_execve,
    [__NR_wait4] = sys_waitpid,
    [__NR_getpid] = sys_getpid,
    [__NR_munmap] = sys_munmap,
    [__NR_mmap] = sys_mmap,
    [__NR_brk] = sys_brk,
    [__NR_mprotect] = sys_mprotect,
    [__NR_open] = sys_open,
    [__NR_close] = sys_close,
    [__NR_write] = sys_write,
    [__NR_read] = sys_read,
    [__NR_lseek] = sys_lseek,
    [__NR_stat] = sys_stat,
    [__NR_fstat] = sys_fstat,
    [__NR_link] = NULL,
    [__NR_unlink] = sys_unlink,
    [__NR_symlink] = NULL,
    [__NR_readlink] = NULL,
    [__NR_chown] = NULL,
    [__NR_mkdir] = sys_mkdir,
    [__NR_rmdir] = sys_rmdir,
    [__NR_chdir] = sys_chdir,
    [__NR_fchdir] = sys_fchdir,
    [__NR_getcwd] = sys_getcwd,
    [__NR_rename] = NULL,
    [__NR_getdents64] = sys_getdents,
    [__NR_getdents] = sys_getdents,
};

void syscall_handler(syscall_frame_t* frame) {
  // log_debug("Syscall number: %lu", frame->syscall_num);
  const size_t num_entries = sizeof(syscall_table) / sizeof(syscall_handler_t);

  if (frame->syscall_num >= num_entries) {
    frame->syscall_num = -ENOSYS;
    return;
  }

  syscall_handler_t handler = syscall_table[frame->syscall_num];

  if (!handler) {
    log_error("Invalid syscall number: %lu", frame->syscall_num);
    frame->syscall_num = -ENOSYS;
    return;
  }

  thread_t* thread = scheduler_get_current_thread();

  thread->status = TASK_SYSCALL_WAIT;

  handler(frame);

  thread->status = TASK_RUNNING;
  // log_debug("Syscall completed");
};

void syscall_init(void* cpu_local_data) { x86_64_syscall_init(cpu_local_data); }
