#pragma once

#include <stdint.h>
#include <arch/x86_64/syscall.h>


void tty_init(void);

void sys_write(syscall_frame_t* frame);

void sys_read(syscall_frame_t* frame);
