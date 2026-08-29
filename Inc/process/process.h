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

vma_t* vma_add(process_t* process, vma_t* vma);

bool vma_remove(process_t* process, vma_t* vma);

void vma_free(process_t* process);

void vma_print(process_t* process);

bool vma_add_nullspace(process_t* process, uintptr_t start, uintptr_t end);

bool vma_find_gap(vma_t* vma_head, bool reverse, uintptr_t size,
                  uintptr_t* gap_start);

void kprocess_init(process_t* process);

int load_user_process(process_t** process, const char* elf_path, void* arg);

void sys_getpid(syscall_frame_t* frame);

void sys_brk(syscall_frame_t* frame);

void sys_munmap(syscall_frame_t* frame);

void sys_mmap(syscall_frame_t* frame);

void sys_mprotect(syscall_frame_t* frame);
