#include <device/device.h>
#include <libs/string.h>
#include <mm/vmm/kheap.h>
#include <utils/log.h>
#include <utils/printf.h>

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static struct device *device_list_head;
static uint32_t next_device_id;

void generate_disk_name(int partition, char *buffer, size_t buffer_size) {
  static uint8_t disk_counter = 0;

  if (partition < 0) {
    snprintf(buffer, buffer_size, "disk%d", disk_counter++);
    return;
  }

  snprintf(buffer, buffer_size, "disk%dp%d", disk_counter - 1, partition);
}

int device_registry_init(void) {
  device_list_head = NULL;
  next_device_id = 1; // Reset device IDs
  return 0;
}

struct device *device_registry_get_list(void) { return device_list_head; }

int device_registry_register(void *dev, enum device_type type) {
  if (!dev) {
    return -1;
  }

  struct device *new_device = kmalloc(sizeof(struct device));

  if (!new_device) {
    return -1;
  }

  memset(new_device, 0, sizeof(struct device));

  new_device->device_id =
      __atomic_fetch_add(&next_device_id, 1, __ATOMIC_SEQ_CST);
  new_device->type = type;
  new_device->dev = dev;
  new_device->prev = NULL;
  new_device->next = device_list_head;

  if (device_list_head) {
    device_list_head->prev = new_device;
  }

  device_list_head = new_device;

  return new_device->device_id;
}

int device_registry_unregister(uint32_t device_id) {
  struct device *current = device_list_head;

  while (current) {
    if (current->device_id == device_id) {
      if (current->prev) {
        current->prev->next = current->next;
      } else {
        device_list_head = current->next;
      }

      if (current->next) {
        current->next->prev = current->prev;
      }

      kfree(current);
      return 0;
    }
    current = current->next;
  }

  return -1; // Device not found
}

int device_registry_register_child(struct device *parent_device,
                                   void *child_dev,
                                   enum device_type child_type) {

  if (!parent_device) {
    return -1; // Parent device not found
  }

  struct device *new_child = kmalloc(sizeof(struct device));

  if (!new_child) {
    return -1; // Memory allocation failed
  }

  memset(new_child, 0, sizeof(struct device));

  new_child->device_id =
      __atomic_fetch_add(&next_device_id, 1, __ATOMIC_SEQ_CST);
  new_child->type = child_type;
  new_child->dev = child_dev;
  new_child->parent = parent_device;

  // Insert the new child at the end of the parent's child list
  if (!parent_device->child) {
    parent_device->child = new_child;
  } else {
    struct device *current_child = parent_device->child;
    
    while (current_child->next) {
      current_child = current_child->next;
    }

    current_child->next = new_child;
    new_child->prev = current_child;
    new_child->next = NULL;
  }

  return new_child->device_id;
}

int device_registry_unregister_child(struct device *parent_device,
                                     uint32_t child_device_id) {
  if (!parent_device) {
    return -1; // Parent device not found
  }

  struct device *current_child = parent_device->child;

  while (current_child) {
    if (current_child->device_id == child_device_id) {
      if (current_child->prev) {
        current_child->prev->next = current_child->next;
      } else {
        parent_device->child = current_child->next;
      }

      if (current_child->next) {
        current_child->next->prev = current_child->prev;
      }

      kfree(current_child);
      return 0; // Child device unregistered successfully
    }
    current_child = current_child->next;
  }

  return -1; // Child device not found
}

struct device *device_registry_find(uint32_t device_id) {
  struct device *current = device_list_head;

  while (current) {
    if (current->device_id == device_id) {
      return current;
    }
    current = current->next;
  }

  return NULL; // Device not found
}

static void print_childern(struct device *parent){
  struct device *current_child = parent->child;

  while (current_child) {
    log_print("    Device ID: %u, Type: %s\n", current_child->device_id,
              current_child->type == DEVICE_TYPE_BLOCK  ? "Block"
              : current_child->type == DEVICE_TYPE_CHAR ? "Char"
                                                        : "Unknown");
    current_child = current_child->next;
  }
}

void device_debug_print_list(void) {
  struct device *current = device_list_head;
  log_print("Device List:\n");

  while (current) {
    log_print("  Device ID: %u, Type: %s\n", current->device_id,
              current->type == DEVICE_TYPE_BLOCK  ? "Block"
              : current->type == DEVICE_TYPE_CHAR ? "Char"
                                                  : "Unknown");
    print_childern(current);
    current = current->next;
  }

  log_newline();
}
