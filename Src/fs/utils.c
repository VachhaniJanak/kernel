#include "utils.h"

#include <boot/boot.h>
#include <device/block.h>
#include <device/char.h>
#include <device/device.h>
#include <fs/devfs/devfs.h>
#include <fs/fat/fat32.h>
#include <fs/fs.h>
#include <fs/prt/gpt.h>
#include <libs/string.h>
#include <mm/vmm/kheap.h>
#include <stddef.h>
#include <stdint.h>
#include <tty/tty.h>
#include <utils/log.h>

struct gpt_partition_ctx {
  struct device* parent_device;
  void* buffer;
  int count;
  int kernel_partition_index;
  enum filesystem_type kernel_partition_fs_type;
  struct block_device* kernel_partition_device;
};

static uint8_t efi_system_partition_guid[16] = EFI_SYSTEM_PARTITION_GUID;
static uint8_t microsoft_basic_data_guid[16] = MICROSOFT_BASIC_DATA_GUID;
static uint8_t linux_filesystem_guid[16] = LINUX_FILESYSTEM_GUID;
static uint8_t linux_swap_guid[16] = LINUX_SWAP_GUID;

static inline enum filesystem_type probe_microsoft_fs(void* buffer) {
  // check for fat32 signature at offset 0x52
  struct fat32_bpb* bpb = (struct fat32_bpb*)buffer;

  if (memcmp(bpb->fs_type, "FAT16   ", 8) == 0) {
    return FS_TYPE_FAT16;
  }

  if (memcmp(bpb->fs_type, "FAT32   ", 8) == 0) {
    return FS_TYPE_FAT32;
  }

  return FS_TYPE_UNKNOWN;
}

static inline enum filesystem_type probe_linux_fs(void* buffer) {
  // TODO: Implement Linux filesystem detection
  (void)buffer;  // Suppress unused parameter warning
  return FS_TYPE_UNKNOWN;
}

static inline enum filesystem_type gpt_partition_probe_fs(
    void* buffer, gpt_partition_entry_t* entry) {
  if (!entry) {
    return FS_TYPE_UNKNOWN;
  }

  if (memcmp(entry->partition_type_guid, efi_system_partition_guid,
             sizeof(efi_system_partition_guid)) == 0) {
    return probe_microsoft_fs(buffer);
  }

  // check is partition is microsoft basic data partition type
  if (memcmp(entry->partition_type_guid, microsoft_basic_data_guid,
             sizeof(microsoft_basic_data_guid)) == 0) {
    return probe_microsoft_fs(buffer);
  }

  // check is partition is linux filesystem partition type
  if (memcmp(entry->partition_type_guid, linux_filesystem_guid,
             sizeof(linux_filesystem_guid)) == 0) {
    return probe_linux_fs(buffer);
  }

  return FS_TYPE_UNKNOWN;
}

static int partition_fill_callback(void* ctx, gpt_partition_entry_t* entry) {
  struct gpt_partition_ctx* partition_ctx = (struct gpt_partition_ctx*)ctx;

  if (!ctx || !entry) {
    return -1;  // Stop iteration on error
  }

  struct device* parent_device = partition_ctx->parent_device;

  if (!parent_device || !parent_device->dev) {
    return -1;  // Stop iteration on error
  }

  struct block_device* parent_blk_dev = parent_device->dev;

  struct block_device* child = kmalloc(sizeof(struct block_device));

  if (!child) {
    log_error("Failed to allocate memory for block device.");
    return -1;  // Stop iteration on error
  }

  memset(child, 0, sizeof(struct block_device));

  child->type = BLOCK_DEVICE_PARTITION;
  child->start_lba = entry->start_LBA;
  child->sector_count = entry->end_LBA - entry->start_LBA + 1;
  child->sector_size = parent_blk_dev->sector_size;
  child->ops = block_partition_get_ops();
  child->private_data = parent_blk_dev->private_data;
  child->fs_type = FS_TYPE_UNKNOWN;
  child->parent = parent_blk_dev;

  int result =
      device_registry_register_child(parent_device, child, DEVICE_TYPE_BLOCK);

  if (result < 0) {
    log_error("Failed to register child block device for partition.");
    kfree(child);
    return 0;  // Stop iteration on error
  }

  int rc = parent_blk_dev->ops->read_sectors(
      parent_blk_dev, partition_ctx->buffer, entry->start_LBA, 1);

  if (rc != 0) {
    log_error("Failed to read first sector of partition. Error code: %d", rc);
    kfree(child);
    return 0;  // Stop iteration on error
  }

  child->fs_type = gpt_partition_probe_fs(partition_ctx->buffer, entry);

  // If this is the kernel partition, store its information in the context
  if (partition_ctx->kernel_partition_index == partition_ctx->count) {
    partition_ctx->kernel_partition_fs_type = child->fs_type;
    partition_ctx->kernel_partition_device = child;
  }

  partition_ctx->count++;
  return 0;  // Continue iteration
}

