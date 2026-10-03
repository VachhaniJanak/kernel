#pragma once

#include <fs/fs.h>
#include <platform/attributes.h>
#include <stdint.h>

#define FAT32_EOC 0x0FFFFFF8  // End of clusterchain marker

enum fat32_attributes {
  FAT_ATTR_READ_ONLY = 0x01,
  FAT_ATTR_HIDDEN = 0x02,
  FAT_ATTR_SYSTEM = 0x04,
  FAT_ATTR_VOLUME_ID = 0x08,
  FAT_ATTR_DIRECTORY = 0x10,
  FAT_ATTR_ARCHIVE = 0x20,
  FAT_ATTR_LFN = FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM |
      FAT_ATTR_VOLUME_ID
};

enum fat32_readdir_state {
  FAT_READDIR_DOT = 0,
  FAT_READDIR_DOTDOT,
  FAT_READDIR_CHILDREN,
  FAT_READDIR_END,
};

struct fat32_bpb {
  // Basic FAT16/FAT32 Shared Header
  uint8_t jmp[3];    // Jump instruction for bootloaders
  char oem_name[8];  // e.g., "MSWIN4.1"
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sectors;
  uint8_t fat_count;
  uint16_t root_dir_entries;   // Always 0 for FAT32
  uint16_t total_sectors_16;   // Always 0 for FAT32
  uint8_t media_descriptor;    // Usually 0xF8 for hard drives
  uint16_t fat_size_16;        // Always 0 for FAT32
  uint16_t sectors_per_track;  // For old floppy disks (ignore)
  uint16_t heads;              // For old floppy disks (ignore)
  uint32_t hidden_sectors;     // Sectors before the partition begins
  uint32_t total_sectors_32;   // Total size of the volume in sectors

  // FAT32-Specific Extended Header
  uint32_t fat_size_32;   // How many sectors a single FAT table takes up
  uint16_t ext_flags;     // Mirroring flags etc.
  uint16_t fat_version;   // Usually 0
  uint32_t root_cluster;  // The cluster number where the root directory starts
                          // (usually 2)
  uint16_t fs_info_sector;      // Usually sector 1
  uint16_t backup_boot_sector;  // Usually sector 6
  uint8_t reserved[12];
  uint8_t drive_number;  // 0x80 for hard disks
  uint8_t reserved1;
  uint8_t boot_signature;  // Usually 0x29
  uint32_t volume_id;      // Serial number
  char volume_label[11];   // e.g., "NO NAME    "
  char fs_type[8];         // "FAT32   "

  // The rest of the 512-byte sector is boot code and a 0xAA55 signature
} PACKED;

struct fat32_dir_entry {
  char name[11];  // 8 chars for name, 3 for extension (NO DOT!) e.g. "FILE TXT"
  uint8_t attributes;  // 0x01=Read-Only, 0x02=Hidden, 0x10=Directory, 0x0F=Long
                       // File Name
  uint8_t reserved_nt;
  uint8_t creation_time_tenths;
  uint16_t creation_time;
  uint16_t creation_date;
  uint16_t last_access_date;
  uint16_t cluster_high;
  uint16_t last_mod_time;
  uint16_t last_mod_date;
  uint16_t cluster_low;
  uint32_t file_size;  // Size of the file in bytes (0 for directories)
} PACKED;

struct fat32_lfn_entry {
  uint8_t order;       // The sequence number of this LFN entry
  uint16_t name1[5];   // First 5 characters (UTF-16)
  uint8_t attributes;  // MUST be 0x0F (FAT_ATTR_LFN)
  uint8_t type;        // MUST be 0x00
  uint8_t checksum;    // Checksum of the associated 8.3 short name
  uint16_t name2[6];   // Next 6 characters (UTF-16)
  uint16_t zero;       // MUST be 0x0000
  uint16_t name3[2];   // Final 2 characters (UTF-16)
} PACKED;

struct fat32_sb_info {
  struct block_device* device;

  // Cached Geometry
  uint32_t bytes_per_sector;
  uint32_t sectors_per_cluster;
  uint32_t cluster_size_bytes;
  uint32_t total_sectors;

  // Absolute Disk Addresses (LBA - Logical Block Addresses)
  uint32_t fat_start_lba;
  uint32_t data_start_lba;

  uint32_t root_dir_cluster;
  size_t current_inode_num;  // For generating unique inode numbers for
                             // files/directories
};

struct fat32_dir_info {
  uint32_t current_cluster;
  uint32_t entry_index;
  enum fat32_readdir_state state;  // 0=dot, 1=dotdot, 2=children, 3=end
};

struct fat32_inode {
  struct inode* vfs;           // back-pointer to the VFS inode wrapper
  struct fat32_inode* parent;  // parent directory (NULL for root)
  uint32_t first_cluster;
  uint32_t file_size;

  // parent directory's cluster and offset of this entry
  uint32_t entry_cluster;
  uint32_t entry_index;

  // Performance Optimization: The "Cursor"
  // If a user reads 10MB sequentially, we shouldn't re-walk the FAT table from
  // cluster 0 every single read. We cache where they currently are!
  uint32_t cached_logical_cluster;
  uint32_t cached_physical_cluster;
  struct block_device* device;
};

struct mount_ops* fat32_get_mount_ops(void);

struct superblock* fat32_create_superblock(struct block_device* block_dev);
