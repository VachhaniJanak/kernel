#pragma once

#include <fs/fs.h>
#include <stddef.h>
#include <stdint.h>

enum block_device_type {
  BLOCK_DEVICE_DISK,
  BLOCK_DEVICE_PARTITION,
};

struct block_device;

struct block_device_operations {
  int (*read_sectors)(struct block_device *dev, void *buffer, size_t lba,
                      size_t sector_count);
  int (*write_sectors)(struct block_device *dev, const void *buffer, size_t lba,
                       size_t sector_count);
};

struct block_device {
  enum block_device_type type;
  struct block_device *parent;
  size_t start_lba;
  size_t sector_count;
  uint32_t sector_size;
  struct block_device_operations *ops;
  void *private_data; // driver-specific data
  enum filesystem_type fs_type;
};

struct block_device_operations *block_partition_get_ops(void);

struct device_operations *blockdev_get_fops(void);


