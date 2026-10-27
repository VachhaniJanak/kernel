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

#define WNOHANG 0x00000001
#define WUNTRACED 0x00000002

process_t* kprocess_init(const char* name);

int load_user_process(process_t** process, const char* elf_path, void* arg);

void sys_getpid(syscall_frame_t* frame);

void sys_brk(syscall_frame_t* frame);

void sys_munmap(syscall_frame_t* frame);

void sys_mmap(syscall_frame_t* frame);

void sys_mprotect(syscall_frame_t* frame);

int kprocess_add_basic_fd(process_t* process);

void sys_fork(syscall_frame_t* frame);

void sys_waitpid(syscall_frame_t* frame);

void sys_execve(syscall_frame_t* frame);

void process_add_child(process_t* parent, process_t* child);

void process_remove_child(process_t* parent, process_t* child);

process_t* process_get_child_by_pid(process_t* parent, size_t pid);

void send_signal(thread_t* target, int signal_num);

void send_signal_to_process(process_t* proc, int signal_num);

void terminate_process(process_t* process);

void terminate_current_process(void);
