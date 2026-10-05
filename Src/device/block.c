#include <device/block.h>
#include <fs/devfs/devfs.h>
#include <mm/kheap/kheap.h>
#include <libs/string.h>

#include <stddef.h>
#include <stdint.h>

static int block_partition_write(struct block_device* partition,
                                 const void* buffer, size_t lba, size_t count);

static int block_partition_read(struct block_device* partition, void* buffer,
                                size_t lba, size_t count);

static int blockdev_write(struct devfs_device* dev, const void* buffer,
                          size_t length, size_t* position);

static int blockdev_read(struct devfs_device* dev, void* buffer, size_t length,
                         size_t* position);

static struct block_device_operations block_partition_ops = {
    .read_sectors = block_partition_read,
    .write_sectors = block_partition_write,
};

static struct device_operations blockdev_fops = {
    .read = blockdev_read,
    .write = blockdev_write,
};

struct block_device_operations *block_partition_get_ops(void) {
  return &block_partition_ops;
}

struct device_operations *blockdev_get_fops(void) {
  return &blockdev_fops;
}

static int block_partition_write(struct block_device* partition,
                                 const void* buffer, size_t lba, size_t count) {
  if (!partition || !partition->parent) {
    return -1;
  }

  if (!buffer && count != 0) {
    return -1;
  }

  if (lba > partition->sector_count) {
    return -1;
  }

  if (count > partition->sector_count - lba) {
    return -1;
  }

  if (partition->start_lba > SIZE_MAX - lba) {
    return -1;
  }

  size_t parent_lba = partition->start_lba + lba;

  return partition->parent->ops->write_sectors(partition->parent, buffer,
                                               parent_lba, count);
}

static int block_partition_read(struct block_device* partition, void* buffer,
                                size_t lba, size_t count) {
  if (!partition || !partition->parent) {
    return -1;
  }

  if (!buffer && count != 0) {
    return -1;
  }

  if (lba > partition->sector_count) {
    return -1;
  }

  if (count > partition->sector_count - lba) {
    return -1;
  }

  if (partition->start_lba > SIZE_MAX - lba) {
    return -1;
  }

  size_t parent_lba = partition->start_lba + lba;

  return partition->parent->ops->read_sectors(partition->parent, buffer,
                                              parent_lba, count);
}

static int blockdev_write(struct devfs_device* dev, const void* buffer,
                          size_t length, size_t* position) {
  if (!dev || !buffer || !position) {
    return -1;
  }

  struct block_device* device = dev->private_data;

  if (!device || !device->ops) {
    return -1;
  }

  if (length == 0) {
    return 0;
  }

  if (*position > SIZE_MAX - length) {
    return -1;
  }

  size_t device_size = device->sector_count * device->sector_size;

  if (*position > device_size || length > device_size - *position) {
    return -1;
  }

  // if not aligned to sector size
  if ((*position % device->sector_size) != 0 ||
      (length % device->sector_size) != 0) {
    void* temp_buffer = kmalloc(device->sector_size);

    if (!temp_buffer) {
      return -1;
    }

    size_t start_lba = *position / device->sector_size;
    size_t end_lba = (*position + length - 1) / device->sector_size;
    size_t total_sectors = end_lba - start_lba + 1;

    for (size_t i = 0; i < total_sectors; i++) {
      size_t current_lba = start_lba + i;
      size_t current_offset = current_lba * device->sector_size;

      // Read the existing sector data
      int read_result =
          device->ops->read_sectors(device, temp_buffer, current_lba, 1);

      if (read_result != 0) {
        kfree(temp_buffer);
        return -1;
      }

      // Calculate the range of bytes to overwrite in this sector
      size_t sector_start = current_offset;
      size_t sector_end = current_offset + device->sector_size;

      size_t write_start =
          (*position > sector_start) ? *position : sector_start;
      size_t write_end = ((*position + length) < sector_end)
                             ? (*position + length)
                             : sector_end;

      // Copy the new data into the temp buffer
      memcpy((uint8_t*)temp_buffer + (write_start - sector_start),
             (const uint8_t*)buffer + (write_start - *position),
             write_end - write_start);

      // Write the modified sector back to the device
      int write_result =
          device->ops->write_sectors(device, temp_buffer, current_lba, 1);

      if (write_result != 0) {
        kfree(temp_buffer);
        return -1;
      }
    }

    kfree(temp_buffer);
    *position += length;
    return (int)length;
  }

  size_t lba = *position / device->sector_size;
  size_t sector_count = length / device->sector_size;

  int result = device->ops->write_sectors(device, buffer, lba, sector_count);

  if (result != 0) {
    return -1;
  }

  *position += length;

  return (int)length;
}

static int blockdev_read(struct devfs_device* dev, void* buffer, size_t length,
                         size_t* position) {
  if (!dev || !buffer || !position) {
    return -1;
  }

  struct block_device* device = dev->private_data;

  if (!device || !device->ops) {
    return -1;
  }

  if (length == 0) {
    return 0;
  }

  if (*position > SIZE_MAX - length) {
    return -1;
  }

  size_t device_size = device->sector_count * device->sector_size;

  if (*position > device_size || length > device_size - *position) {
    return -1;
  }

  // if not aligned to sector size
  if ((*position % device->sector_size) != 0 ||
      (length % device->sector_size) != 0) {
    void* temp_buffer = kmalloc(device->sector_size);

    if (!temp_buffer) {
      return -1;
    }

    size_t start_lba = *position / device->sector_size;
    size_t end_lba = (*position + length - 1) / device->sector_size;
    size_t total_sectors = end_lba - start_lba + 1;

    for (size_t i = 0; i < total_sectors; i++) {
      size_t current_lba = start_lba + i;
      size_t current_offset = current_lba * device->sector_size;

      // Read the existing sector data
      int read_result =
          device->ops->read_sectors(device, temp_buffer, current_lba, 1);

      if (read_result != 0) {
        kfree(temp_buffer);
        return -1;
      }

      // Calculate the range of bytes to copy from this sector
      size_t sector_start = current_offset;
      size_t sector_end = current_offset + device->sector_size;

      size_t read_start = (*position > sector_start) ? *position : sector_start;
      size_t read_end = ((*position + length) < sector_end)
                            ? (*position + length)
                            : sector_end;

      // Copy the relevant data from the temp buffer to the output buffer_size
      memcpy((uint8_t*)buffer + (read_start - *position),
             (uint8_t*)temp_buffer + (read_start - sector_start),
             read_end - read_start);
    }

    kfree(temp_buffer);
    *position += length;
    return (int)length;
  }

  size_t lba = *position / device->sector_size;
  size_t sector_count = length / device->sector_size;

  int result = device->ops->read_sectors(device, buffer, lba, sector_count);

  if (result != 0) {
    return -1;
  }

  *position += length;

  return (int)length;
}
