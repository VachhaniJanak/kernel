#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GPT_SECTOR_SIZE 512
#define GPT_MBR_SIGNATURE 0xaa55
#define GPT_PROTECTIVE_PARTITION_TYPE 0xEE
#define GPT_HEADER_SIGNATURE "EFI PART"

// CRC32 polynomial (0xEDB88320)
#define GPT_CRC32_POLYNOMIAL 0xEDB88320
#define GPT_NUM_PART_ENTRY 128

typedef struct __attribute__((packed)) {
  uint8_t boot_indicator;   // 0x00 (not bootable)
  uint8_t starting_CHS[3];  // 0x000001
  uint8_t partition_type;   // 0xEE (GPT protective partition)
  uint8_t ending_CHS[3];    // 0xFFFFFF
  uint32_t starting_LBA;    // 0x00000001 (starting at sector 1)
  uint32_t size_sectors;    // size of the disk
} protective_mbr_entry_t;

typedef struct __attribute__((packed)) {
  uint8_t bootcode[446];
  protective_mbr_entry_t partition_table[4];  // 4 partition entries
  uint16_t signature;                         // 0x55AA to indicate MBR
} protective_mbr_t;

typedef struct __attribute__((packed)) {
  uint8_t signature[8];     // ASCII string "EFI PART"
  uint32_t revision;        // specification version
  uint32_t header_size;     // GPT header size (usually 92 bytes)
  uint32_t header_CRC32;    // GPT header checksum
  uint32_t reserved0;       // Reserved (must be 0)
  uint64_t primary_LBA;     // LBA address of this GPT header (usually 1)
  uint64_t backup_LBA;      // backup GPT header addr (last sector)
  uint64_t first_LBA;       // first LBA address of partitions
  uint64_t last_LBA;        // last LBA address of partitions
  uint8_t disk_guid[16];    // unique identifier for the disk (GUID)
  uint64_t table_LBA;       // partition table starting LBA address
  uint32_t num_part_entr;   // entries in the partition table
  uint32_t size_Part_entr;  // size of each partition entry (128 bytes).
  uint32_t table_CRC32;     // A checksum of partition table.
  uint8_t reserved1[420];   // Reserved (must be 0).
} gpt_primary_header_t;

typedef struct __attribute__((packed)) {
  uint8_t partition_type_guid[16];  // indicat type of partition
  uint8_t partition_guid[16];
  uint64_t start_LBA;  // Starting sector of the partition
  uint64_t end_LBA;    // Ending sector of the partition
  uint64_t partition_attr;
  uint16_t name[36];  // name of partition (UTF-16)
} gpt_partition_entry_t;

bool get_gpt_header(gpt_primary_header_t* header);

size_t get_num_gpt_valid_partition(gpt_primary_header_t* header);

bool get_gpt_partition_entry(gpt_primary_header_t* header,
                             gpt_partition_entry_t* entry, size_t index);