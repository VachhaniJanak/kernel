#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "scheduler.h"

#define KERNEL_CS_REGS 0x08
#define KERNEL_SS_REGS 0x10
#define USER_SS_REGS 0x1B
#define USER_CS_REGS 0x23
#define RFLAGS_IF 0x202

bool vma_add(process_t* process, vma_t* vma);

bool vma_remove(process_t* process, vma_t* vma);

void vma_free(process_t* process);

void vma_print(process_t* process);

void kprocess_init(process_t* process);

int load_user_process(process_t** process, const char* elf_path, void* arg);
