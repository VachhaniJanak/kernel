#pragma once

#include <scheduler/scheduler.h>
#include <stdbool.h>
#include <stdint.h>

bool vma_add(process_t* process, vma_t* vma);

bool vma_remove(process_t* process, vma_t* vma);
