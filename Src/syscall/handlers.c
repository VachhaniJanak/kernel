#include "handlers.h"

#include <arch/x86_64/syscall.h>
#include <fs/fs.h>
#include <libs/errno.h>
#include <libs/string.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdint.h>
#include <syscall/syscall.h>
#include <utils/log.h>

void sys_write(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;
  const char* buffer = (const char*)frame->arg2;
  size_t count = frame->arg3;

  if (!buffer || count == 0) {
    frame->syscall_num = -EINVAL;  // Invalid argument
    return;
  }

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;  // Bad file descriptor
    return;
  }

  frame->syscall_num = fs_write(file, buffer, count);
}

void sys_read(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;
  void* buf = (void*)frame->arg2;
  size_t count = frame->arg3;

  if (!buf || count == 0) {
    frame->syscall_num = -EINVAL;
    return;
  }

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;  // Bad file descriptor
    return;
  }

  frame->syscall_num = fs_read(file, buf, count);
}

static inline int fs_assign_fd(struct process_s* process, struct file* file) {
  for (int fd = 0; fd < MAX_SYSTEM_OPEN_FILES; fd++) {
    if (process->open_files[fd] == NULL) {
      process->open_files[fd] = file;
      return fd;
    }
  }
  return -1;  // No available file descriptor
}

void sys_open(syscall_frame_t* frame) {
  const char* pathname = (const char*)frame->arg1;
  int flags = (int)frame->arg2;
  int mode = (int)frame->arg3;

  (void)mode;

  if (!pathname) {
    frame->syscall_num = -EINVAL;
    return;
  }

  struct file* file = fs_open(pathname, flags);

  if (!file) {
    frame->syscall_num = -ENOENT;
    return;
  }

  struct process_s* process = scheduler_get_current_process();
  int fd = fs_assign_fd(process, file);

  if (fd < 0) {
    frame->syscall_num = -EMFILE;
    fs_close(file);
    return;
  }

  frame->syscall_num = fd;
}

void sys_close(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;
    return;
  }

  struct process_s* process = current_thread->parent_process;
  process->open_files[fd] = NULL;

  int result = fs_close(file);
  frame->syscall_num = result;
}

void sys_lseek(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;
  off_t offset = (off_t)frame->arg2;
  int whence = (int)frame->arg3;

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;
    return;
  }

  frame->syscall_num = fs_lseek(file, offset, whence);
}

void sys_unlink(syscall_frame_t* frame) {
  const char* pathname = (const char*)frame->arg1;

  if (!pathname) {
    frame->syscall_num = -EINVAL;
    return;
  }

  frame->syscall_num = fs_unlink(pathname);
}

void sys_stat(syscall_frame_t* frame) {
  const char* pathname = (const char*)frame->arg1;
  struct fs_stat* statbuf = (struct fs_stat*)frame->arg2;

  if (!pathname || !statbuf) {
    frame->syscall_num = -EINVAL;
    return;
  }

  struct fs_stat buf;
  int result = fs_stat(pathname, &buf);
  memcpy(statbuf, &buf, sizeof(struct fs_stat));
  frame->syscall_num = result;
}

void sys_fstat(syscall_frame_t* frame) {
  const int fd = (int)frame->arg1;
  struct fs_stat* statbuf = (struct fs_stat*)frame->arg2;

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;
    return;
  }

  struct fs_stat buf;
  int result = fs_fstat(file, &buf);
  memcpy(statbuf, &buf, sizeof(struct fs_stat));
  frame->syscall_num = result;
}

void sys_exit(syscall_frame_t* frame) { 
  sys_pthread_exit(frame); 
}

void sys_kill(syscall_frame_t* frame) {
  int pid = (int)frame->arg1;
  int sig = (int)frame->arg2;

  struct process_s* target_process = scheduler_get_process_by_pid(pid);

  if (!target_process) {
    frame->syscall_num = -ESRCH;
    return;
  }

  // Send the signal to the target process
  send_signal_to_process(target_process, sig);
  frame->syscall_num = 0;  // Success
}

void sys_mkdir(syscall_frame_t* frame) {
  const char* pathname = (const char*)frame->arg1;
  uint32_t mode = (uint32_t)frame->arg2;

  if (!pathname) {
    frame->syscall_num = -EINVAL;
    return;
  }

  frame->syscall_num = fs_mkdir(pathname, mode);
}