int prepare_kernel_partition(struct block_device** root_dev,
                             enum filesystem_type* fs_type) {
  if (!root_dev || !fs_type) {
    log_error("Invalid arguments to prepare_kernel_partition.");
    return -1;
  }

  struct boot_volume_info boot_volume_info;

  if (!getBootVolumeInfo(&boot_volume_info)) {
    log_error("Failed to get boot volume info.");
    return -1;
  }

  if (boot_volume_info.type != BOOT_VOLUME_TYPE_GPT) {
    log_error("Boot volume is not GPT. Type: %d", boot_volume_info.type);
    return -1;
  }

  struct device* device = device_registry_get_list();

  while (device) {
    if (device->type != DEVICE_TYPE_BLOCK) {
      device = device->next;
      continue;
    }

    struct block_device* blk_dev = device->dev;

    if (!blk_dev) {
      log_error("Block device structure is NULL for device ID: %u",
                device->device_id);
      device = device->next;
      continue;
    }

    gpt_header_t primary_header;
    gpt_result_t result = gpt_get_primary_header(blk_dev, &primary_header);

    if (result != GPT_SUCCESS) {
      log_error("Failed to get GPT primary header for device ID: %u, Error: %d",
                device->device_id, result);
      device = device->next;
      continue;
    }

    // check if the disk GUID matches the boot volume's disk GUID
    if (memcmp(primary_header.disk_guid, &boot_volume_info.gpt_disk_uuid,
               sizeof(primary_header.disk_guid)) != 0) {
      device = device->next;
      log_error("Disk GUID mismatch for device ID: %u", device->device_id);
      continue;
    }

    gpt_partition_entry_t partition_entry;
    result = gpt_get_partition_entry(blk_dev, &primary_header,
                                     boot_volume_info.partition_index - 1,
                                     &partition_entry);

    if (result != GPT_SUCCESS) {
      log_error(
          "Failed to get GPT partition entry for device ID: %u, Error: %d",
          device->device_id, result);
      device = device->next;
      continue;
    }

    // check if the partition GUID matches the boot volume's partition GUID
    if (memcmp(partition_entry.partition_guid,
               &boot_volume_info.gpt_partition_uuid,
               sizeof(partition_entry.partition_guid)) != 0) {
      log_error("Partition GUID mismatch for device ID: %u", device->device_id);
      return -1;
    }

    int save_idx = 0;
    struct gpt_partition_ctx partition_ctx;

    partition_ctx.parent_device = device;
    partition_ctx.count = 1;  // Start with 1 for the boot partition
    partition_ctx.buffer = kmalloc(blk_dev->sector_size);
    partition_ctx.kernel_partition_index = boot_volume_info.partition_index;
    partition_ctx.kernel_partition_fs_type = FS_TYPE_UNKNOWN;
    partition_ctx.kernel_partition_device = NULL;

    result =
        gpt_iterate_partition_entries(blk_dev, &primary_header, &save_idx,
                                      &partition_ctx, partition_fill_callback);

    kfree(partition_ctx.buffer);

    if (result != GPT_SUCCESS && result != GPT_ERR_OUTOF_PARTITION_ENTRIES) {
      log_error(
          "Failed to iterate GPT partition entries for device ID: %u, "
          "Error: %d",
          device->device_id, result);
      return -1;
    }

    *fs_type = partition_ctx.kernel_partition_fs_type;
    *root_dev = partition_ctx.kernel_partition_device;

    return 0;
  }

  return -1;
}

void create_required_directories(void) {
  int rc;

  rc = fs_mkdir("/mnt", 0755);

  if (rc != 0) {
    log_error("Failed to create /mnt directory. Error code: %d", rc);
  }

  rc = fs_mkdir("/dev", 0755);

  if (rc != 0) {
    log_error("Failed to create /dev directory. Error code: %d", rc);
  }

  rc = fs_mkdir("/tmp", 0777);

  if (rc != 0) {
    log_error("Failed to create /tmp directory. Error code: %d", rc);
  }

  rc = fs_mkdir("/proc", 0755);

  if (rc != 0) {
    log_error("Failed to create /proc directory. Error code: %d", rc);
  }

  rc = fs_mkdir("/sys", 0755);

  if (rc != 0) {
    log_error("Failed to create /sys directory. Error code: %d", rc);
  }
}

