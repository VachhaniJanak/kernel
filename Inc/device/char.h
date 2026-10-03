#pragma once

#include <stddef.h>
#include <stdint.h>

struct char_device;

struct char_device_operations {
  int (*read)(struct char_device* dev, void* buf, size_t len);
  int (*write)(struct char_device* dev, const void* buf, size_t len);
  int (*open)(struct char_device* dev);
  int (*close)(struct char_device* dev);
};

struct char_device {
  struct char_device_operations* ops;
  void* private_data;  // driver-specific data
};

struct device_operations* device_get_dev_null_fops(void);

struct device_operations* device_get_dev_zero_fops(void);
