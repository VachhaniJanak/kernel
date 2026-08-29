#pragma once

#define ENOSYS 38

enum syscall_number {
  SYS_GETPID = 0,
  SYS_WRITE = 1,
  SYS_READ = 2,
  SYS_PTHREAD_EXIT = 3,
  SYS_PTHREAD_CREATE = 4,
  SYS_PTHREAD_JOIN = 5,
  SYS_PTHREAD_SLEEP = 7,
  SYS_MUNMAP = 8,
  SYS_MMAP = 9,
  SYS_BRK = 10,
  SYS_MPROTECT = 11,
  SYS_COUNT
};

void syscall_init(void* cpu_local_data);
