#include <arch/x86_64/syscall.h>
#include <stddef.h>
#include <syscall/syscall.h>
#include <utils/log.h>

// Define a type for our system call functions
typedef uint64_t (*syscall_handler_t)(syscall_frame_t* frame);

uint64_t sys_getpid(syscall_frame_t* frame) {
  // For demonstration purposes, we'll just return a fixed PID.
  // In a real implementation, this would return the actual process ID.
  return 42;  // Example PID
}

uint64_t sys_write(syscall_frame_t* frame) {
  // Extract arguments from the syscall frame
  uint64_t fd = frame->arg1;
  const char* buffer = (const char*)frame->arg2;
  uint64_t count = frame->arg3;

  // For demonstration purposes, we'll just log the write operation.
  // In a real implementation, this would write to the specified file
  // descriptor.
  log_info("sys_write called with fd=%lu, buffer=%p, count=%lu", fd, buffer,
           count);

  // Return the number of bytes written (for demonstration, we return count)
  return count;
}

uint64_t sys_read(syscall_frame_t* frame) { return 0; }

uint64_t sys_exit(syscall_frame_t* frame) { return 0; }

syscall_handler_t syscall_table[] = {
    [SYS_GETPID] = sys_getpid,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = sys_read,
    [SYS_EXIT] = sys_exit,
};

void syscall_init(void* cpu_local_data) { x86_64_syscall_init(cpu_local_data); }

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

  frame->syscall_num = handler(frame);
};