void sys_rmdir(syscall_frame_t* frame) {
  const char* pathname = (const char*)frame->arg1;

  if (!pathname) {
    frame->syscall_num = -EINVAL;
    return;
  }

  frame->syscall_num = fs_rmdir(pathname);
}

void sys_getcwd(syscall_frame_t* frame) {
  char* buf = (char*)frame->arg1;
  size_t size = (size_t)frame->arg2;

  if (!buf || size == 0) {
    frame->syscall_num = -EINVAL;
    return;
  }

  process_t* current_process = scheduler_get_current_process();
  int result = fs_make_path(current_process->cwd, buf, size);

  if (result < 0) {
    frame->syscall_num = -ENOMEM;  // Return the error code
    return;
  }

  frame->syscall_num = 0;
}

void sys_chdir(syscall_frame_t* frame) {
  const char* path = (const char*)frame->arg1;

  if (!path || strlen(path) == 0) {
    frame->syscall_num = -EINVAL;
    return;
  }

  if (strcmp(path, ".") == 0) {
    frame->syscall_num = 0;
    return;
  }

  struct dentry* new_cwd_dentry = NULL;
  struct inode* new_cwd_inode = fs_resolve_path(path, &new_cwd_dentry);

  process_t* current_process = scheduler_get_current_process();

  if (!new_cwd_inode || !new_cwd_dentry) {
    frame->syscall_num = -ENOENT;
    return;
  }

  if (!VFS_ISDIR(new_cwd_inode->mode)) {
    fs_dcache_push(new_cwd_dentry);
    frame->syscall_num = -ENOTDIR;
    return;
  }

  fs_dcache_push(current_process->cwd);

  current_process->cwd = new_cwd_dentry;
  frame->syscall_num = 0;  // Success
}

void sys_fchdir(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;  // Bad file descriptor
    return;
  }

  if (!file->inode || !VFS_ISDIR(file->inode->mode)) {
    frame->syscall_num = -ENOTDIR;  // Not a directory
    return;
  }

  process_t* current_process = scheduler_get_current_process();
  current_process->cwd = file->dentry;

  fs_inc_dentry_refcount(current_process->cwd, 1);
  fs_dcache_push(current_process->cwd);

  frame->syscall_num = 0;  // Success
}

static int dir_capture_fill(void* ctx, const char* name, uint32_t ino,
                            uint32_t type) {
  struct fs_readdir_ctx* c = (struct fs_readdir_ctx*)ctx;

  uint8_t* ptr = c->buffer + c->next_offset;
  struct fs_dirent* d = (struct fs_dirent*)ptr;

  size_t reclen = sizeof(struct fs_dirent) + strlen(name) + 1;

  if (c->next_offset + reclen > c->buffer_size) {
    return 1;
  }

  d->d_ino = ino;
  d->d_off = 0;
  d->d_reclen = reclen;
  d->d_type = (uint8_t)type;
  strcpy(d->d_name, name);

  c->next_offset += d->d_reclen;
  c->bytes_count += reclen;

  return 0;
}

void sys_getdents(syscall_frame_t* frame) {
  int fd = (int)frame->arg1;
  void* buf = (void*)frame->arg2;
  size_t bufsize = (size_t)frame->arg3;

  if (!buf || bufsize == 0) {
    frame->syscall_num = -EINVAL;
    return;
  }

  thread_t* current_thread = scheduler_get_current_thread();
  struct file* file = scheduler_get_fd(current_thread, fd);

  if (!file) {
    frame->syscall_num = -EBADF;  // Bad file descriptor
    return;
  }

  if (!VFS_ISDIR(file->inode->mode)) {
    frame->syscall_num = -ENOTDIR;  // Not a directory
    return;
  }

  struct fs_readdir_ctx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.buffer = buf;
  ctx.buffer_size = bufsize;
  ctx.next_offset = 0;
  ctx.bytes_count = 0;

  int result = fs_readdir(file, dir_capture_fill, &ctx);

  if (result < 0) {
    frame->syscall_num = result;  // Propagate the error code
    return;
  }

  frame->syscall_num = ctx.bytes_count;
}
