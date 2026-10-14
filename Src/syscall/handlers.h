#pragma once

#include <arch/x86_64/syscall.h>

void sys_write(syscall_frame_t* frame);

void sys_read(syscall_frame_t* frame);
