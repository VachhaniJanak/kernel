#pragma once

#include <arch/x86_64/syscall.h>
#include <stdbool.h>
#include <stdint.h>

#include "scheduler.h"

#define KERNEL_CS_REGS 0x08
#define KERNEL_SS_REGS 0x10
#define USER_SS_REGS 0x1B
#define USER_CS_REGS 0x23
#define RFLAGS_IF 0x202

process_t* kprocess_init(const char* name);

int load_user_process(process_t** process, const char* elf_path, void* arg);

void sys_getpid(syscall_frame_t* frame);

void sys_brk(syscall_frame_t* frame);

void sys_munmap(syscall_frame_t* frame);

void sys_mmap(syscall_frame_t* frame);

void sys_mprotect(syscall_frame_t* frame);

int kprocess_add_basic_fd(process_t* process);

void sys_fork(syscall_frame_t* frame);
