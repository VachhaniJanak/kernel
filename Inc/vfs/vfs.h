#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <vfs/fat/ff.h>

typedef struct {
  FIL *file; /* File system object */
} vfs_t;

void vfs_list_directory(const char* path);

bool init_vfs(void);

bool create_directory(const char* path);

bool rm_directory(const char* path);

int vfs_open(vfs_t* vfs, const TCHAR* path, BYTE mode);

int vfs_close(vfs_t* vfs);

int vfs_read(vfs_t* vfs, void* buffer, size_t bytes);

int vfs_write(vfs_t* vfs, const void* buffer, size_t bytes);

int vfs_seek(vfs_t* vfs, size_t position);

size_t vfs_get_file_size(vfs_t* vfs);
