#pragma once

#include <arch/x86_64/syscall.h>

void sys_write(syscall_frame_t* frame);

void sys_read(syscall_frame_t* frame);

void sys_open(syscall_frame_t* frame);

void sys_close(syscall_frame_t* frame);

void sys_lseek(syscall_frame_t* frame);

void sys_unlink(syscall_frame_t* frame);

void sys_stat(syscall_frame_t* frame);

void sys_fstat(syscall_frame_t* frame);

void sys_exit(syscall_frame_t* frame);

void sys_kill(syscall_frame_t* frame);

void sys_mkdir(syscall_frame_t* frame);

void sys_rmdir(syscall_frame_t* frame);

void sys_getcwd(syscall_frame_t* frame);

void sys_chdir(syscall_frame_t* frame);

void sys_fchdir(syscall_frame_t* frame);

void sys_getdents(syscall_frame_t* frame);
