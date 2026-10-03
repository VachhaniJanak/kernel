#include <device/block.h>
#include <fs/prt/gpt.h>
#include <libs/string.h>
#include <mm/vmm/kheap.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>

static inline int disk_read(struct block_device* dev, void* buffer, size_t lba,
                            size_t sector_count) {
  if (!dev || !buffer || sector_count == 0) {
    return -1;
  }

  if (!dev->ops || !dev->ops->read_sectors) {
    return -1;
  }

  int result = dev->ops->read_sectors(dev, buffer, lba, sector_count);

  if (result != 0) {
    return -1;
  }

  return 0;
}

static gpt_result_t gpt_read_headers(struct block_device* dev,
                                     gpt_header_t* primary_header,
                                     gpt_header_t* backup_header) {
  uint8_t* buffer = kmalloc(dev->sector_size);

  if (!buffer) {
    return GPT_ERR_OUT_MM;
  }

  int result = disk_read(dev, buffer, 0, 1);

  if (result != 0) {
    kfree(buffer);
    return GPT_ERR_READ;
  }

  protective_mbr_t* mbr = (protective_mbr_t*)buffer;

  if (mbr->signature != GPT_MBR_SIGNATURE) {
    kfree(buffer);
    return GPT_ERR_INVALID_MBR;
  }

  if (mbr->partition_table[0].partition_type != GPT_PROTECTIVE_PARTITION_TYPE) {
    kfree(buffer);
    return GPT_ERR_NOT_GPT_PARTITION;
  }

  size_t primary_header_lba = mbr->partition_table[0].starting_LBA;

  result = disk_read(dev, buffer, primary_header_lba, 1);

  if (result != 0) {
    kfree(buffer);
    return GPT_ERR_READ;
  }

  memcpy(primary_header, buffer, sizeof(*primary_header));

  if (backup_header != NULL) {
    size_t backup_header_lba = primary_header->backup_LBA;

    result = disk_read(dev, buffer, backup_header_lba, 1);

    if (result != 0) {
      kfree(buffer);
      return GPT_ERR_READ;
    }

    memcpy(backup_header, buffer, sizeof(*backup_header));
  }

  kfree(buffer);
  return GPT_SUCCESS;
}

static uint32_t calculate_crc32(const uint8_t* data, const uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;

  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];

    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? GPT_CRC32_POLYNOMIAL : 0);
    }
  }

  return crc ^ 0xFFFFFFFF;
}

static inline bool is_valid_header_signature(const uint8_t* signature) {
  return memcmp(signature, GPT_HEADER_SIGNATURE,
                sizeof(GPT_HEADER_SIGNATURE) - 1) == 0;
}

static gpt_result_t gpt_verify_header(gpt_header_t* header) {
  if (!is_valid_header_signature(header->signature)) {
    return GPT_ERR_INVALID_HEADER;
  }

  uint32_t original_crc32 = header->header_CRC32;
  header->header_CRC32 = 0;  // Set to 0 for CRC calculation
  uint32_t header_size = header->header_size;
  uint32_t calculated_crc32 =
      calculate_crc32((const uint8_t*)header, header_size);

  if (calculated_crc32 != original_crc32) {
    return GPT_ERR_INVALID_HEADER;
  }

  return GPT_SUCCESS;
}

static inline bool is_guid_same(const uint8_t* guid1, const uint8_t* guid2) {
  return memcmp(guid1, guid2, 16) == 0;
}

static inline gpt_result_t is_consistent_headers(gpt_header_t* primary_header,
                                                 gpt_header_t* backup_header) {
  if (primary_header->header_size != backup_header->header_size) {
    return GPT_ERR_INCONSISTENT_HEADERS;
  }

  if (!is_guid_same(primary_header->disk_guid, backup_header->disk_guid)) {
    return GPT_ERR_INCONSISTENT_HEADERS;
  }

  if (primary_header->num_part_entr != backup_header->num_part_entr) {
    return GPT_ERR_INCONSISTENT_HEADERS;
  }

  if (primary_header->size_Part_entr != backup_header->size_Part_entr) {
    return GPT_ERR_INCONSISTENT_HEADERS;
  }

  if (primary_header->table_CRC32 != backup_header->table_CRC32) {
    return GPT_ERR_INCONSISTENT_HEADERS;
  }

  return GPT_SUCCESS;
}

