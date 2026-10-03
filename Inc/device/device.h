#pragma once

#include <stdint.h>
#include <stddef.h>

enum device_type { DEVICE_TYPE_BLOCK, DEVICE_TYPE_CHAR, DEVICE_TYPE_UNKNOWN };

struct device {
  uint32_t device_id; // Unique identifier for the device
  enum device_type type;
  void *dev; // Pointer to the actual device structure (block or char)
  struct device *prev;
  struct device *next;
  struct device *child;
  struct device *parent;
};

void generate_disk_name(int partition, char *buffer, size_t buffer_size);

int device_registry_init(void);

struct device *device_registry_get_list(void);

int device_registry_register(void *dev, enum device_type type);

int device_registry_unregister(uint32_t device_id);

int device_registry_register_child(struct device *parent_device,
                                   void *child_dev,
                                   enum device_type child_type);

int device_registry_unregister_child(struct device *parent_device,
                                     uint32_t child_device_id);

struct device *device_registry_find(uint32_t device_id);

void device_debug_print_list(void);
