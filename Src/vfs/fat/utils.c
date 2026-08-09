#include "utils.h"

#include <stdint.h>

#include <utils/log.h>

#include "gpt.h"

uint32_t calculate_crc32(const uint8_t* data, const uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;

  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];

    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? GPT_CRC32_POLYNOMIAL : 0);
    }
  }

  return crc ^ 0xFFFFFFFF;
}

static void print_protective_mbr_entry(protective_mbr_entry_t* entry) {
  LOG_PRINT("  Boot Indicator: 0x%x\n", entry->boot_indicator);
  LOG_PRINT("  Partition Type: 0x%x\n", entry->partition_type);
  LOG_PRINT("  Starting LBA: 0x%x\n", entry->starting_LBA);
  LOG_PRINT("  Size Sectors: 0x%x\n", entry->size_sectors);
}

void print_protective_mbr(protective_mbr_t* sector) {
  LOG_PRINT("\nProtective MBR:\n");
  LOG_PRINT("Signature: 0x%x\n", sector->signature);
  LOG_PRINT("Partition Entry:\n");

  for (int i = 0; i < 4; i++) {
    LOG_PRINT("\nPartition %d:\n", i + 1);
    print_protective_mbr_entry(&sector->partition_table[i]);
  }
}

static void print_guid(const uint8_t* guid) {
  LOG_PRINT(
      "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
      guid[3], guid[2], guid[1], guid[0],  // Little-endian for first 4 bytes
      guid[5], guid[4],                    // Next 2 bytes
      guid[7], guid[6],                    // Next 2 bytes
      guid[8], guid[9],                    // Next 2 bytes
      guid[10], guid[11], guid[12], guid[13], guid[14],
      guid[15]  // Last 6 bytes
  );
}

void print_primary_header(gpt_primary_header_t* header) {
  LOG_PRINT("\nGPT Primary Header:\n");
  LOG_PRINT("Signature: %.8s\n", header->signature);
  LOG_PRINT("Revision: 0x%x\n", header->revision);
  LOG_PRINT("Header Size: 0x%x\n", header->header_size);
  LOG_PRINT("Header CRC32: 0x%x\n", header->header_CRC32);
  LOG_PRINT("Primary LBA: 0x%lx\n", header->primary_LBA);
  LOG_PRINT("Backup Header LBA: 0x%lx\n", header->backup_LBA);
  LOG_PRINT("First LBA: 0x%lx\n", header->first_LBA);
  LOG_PRINT("Last LBA: 0x%lx\n", header->last_LBA);

  LOG_PRINT("Unique Identifier: ");
  print_guid(header->disk_guid);

  LOG_PRINT("Starting table LBA: 0x%lx\n", header->table_LBA);
  LOG_PRINT("Num part Entries: 0x%x\n", header->num_part_entr);
  LOG_PRINT("Size of Partition Entries: 0x%x\n", header->size_Part_entr);
  LOG_PRINT("Partition tabel CRC32: 0x%x\n", header->table_CRC32);
}

// static inline void print_utf_16(uint16_t* buffer) {
//   for (int i = 0; buffer[i] && i < 36; i++) {
//     LOG_PRINT("%lc", (wint_t)buffer[i]);
//   }
// }

void print_partition_entry(gpt_partition_entry_t* entries) {
  LOG_PRINT("\nPartition Entry:\n");
  LOG_PRINT("Partition Type GUID: ");
  print_guid(entries->partition_type_guid);
  LOG_PRINT("Partition GUID: ");
  print_guid(entries->partition_guid);
  LOG_PRINT("Starting LBA: 0x%lx\n", entries->start_LBA);
  LOG_PRINT("Ending LBA: 0x%lx\n", entries->end_LBA);
  LOG_PRINT("Partition Attributes: 0x%lx\n", entries->partition_attr);
  LOG_PRINT("Partition Name: ");
  // print_utf_16(entries->name);
  LOG_PRINT("\n");
}