void register_basic_devices(void) {
  struct inode* devfs_inode = fs_get_dev_dir_inode();

  if (!devfs_inode) {
    log_error("Failed to resolve /dev directory.");
    return;
  }

  // Register /dev/null
  int rc = devfs_register(devfs_inode, "null", DEVFS_DEVICE_CHAR, 0,
                          device_get_dev_null_fops(), NULL);

  if (rc != 0) {
    log_error("Failed to register /dev/null. Error code: %d", rc);
  }

  // Register /dev/zero
  rc = devfs_register(devfs_inode, "zero", DEVFS_DEVICE_CHAR, 1,
                      device_get_dev_zero_fops(), NULL);

  if (rc != 0) {
    log_error("Failed to register /dev/zero. Error code: %d", rc);
  }

  // Register /dev/console
  rc = devfs_register(devfs_inode, "console", DEVFS_DEVICE_CHAR, 2,
                      device_get_dev_console_fops(), NULL);

  if (rc != 0) {
    log_error("Failed to register /dev/console. Error code: %d", rc);
  }
}

void register_all_devices(void) {
  struct inode* devfs_inode = fs_get_dev_dir_inode();

  if (!devfs_inode) {
    log_error("Failed to resolve /dev directory.");
    return;
  }

  struct device_operations* blockdev_fops = blockdev_get_fops();
  struct device* device = device_registry_get_list();

  while (device) {
    if (device->type == DEVICE_TYPE_BLOCK) {
      struct block_device* blk_dev = device->dev;

      if (blk_dev && blk_dev->type == BLOCK_DEVICE_DISK) {
        char disk_name[32];
        generate_disk_name(-1, disk_name, sizeof(disk_name));

        const uint32_t dev_id = device->device_id;

        int rc = devfs_register(devfs_inode, disk_name, DEVFS_DEVICE_BLOCK,
                                dev_id, blockdev_fops, blk_dev);

        if (rc != 0) {
          log_error(
              "Failed to register block device %s in devfs. Error code: %d",
              disk_name, rc);
        }

        // if child devices exist, register them as well
        struct device* child_device = device->child;
        int child_index = 0;

        while (child_device) {
          if (child_device->type == DEVICE_TYPE_BLOCK) {
            struct block_device* child_blk_dev = child_device->dev;
            const uint32_t child_dev_id = child_device->device_id;

            if (child_blk_dev &&
                child_blk_dev->type == BLOCK_DEVICE_PARTITION) {
              char partition_name[32];
              generate_disk_name(child_index, partition_name,
                                 sizeof(partition_name));

              int rc = devfs_register(devfs_inode, partition_name,
                                      DEVFS_DEVICE_BLOCK, child_dev_id,
                                      blockdev_fops, child_blk_dev);

              if (rc != 0) {
                log_error(
                    "Failed to register block device %s in devfs. Error code: "
                    "%d",
                    partition_name, rc);
              }
            }
          }

          child_index++;
          child_device = child_device->next;
        }
      }
    }

    device = device->next;
  }
}

struct readdir_ctx {
  char names[32][256];
  uint32_t inos[32];
  uint32_t types[32];
  int count;
  int capacity;
  int stop_after; /* return nonzero from fill after N entries */
};

static int capture_fill(void* ctx, const char* name, uint32_t ino,
                        uint32_t type) {
  struct readdir_ctx* c = (struct readdir_ctx*)ctx;
  if (c->count >= c->capacity) return 1; /* buffer full */

  strcpy(c->names[c->count], name);
  c->inos[c->count] = ino;
  c->types[c->count] = type;
  c->count++;

  if (c->stop_after > 0 && c->count >= c->stop_after)
    return 1; /* simulate "caller's buffer full" */

  return 0;
}

void print_dirs(const char* path) {
  struct readdir_ctx ctx;
  memset(&ctx, 0, sizeof(ctx));

  ctx.capacity = 32;
  ctx.stop_after = 0;

  struct file* dirfile = fs_open_dir(path);

  if (!dirfile) {
    log_error("Failed to open directory: %s\n", path);
    return;
  }

  int rc = fs_readdir(dirfile, capture_fill, &ctx);

  fs_close_dir(dirfile);

  if (rc != 0) {
    log_error("vfs_readdir failed with error: %d\n", rc);
    return;
  }

  log_print("Directory entries:\n");

  for (int i = 0; i < ctx.count; i++) {
    log_print("  %s\n", ctx.names[i]);
  }
  log_print("\n");
}
