#pragma once

#define ENOSYS 38

enum syscall_number {
  SYS_GETPID = 0,
  SYS_WRITE,
  SYS_READ,
  SYS_PTHREAD_EXIT,
  SYS_PTHREAD_CREATE,
  SYS_PTHREAD_JOIN,
  SYS_PTHREAD_SLEEP,
  SYS_MUNMAP,
  SYS_MMAP,
  SYS_BRK,
  SYS_MPROTECT,
  SYS_COUNT,
  SYS_FORK
};

void syscall_init(void* cpu_local_data);