gpt_result_t gpt_get_primary_header(struct block_device* dev,
                                    gpt_header_t* primary_header) {
  if (!dev || !primary_header) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  gpt_header_t* backup_header = kmalloc(sizeof(gpt_header_t));

  if (!backup_header) {
    return GPT_ERR_INVALID_HEADER;
  }

  // read both primary and backup headers
  gpt_result_t result = gpt_read_headers(dev, primary_header, backup_header);

  if (result != GPT_SUCCESS) {
    kfree(backup_header);
    return result;
  }

  result = gpt_verify_header(primary_header);

  if (result != GPT_SUCCESS) {
    kfree(backup_header);
    return result;
  }

  result = gpt_verify_header(backup_header);

  if (result != GPT_SUCCESS) {
    kfree(backup_header);
    return result;
  }

  result = is_consistent_headers(primary_header, backup_header);

  if (result != GPT_SUCCESS) {
    kfree(backup_header);
    return result;
  }

  kfree(backup_header);
  return GPT_SUCCESS;
}

static inline bool is_empty_partition_entry(gpt_partition_entry_t* entry) {
  return entry->start_LBA == 0 && entry->end_LBA == 0;
}

gpt_result_t gpt_iterate_partition_entries(
    struct block_device* dev, gpt_header_t* header, int* save_idx, void* ctx,
    int (*fill)(void* ctx, gpt_partition_entry_t* entry)) {
  if (!header || !fill || !save_idx) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  int num_entries = (int)header->num_part_entr;
  size_t table_lba = header->table_LBA;
  size_t entry_size = header->size_Part_entr;
  const size_t sector_size = dev->sector_size;

  if (entry_size == 0 || entry_size > sector_size) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  if (*save_idx >= num_entries) {
    return GPT_ERR_OUTOF_PARTITION_ENTRIES;
  }

  /*GPT entry size must allow at least one complete entry* in a sector.*/
  size_t entries_per_sector = sector_size / entry_size;

  if (entries_per_sector == 0) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  void* buffer = kmalloc(sector_size);

  if (!buffer) {
    return GPT_ERR_OUT_MM;
  }

  size_t current_sector = SIZE_MAX;  // Initialize to an invalid sector number

  for (int i = *save_idx; i < num_entries; ++i) {
    size_t required_sector = (size_t)i / entries_per_sector;
    size_t entry_index_in_sector = (size_t)i % entries_per_sector;

    //  Load the sector only when it changes.
    if (required_sector != current_sector) {
      if (disk_read(dev, buffer, table_lba + required_sector, 1) != 0) {
        kfree(buffer);
        return GPT_ERR_READ;
      }

      current_sector = required_sector;
    }

    uint8_t* entry_address = buffer + entry_index_in_sector * entry_size;
    gpt_partition_entry_t* partition = (gpt_partition_entry_t*)entry_address;

    *save_idx = i + 1;

    // Skip empty partition entries
    if (is_empty_partition_entry(partition)) {
      continue;
    }

    int callback_result = fill(ctx, partition);

    if (callback_result < 0) {
      kfree(buffer);
      return GPT_SUCCESS;
    }
  }

  kfree(buffer);
  return GPT_ERR_OUTOF_PARTITION_ENTRIES;
}

gpt_result_t gpt_get_partition_entry(struct block_device* dev,
                                     gpt_header_t* header, int index,
                                     gpt_partition_entry_t* entry) {
  if (!header || !entry || index < 0 ||
      (size_t)index >= header->num_part_entr) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  size_t table_lba = header->table_LBA;
  size_t entry_size = header->size_Part_entr;
  const size_t sector_size = dev->sector_size;

  if (entry_size == 0 || entry_size > sector_size) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  size_t entries_per_sector = sector_size / entry_size;

  if (entries_per_sector == 0) {
    return GPT_ERR_INVALID_PARAMETER;
  }

  size_t required_sector = (size_t)index / entries_per_sector;
  size_t entry_index_in_sector = (size_t)index % entries_per_sector;

  void* buffer = kmalloc(sector_size);

  if (!buffer) {
    return GPT_ERR_OUT_MM;
  }

  if (disk_read(dev, buffer, table_lba + required_sector, 1) != 0) {
    kfree(buffer);
    return GPT_ERR_READ;
  }

  uint8_t* entry_address = buffer + entry_index_in_sector * entry_size;
  memcpy(entry, entry_address, entry_size);

  kfree(buffer);
  return GPT_SUCCESS;
}