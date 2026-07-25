#include <drivers/screen/screen.h>
#include <mm/vmm/kheap.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <vfs/fat/ff.h>
#include <vfs/vfs.h>

bool init_vfs(void) {
  FATFS* FatFs = kmalloc(sizeof(FATFS));
  if (f_mount(FatFs, "", 0) != FR_OK) {
    kprintf("Error mounting filesystem.\n");
    return false;
  }
  return true;
}

bool create_directory(const char* path) {
  FRESULT fr;

  fr = f_mkdir(path);
  if (fr != FR_OK) {
    kprintf("Error creating directory '%s' (error=%d)\n", path, fr);
    return false;
  }

  return true;
}

bool rm_directory(const char* path) {
  FRESULT fr;

  fr = f_unlink(path);
  if (fr != FR_OK) {
    kprintf("Error removing directory '%s' (error=%d)\n", path, fr);
    return false;
  }

  return true;
}

void vfs_list_directory(const char* path) {
  FRESULT res;
  DIR dir;
  FILINFO fno;

  res = f_opendir(&dir, path);
  if (res != FR_OK) {
    kprintf("Unable to open directory '%s' (error=%d)\n", path, res);
    return;
  }

  while (1) {
    res = f_readdir(&dir, &fno);

    if (res != FR_OK) {
      kprintf("Read directory failed (error=%d)\n", res);
      break;
    }

    if (fno.fname[0] == '\0') break;  // End of directory

    if (fno.fattrib & AM_DIR) {
      kprintf("[DIR] %s\n", fno.fname);
    } else {
      kprintf("[FILE] %-20s %lu bytes\n", fno.fname, (unsigned long)fno.fsize);
    }
  }

  f_closedir(&dir);
}

int vfs_open(vfs_t* vfs, const TCHAR* path, BYTE mode) {
  vfs->file = kmalloc(sizeof(FATFS));

  if (vfs->file == NULL) {
    return -1;
  }

  FRESULT fr = f_open(vfs->file, path, mode);

  if (fr != FR_OK) {
    return fr;
  }

  return 0;
}

int vfs_close(vfs_t* vfs) {
  if (vfs->file == NULL) {
    return -1;
  }

  FRESULT fr = f_close(vfs->file);

  if (fr != FR_OK) {
    return fr;
  }

  kfree(vfs->file);
  vfs->file = NULL;

  return 0;
}

int vfs_read(vfs_t* vfs, void* buffer, size_t bytes) {
  if (vfs->file == NULL) {
    return -1;
  }

  UINT bw;
  FRESULT fr = f_read(vfs->file, buffer, bytes, &bw);

  if (fr != FR_OK) {
    return fr;
  }

  return bw;
}

int vfs_write(vfs_t* vfs, const void* buffer, size_t bytes) {
  if (vfs->file == NULL) {
    return -1;
  }

  UINT bw;
  FRESULT fr = f_write(vfs->file, buffer, bytes, &bw);

  if (fr != FR_OK) {
    return fr;
  }

  return bw;
}
