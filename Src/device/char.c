#include <device/char.h>
#include <fs/devfs/devfs.h>
#include <libs/string.h>
#include <stddef.h>
#include <stdint.h>

static int dev_null_read(struct devfs_device* dev, void* buffer, size_t length,
                         size_t* off);

static int dev_null_write(struct devfs_device* dev, const void* buffer,
                          size_t length, size_t* off);

static int dev_zero_read(struct devfs_device* dev, void* buffer, size_t length,
                         size_t* off);

static int dev_zero_write(struct devfs_device* dev, const void* buffer,
                          size_t length, size_t* off);

static struct device_operations dev_null_fops = {
    .read = dev_null_read,
    .write = dev_null_write,
};

static struct device_operations dev_zero_fops = {
    .read = dev_zero_read,
    .write = dev_zero_write,
};

struct device_operations* device_get_dev_null_fops(void) {
  return &dev_null_fops;
}

struct device_operations* device_get_dev_zero_fops(void) {
  return &dev_zero_fops;
}

static int dev_null_read(struct devfs_device* dev, void* buffer, size_t length,
                         size_t* off) {
  (void)dev;
  (void)buffer;
  (void)length;
  (void)off;

  return 0;
}

static int dev_null_write(struct devfs_device* dev, const void* buffer,
                          size_t length, size_t* off) {
  (void)dev;
  (void)buffer;
  (void)off;

  return (size_t)length;
}

static int dev_zero_read(struct devfs_device* dev, void* buffer, size_t length,
                         size_t* off) {
  (void)dev;
  (void)off;

  if (length > 0 && !buffer) {
    return -1;
  }

  memset(buffer, 0, length);
  return (int)length;
}

static int dev_zero_write(struct devfs_device* dev, const void* buffer,
                          size_t length, size_t* off) {
  (void)dev;
  (void)buffer;
  (void)off;

  return (int)length;
}
