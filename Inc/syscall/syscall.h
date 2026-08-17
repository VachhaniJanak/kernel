#pragma once

#define ENOSYS 38

enum syscall_number {
  SYS_GETPID = 0,
  SYS_WRITE = 1,
  SYS_READ = 2,
  SYS_EXIT = 3,

  SYS_COUNT
};

void syscall_init(void* cpu_local_data);
