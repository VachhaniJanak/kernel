#include "gpt.h"

#include <drivers/ahci/ahci.h>
#include <mm/vmm/kheap.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/utils.h>

#include "utils.h"

static bool read_gpt_header(gpt_primary_header_t* header) {
  uint8_t* virt_buf = kmalloc(GPT_SECTOR_SIZE);
  uint8_t* phys_buf = kmalloc_phys_addr(virt_buf);

  ahci_read_disk(0, 1, phys_buf);

  protective_mbr_t* mbr = (protective_mbr_t*)virt_buf;

  if (mbr->signature != GPT_MBR_SIGNATURE) {
    return false;  // Invalid MBR signature
  }

  if (mbr->partition_table[0].partition_type != GPT_PROTECTIVE_PARTITION_TYPE) {
    return false;
  }

  size_t gpt_header_lba = mbr->partition_table[0].starting_LBA;

  ahci_read_disk(gpt_header_lba, 1, phys_buf);
  kmemcpy(header, virt_buf, sizeof(gpt_primary_header_t));

  kfree(virt_buf);
  return true;
}

static inline bool is_valid_header_signature(const uint8_t* signature) {
  return kmemcmp(signature, GPT_HEADER_SIGNATURE,
                 sizeof(GPT_HEADER_SIGNATURE) - 1) == 0;
}

static bool verify_header(gpt_primary_header_t* header) {
  if (!is_valid_header_signature(header->signature)) {
    return false;
  }

  uint32_t original_crc32 = header->header_CRC32;
  header->header_CRC32 = 0;  // Set to 0 for CRC calculation
  uint32_t header_size = header->header_size;
  uint32_t calculated_crc32 =
      calculate_crc32((const uint8_t*)header, header_size);

  if (calculated_crc32 != original_crc32) {
    return false;
  }

  return true;
}

static inline bool is_guid_same(const uint8_t* guid1, const uint8_t* guid2) {
  return kmemcmp(guid1, guid2, 16) == 0;
}

static bool is_consistent_header(gpt_primary_header_t* primary_header,
                                 gpt_primary_header_t* backup_header) {
  if (primary_header->header_size != backup_header->header_size) {
    return false;
  }

  if (!is_guid_same(primary_header->disk_guid, backup_header->disk_guid)) {
    return false;
  }

  if (primary_header->num_part_entr != backup_header->num_part_entr) {
    return false;
  }

  if (primary_header->size_Part_entr != backup_header->size_Part_entr) {
    return false;
  }

  if (primary_header->table_CRC32 != backup_header->table_CRC32) {
    return false;
  }

  return true;
}

bool get_gpt_header(gpt_primary_header_t* header) {
  uint8_t* virt_buf = kmalloc(GPT_SECTOR_SIZE);
  uint8_t* phys_buf = kmalloc_phys_addr(virt_buf);

  read_gpt_header(header);

  if (!verify_header(header)) {
    return false;  // Invalid GPT header
  }

  ahci_read_disk(header->backup_LBA, 1, phys_buf);
  gpt_primary_header_t* header2 = (gpt_primary_header_t*)virt_buf;

  if (!verify_header(header2)) {
    return false;  // Invalid backup GPT header
  }

  if (!is_consistent_header(header, header2)) {
    return false;  // Inconsistent headers
  }

  kfree(virt_buf);
  return true;
}

size_t get_num_gpt_valid_partition(gpt_primary_header_t* header) {
  uint8_t* virt_buf = kmalloc(GPT_SECTOR_SIZE);
  uint8_t* phys_buf = kmalloc_phys_addr(virt_buf);

  size_t count = 0;
  for (size_t i = 0; i < header->num_part_entr; i++) {
    ahci_read_disk(header->table_LBA + i, 1, phys_buf);

    gpt_partition_entry_t* partition = (void*)virt_buf;

    size_t num_partitions = GPT_SECTOR_SIZE / header->size_Part_entr;
    for (size_t j = 0; j < num_partitions; j++, partition++) {
      if (partition->start_LBA == 0 && partition->end_LBA == 0) {
        continue;  // Skip empty partition entries
      }
      count++;
    }
  }

  kfree(virt_buf);
  return count;
}

bool get_gpt_partition_entry(gpt_primary_header_t* header,
                             gpt_partition_entry_t* entry, size_t index) {
  uint8_t* virt_buf = kmalloc(GPT_SECTOR_SIZE);
  uint8_t* phys_buf = kmalloc_phys_addr(virt_buf);

  size_t count = 0;
  for (size_t i = 0; i < header->num_part_entr; i++) {
    ahci_read_disk(header->table_LBA + i, 1, phys_buf);

    gpt_partition_entry_t* partition = (void*)virt_buf;

    size_t num_partitions = GPT_SECTOR_SIZE / header->size_Part_entr;
    for (size_t j = 0; j < num_partitions; j++, partition++) {
      if (partition->start_LBA == 0 && partition->end_LBA == 0) {
        continue;  // Skip empty partition entries
      }

      if (count == index) {
        *entry = *partition;
        return true;
      }

      count++;
    }
  }

  kfree(virt_buf);
  return false;
}
