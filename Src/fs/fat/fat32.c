#include <device/block.h>
#include <fs/devfs/devfs.h>
#include <fs/fat/fat32.h>
#include <fs/fs.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <platform/attributes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <process/locks.h>

static struct superblock* fat32_mount(struct inode* device_inode);

int fat32_destroy_superblock(struct superblock* sb);

struct inode* fat32_lookup(struct inode* dir, const char* name);

int fat32_open_dir(struct file* file);

int fat32_close_dir(struct file* file);

int fat32_readdir(struct file* file, void* ctx,
                  int (*fill)(void*, const char*, uint32_t, uint32_t));

int fat32_mkdir(struct inode* dir, const char* name, uint32_t mode);

int fat32_rmdir(struct inode* dir, const char* name);

int fat32_read(struct file* file, void* buf, size_t len, size_t* off);

int fat32_write(struct file* file, const void* buf, size_t len, size_t* off);

struct inode* fat32_create(struct inode* dir, const char* name, uint32_t mode);

int fat32_unlink(struct inode* dir, const char* name);

void fat32_destroy(struct inode* inode);

static struct mount_ops fat32_mount_ops = {
    .target_mount = fat32_mount,
    .target_unmount = fat32_destroy_superblock,
};

static struct file_operations fat32_file_ops = {
    .read = fat32_read,
    .write = fat32_write,
    .open = NULL,
    .close = NULL,
    .destroy = fat32_destroy,
};

static struct file_operations fat32_dir_ops = {
    .open = fat32_open_dir,
    .close = fat32_close_dir,
    .readdir = fat32_readdir,
    .lookup = fat32_lookup,
    .mkdir = fat32_mkdir,
    .rmdir = fat32_rmdir,
    .create = fat32_create,
    .unlink = fat32_unlink,
    .destroy = fat32_destroy,
};

struct mount_ops* fat32_get_mount_ops(void) { return &fat32_mount_ops; }

// Convert a cluster number to an absolute physical disk sector
static inline uint32_t cluster_to_sector(struct fat32_sb_info* sb_info,
                                         uint32_t cluster) {
  // Data clusters start at 2.
  return sb_info->data_start_lba +
         ((cluster - 2) * sb_info->sectors_per_cluster);
}

// Combine the high and low 16-bit cluster halves from the Directory Entry
static inline uint32_t get_full_cluster(struct fat32_dir_entry* entry) {
  return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

static void to_fat_filename(const char* input, char* output) {
  int i = 0, j = 0;

  // Fill output with spaces by default
  for (int k = 0; k < 11; k++) output[k] = ' ';

  // Copy the name (up to 8 chars)
  while (input[i] != '\0' && input[i] != '.' && j < 8) {
    // Convert to uppercase
    char c = input[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    output[j++] = c;
    i++;
  }

  // If there is an extension, jump to index 8 in the output
  while (input[i] != '\0' && input[i] != '.') i++;  // find the dot

  if (input[i] == '.') {
    i++;  // skip the dot
    j = 8;
    // Copy the extension (up to 3 chars)
    while (input[i] != '\0' && j < 11) {
      char c = input[i];
      if (c >= 'a' && c <= 'z') c -= 32;
      output[j++] = c;
      i++;
    }
  }
}

static uint8_t lfn_checksum(const unsigned char* short_name) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++) {
    // Rotate right by 1 bit and add the next character
    sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
  }
  return sum;
}

static void extract_lfn_chars(struct fat32_lfn_entry* lfn, char* buffer) {
  int sequence = (lfn->order & 0x3F) - 1;
  int offset = sequence * 13;

  for (int i = 0; i < 5; i++) buffer[offset++] = (char)(lfn->name1[i] & 0xFF);
  for (int i = 0; i < 6; i++) buffer[offset++] = (char)(lfn->name2[i] & 0xFF);
  for (int i = 0; i < 2; i++) buffer[offset++] = (char)(lfn->name3[i] & 0xFF);
}

static void create_lfn_entry(struct fat32_lfn_entry* lfn, const char* name,
                             int seq, int is_last, uint8_t checksum) {
  memset(lfn, 0, sizeof(struct fat32_lfn_entry));

  lfn->order = seq;
  if (is_last) {
    lfn->order |= 0x40;
  }

  lfn->attributes = FAT_ATTR_LFN;
  lfn->type = 0;
  lfn->checksum = checksum;

  int name_idx = (seq - 1) * 13;

  for (int i = 0; i < 5; i++) {
    if (name[name_idx] != '\0') {
      lfn->name1[i] = name[name_idx++];
    } else {
      lfn->name1[i] =
          (name_idx == strlen(name)) ? 0x0000 : 0xFFFF;  // Padding rules
      name_idx = strlen(name);                           // Stop reading
    }
  }
  for (int i = 0; i < 6; i++) {
    if (name[name_idx] != '\0') {
      lfn->name2[i] = name[name_idx++];
    } else {
      lfn->name2[i] = (name_idx == strlen(name)) ? 0x0000 : 0xFFFF;
      name_idx = strlen(name);
    }
  }
  for (int i = 0; i < 2; i++) {
    if (name[name_idx] != '\0') {
      lfn->name3[i] = name[name_idx++];
    } else {
      lfn->name3[i] = (name_idx == strlen(name)) ? 0x0000 : 0xFFFF;
      name_idx = strlen(name);
    }
  }
}

static inline int device_read(struct block_device* dev, size_t lba,
                              size_t sector_count, void* buffer) {
  if (!dev->ops || !dev->ops->read_sectors) {
    return -1;
  }

  return dev->ops->read_sectors(dev, buffer, lba, sector_count);
}

static inline int device_write(struct block_device* dev, size_t lba,
                               size_t sector_count, const void* buffer) {
  if (!dev->ops || !dev->ops->write_sectors) {
    return -1;
  }

  return dev->ops->write_sectors(dev, buffer, lba, sector_count);
}

static struct inode* create_inode(struct superblock* sb,
                                  struct fat32_inode* parent, uint16_t mode,
                                  uint32_t cluster, uint32_t size,
                                  uint32_t entry_cluster,
                                  uint32_t entry_index) {
  if (!sb->private_data) {
    return NULL;
  }

  struct fat32_inode* fi = kmalloc(sizeof(*fi));

  if (!fi) {
    return NULL;
  }

  memset(fi, 0, sizeof(*fi));

  struct fat32_sb_info* fsb = sb->private_data;

  fi->device = fsb->device;
  fi->cached_physical_cluster = cluster;
  fi->cached_logical_cluster = 0;
  fi->first_cluster = cluster;
  fi->file_size = size;
  fi->parent = parent;
  fi->entry_cluster = entry_cluster;
  fi->entry_index = entry_index;

  struct inode* inode = kmalloc(sizeof(*inode));

  if (!inode) {
    kfree(fi);
    return NULL;
  }

  memset(inode, 0, sizeof(*inode));

  inode->inode_num = fsb->current_inode_num++;
  inode->mode = mode;
  inode->size = size;
  inode->blocks =
      (size + fsb->cluster_size_bytes - 1) / fsb->cluster_size_bytes;
  inode->private_data = fi;
  inode->sb = sb;
  inode->refcount = 0;

  mutex_init(&inode->lock);

  if (VFS_ISDIR(mode)) {
    inode->fops = &fat32_dir_ops;
  } else {
    inode->fops = &fat32_file_ops;
  }

  fi->vfs = inode;
  return inode;
}

void fat32_destroy(struct inode* inode) {
  if (!inode) {
    return;
  }

  struct fat32_inode* fi = inode->private_data;

  if (fi) {
    kfree(fi);
  }

  kfree(inode);
}

static uint32_t get_next_cluster(struct fat32_sb_info* fs,
                                 uint32_t current_cluster) {
  uint32_t entries_per_sector = fs->bytes_per_sector / sizeof(uint32_t);
  uint32_t fat_sector =
      fs->fat_start_lba + (current_cluster / entries_per_sector);
  uint32_t fat_offset = (current_cluster % entries_per_sector);
  uint32_t* fat_buffer = kmalloc(fs->bytes_per_sector);

  if (!fat_buffer) {
    return FAT32_EOC;  // Indicate end of cluster chain on error
  }

  int result = device_read(fs->device, fat_sector, 1, fat_buffer);

  if (result < 0) {
    kfree(fat_buffer);
    return FAT32_EOC;  // Indicate end of cluster chain on error
  }

  uint32_t next_cluster = fat_buffer[fat_offset];

  kfree(fat_buffer);
  next_cluster &= 0x0FFFFFFF;
  return next_cluster;
}

static bool is_short_name_exists(struct fat32_sb_info* fsb,
                                 uint32_t parent_cluster, const char* name) {
  void* buffer = kmalloc(fsb->cluster_size_bytes);

  if (!buffer) {
    // we assume the name exists if we can't allocate memory to check
    // other wise create a collision
    return true;
  }

  uint32_t current_cluster = parent_cluster;
  size_t entries_per_cluster =
      (fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry));

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    struct fat32_dir_entry* dir_entries = buffer;

    uint32_t sector = cluster_to_sector(fsb, current_cluster);
    int result =
        device_read(fsb->device, sector, fsb->sectors_per_cluster, dir_entries);

    if (result < 0) {
      kfree(dir_entries);
      // we assume the name exists if we can't allocate memory to check
      // other wise create a collision
      return true;
    }

    for (size_t i = 0; i < entries_per_cluster; i++) {
      if (dir_entries[i].name[0] == 0x00) {
        break;
      }

      if ((dir_entries[i].attributes & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
        continue;
      }

      if (memcmp(dir_entries[i].name, name, 11) == 0) {
        kfree(dir_entries);
        return true;
      }
    }

    // Move to the next cluster in the directory chain
    current_cluster = get_next_cluster(fsb, current_cluster);
  }

  kfree(buffer);
  return false;  // Return empty entry if not found
}

static void generate_short_name(struct fat32_sb_info* fs,
                                uint32_t parent_cluster, const char* long_name,
                                char* out_name) {
  char base[9] = "        ";  // 8 spaces
  char ext[4] = "   ";        // 3 spaces

  int i = 0, b = 0;

  // 1. Extract and sanitize up to 6 characters for the base
  while (long_name[i] != '\0' && long_name[i] != '.' && b < 6) {
    char c = long_name[i++];
    if (c == ' ' || c == '+' || c == ',' || c == ';' || c == '=')
      continue;                         // Skip invalid FAT chars
    if (c >= 'a' && c <= 'z') c -= 32;  // Convert to uppercase
    base[b++] = c;
  }

  // 2. Extract the extension (if any)
  const char* dot = strrchr(long_name, '.');  // Find the last dot
  if (dot != NULL) {
    int e = 0;
    dot++;  // Skip the dot
    while (dot[e] != '\0' && e < 3) {
      char c = dot[e];
      if (c >= 'a' && c <= 'z') c -= 32;
      ext[e] = c;
      e++;
    }
  }

  // 3. Find a unique numeric tail (~1, ~2, ~3...)
  int tail_num = 1;
  char candidate[12];  // 11 chars + null terminator

  while (tail_num <= 9999) {
    // Clear the candidate with spaces
    for (int k = 0; k < 11; k++) candidate[k] = ' ';
    candidate[11] = '\0';

    // Copy the base
    for (int k = 0; k < b; k++) candidate[k] = base[k];

    // Append the numeric tail (e.g., "~1")
    char tail_str[8];
    int tail_len = 0;

    // Basic integer-to-string for the tail (since we don't have sprintf)
    tail_str[tail_len++] = '~';
    if (tail_num < 10) {
      tail_str[tail_len++] = '0' + tail_num;
    } else {
      // Simplified for example: handle up to ~99
      tail_str[tail_len++] = '0' + (tail_num / 10);
      tail_str[tail_len++] = '0' + (tail_num % 10);
    }

    // Insert the tail right after the base characters
    for (int k = 0; k < tail_len; k++) {
      candidate[b + k] = tail_str[k];
    }

    // Copy the extension into bytes 8, 9, 10
    for (int k = 0; k < 3; k++) {
      candidate[8 + k] = ext[k];
    }

    // 4. Test it against the disk!
    if (!is_short_name_exists(fs, parent_cluster, candidate)) {
      // We found a free slot! Copy it to the output and return.
      for (int k = 0; k < 11; k++) out_name[k] = candidate[k];
      return;
    }

    // Name is taken, increment and try the next number
    tail_num++;
  }
}

static int set_fat_entry(struct fat32_sb_info* fs, uint32_t cluster_no,
                         uint32_t value) {
  // 128 entries per 512-byte sector (512 / 4 = 128)
  uint32_t entries_per_sector = fs->bytes_per_sector / sizeof(uint32_t);
  uint32_t fat_sector = fs->fat_start_lba + (cluster_no / entries_per_sector);
  uint32_t fat_offset = (cluster_no % entries_per_sector);
  uint32_t* fat_buffer = kmalloc(fs->bytes_per_sector);

  if (!fat_buffer) {
    return -1;
  }

  int result = device_read(fs->device, fat_sector, 1, fat_buffer);

  if (result < 0) {
    kfree(fat_buffer);
    return -1;
  }

  fat_buffer[fat_offset] = value;  // Ensure upper 4 bits are zero
  result = device_write(fs->device, fat_sector, 1, fat_buffer);

  if (result < 0) {
    kfree(fat_buffer);
    return -1;
  }

  kfree(fat_buffer);
  return 0;
}

static uint32_t allocate_free_cluster(struct fat32_sb_info* fsb) {
  uint32_t total_clusters = fsb->total_sectors / fsb->sectors_per_cluster;

  for (uint32_t i = 2; i < total_clusters; i++) {
    uint32_t val = get_next_cluster(fsb, i);

    // free cluster found (0x00000000 means free)
    if (val == 0x00000000) {
      // nothing else claims it
      int result;
      result = set_fat_entry(fsb, i, FAT32_EOC);

      if (result < 0) {
        return 0;  // Failed to allocate
      }

      uint8_t* blank_buf = kmalloc(fsb->cluster_size_bytes);

      if (!blank_buf) {
        return 0;  // Allocation failed
      }

      memset(blank_buf, 0, fsb->cluster_size_bytes);

      uint32_t sector = cluster_to_sector(fsb, i);
      size_t sector_count = fsb->sectors_per_cluster;

      result = device_write(fsb->device, sector, sector_count, blank_buf);

      if (result < 0) {
        kfree(blank_buf);
        return 0;  // Failed to write
      }

      kfree(blank_buf);
      return i;
    }
  }

  return 0;  // Disk is full!
}

static int find_entry(struct inode* dir, const char* name,
                      struct fat32_dir_entry* final_entry,
                      uint32_t* found_cluster, uint32_t* found_index) {
  struct fat32_inode* fi = dir->private_data;

  if (!fi || !dir->sb) {
    return -1;
  }

  struct fat32_sb_info* fsb = dir->sb->private_data;

  if (!fsb) {
    return -2;
  }

  uint8_t* buffer = kmalloc(fsb->cluster_size_bytes);

  if (!buffer) {
    return -3;
  }

  int max_entries = fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry);

  char target_short_name[11] = {0};
  memset(target_short_name, ' ', 11);  // Fill with SPACES
  to_fat_filename(name, target_short_name);

  // --- state machine variables ---
  char lfn_buffer[256] = {0};  // holds the assembled long name
  uint8_t current_checksum = 0;
  uint32_t current_cluster = fi->first_cluster;
  bool has_lfn = false;  // flag: are we currently tracking an lfn?

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    const uint32_t sector = cluster_to_sector(fsb, current_cluster);
    const uint32_t sector_count = fsb->sectors_per_cluster;
    int result = device_read(fsb->device, sector, sector_count, buffer);

    if (result < 0) {
      kfree(buffer);
      return -4;
    }

    struct fat32_dir_entry* entries = (struct fat32_dir_entry*)buffer;

    for (int i = 0; i < max_entries; i++) {
      // 1. end of directory?
      if (entries[i].name[0] == 0x00) {
        kfree(buffer);
        return -5;  // Not found, end of directory
      }

      // 2. deleted file? reset lfn state and skip.
      if ((unsigned char)entries[i].name[0] == 0xE5) {
        has_lfn = false;
        continue;
      }

      // 3. is it an lfn entry?
      if (entries[i].attributes == FAT_ATTR_LFN) {
        struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)&entries[i];

        // if this is the last chunk of the string
        if (lfn->order & 0x40) {
          memset(lfn_buffer, 0, sizeof(lfn_buffer));
          has_lfn = true;
          current_checksum = lfn->checksum;
        }

        // only process if the checksums match the chain we are building
        if (has_lfn && lfn->checksum == current_checksum) {
          extract_lfn_chars(lfn, lfn_buffer);
        } else {
          has_lfn = false;  // something broke, discard this lfn
        }
        continue;
      }

      // 4. it is a standard 8.3 entry!
      // volume labels aren't files, skip them.
      if (entries[i].attributes & FAT_ATTR_VOLUME_ID) {
        has_lfn = false;
        continue;
      }

      // -- did we find the file? --
      bool match = false;

      // case a: verify and check the long file name
      if (has_lfn &&
          lfn_checksum((uint8_t*)entries[i].name) == current_checksum &&
          strcmp(lfn_buffer, name) == 0) {
        match = true;
      }

      // case b: fallback check against the short 8.3 name
      if (!match && memcmp(&entries[i].name, target_short_name, 11) == 0) {
        match = true;
      }

      // if either matched, we are done!
      if (match) {
        if (final_entry) {
          memcpy(final_entry, &entries[i], sizeof(struct fat32_dir_entry));
        }

        if (found_cluster) {
          *found_cluster = current_cluster;
        }

        if (found_index) {
          *found_index = i;
        }

        kfree(buffer);
        return 0;  // Success
      }

      has_lfn = false;
    }

    // We didn't find it in this cluster, so look up the next one!
    current_cluster = get_next_cluster(fsb, current_cluster);
  }

  kfree(buffer);
  return -6;
}

static int remove_entry(struct inode* dir, const char* name,
                        struct fat32_dir_entry* final_entry) {
  struct fat32_inode* fi = dir->private_data;

  if (!fi || !dir->sb) {
    return -1;
  }

  struct fat32_sb_info* fsb = dir->sb->private_data;

  if (!fsb) {
    return -1;
  }

  uint8_t* buffer = kmalloc(fsb->cluster_size_bytes);

  if (!buffer) {
    return -1;
  }

  int max_entries = fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry);

  char target_short_name[11] = {0};
  memset(target_short_name, ' ', 11);  // Fill with SPACES
  to_fat_filename(name, target_short_name);

  // --- state machine variables ---
  char lfn_buffer[256] = {0};  // holds the assembled long name
  uint8_t current_checksum = 0;
  uint32_t current_cluster = fi->first_cluster;
  bool has_lfn = false;  // flag: are we currently tracking an lfn?
  int found_index = -1;

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    uint32_t sector = cluster_to_sector(fsb, current_cluster);
    const uint32_t sector_count = fsb->sectors_per_cluster;
    int result = device_read(fsb->device, sector, sector_count, buffer);

    if (result < 0) {
      kfree(buffer);
      return -1;
    }

    struct fat32_dir_entry* entries = (struct fat32_dir_entry*)buffer;

    for (int i = 0; i < max_entries; i++) {
      // 1. end of directory?
      if (entries[i].name[0] == 0x00) {
        kfree(buffer);
        return -1;  // Not found, end of directory
      }

      // 2. deleted file? reset lfn state and skip.
      if ((unsigned char)entries[i].name[0] == 0xE5) {
        has_lfn = false;
        found_index = -1;
        continue;
      }

      // 3. is it an lfn entry?
      if (entries[i].attributes == FAT_ATTR_LFN) {
        struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)&entries[i];

        // if this is the last chunk of the string
        if (lfn->order & 0x40) {
          memset(lfn_buffer, 0, sizeof(lfn_buffer));
          has_lfn = true;
          found_index = i;  // mark the start of the LFN chain
          current_checksum = lfn->checksum;
        }

        // only process if the checksums match the chain we are building
        if (has_lfn && lfn->checksum == current_checksum) {
          extract_lfn_chars(lfn, lfn_buffer);
        } else {
          has_lfn = false;   // something broke, discard this lfn
          found_index = -1;  // reset the found index
        }
        continue;
      }

      // 4. it is a standard 8.3 entry!
      // volume labels aren't files, skip them.
      if (entries[i].attributes & FAT_ATTR_VOLUME_ID) {
        has_lfn = false;
        found_index = -1;
        continue;
      }

      // -- did we find the file? --
      bool match = false;

      // case a: verify and check the long file name
      if (has_lfn &&
          lfn_checksum((uint8_t*)entries[i].name) == current_checksum &&
          strcmp(lfn_buffer, name) == 0) {
        match = true;
      }

      // case b: fallback check against the short 8.3 name
      if (!match && memcmp(&entries[i].name, target_short_name, 11) == 0) {
        match = true;
      }

      // if either matched, we are done!
      if (match) {
        if (final_entry) {
          memcpy(final_entry, &entries[i], sizeof(struct fat32_dir_entry));
        }

        if (found_index != -1) {
          // Mark the LFN entries as deleted
          for (int j = found_index; j <= i; j++) {
            entries[j].name[0] = 0xE5;  // Mark as deleted
          }
        } else {
          // Mark the 8.3 entry as deleted
          entries[i].name[0] = 0xE5;  // Mark as deleted
        }

        // Write the modified cluster back to disk
        result = device_write(fsb->device, sector, sector_count, buffer);

        if (result < 0) {
          kfree(buffer);
          return -1;
        }

        kfree(buffer);
        return 0;  // Success
      }

      has_lfn = false;
      found_index = -1;
    }

    // We didn't find it in this cluster, so look up the next one!
    current_cluster = get_next_cluster(fsb, current_cluster);
  }

  kfree(buffer);
  return -1;  // Not found
}

static int create_entry(struct inode* dir, const char* name,
                        struct fat32_dir_entry* entry_data) {
  if (!dir->private_data || !dir->sb || !dir->sb->private_data) {
    return -1;
  }

  struct fat32_inode* fi = dir->private_data;
  struct fat32_sb_info* fsb = dir->sb->private_data;

  const uint32_t parent_cluster = fi->first_cluster;
  char short_name[11] = {0};
  memset(short_name, ' ', 11);  // Fill with SPACES
  // Generates e.g. "MYAWES~1   "
  generate_short_name(fsb, parent_cluster, name, short_name);

  const int name_len = strlen(name);

  // How many 13-char LFN entries do we need?
  const int lfn_count = (name_len + 12) / 13;

  // LFN entries + 1 standard short name entry
  const int total_slots_needed = lfn_count + 1;
  const uint8_t checksum = lfn_checksum((unsigned char*)short_name);
  void* buffer = kmalloc(fsb->cluster_size_bytes);

  if (!buffer) {
    return -1;
  }

  const int max_entries =
      fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry);
  uint32_t current_cluster = parent_cluster;
  uint32_t previous_cluster = 0;
  bool found_free_slots = false;
  int entry_index = -1;
  int result;

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    const uint32_t lba = cluster_to_sector(fsb, current_cluster);
    result = device_read(fsb->device, lba, fsb->sectors_per_cluster, buffer);

    if (result < 0) {
      kfree(buffer);
      return -1;
    }

    struct fat32_dir_entry* parent_entries = buffer;

    // Find consecutive free slots (`0x00` or `0xE5`)
    for (int i = 0; i <= max_entries - total_slots_needed; i++) {
      bool contiguous = true;

      for (int j = 0; j < total_slots_needed; j++) {
        if (parent_entries[i + j].name[0] != 0x00 &&
            (unsigned char)parent_entries[i + j].name[0] != 0xE5) {
          contiguous = false;
          break;
        }
      }

      if (contiguous) {
        entry_index = i;
        found_free_slots = true;
        break;
      }
    }

    if (found_free_slots) {
      break;
    }

    previous_cluster = current_cluster;
    current_cluster = get_next_cluster(fsb, current_cluster);
  }

  if (!found_free_slots) {
    // allocate a new cluster
    uint32_t new_cluster = allocate_free_cluster(fsb);

    if (new_cluster == 0) {
      kfree(buffer);
      return -1;
    }

    // link the new cluster to the parent directory's cluster chain
    result = set_fat_entry(fsb, previous_cluster, new_cluster);

    if (result < 0) {
      // free allocated cluster if linking failed
      kfree(buffer);
      return -1;  // Failed to link new cluster
    }

    current_cluster = new_cluster;
    entry_index = 0;  // Start at the beginning of the new cluster
    memset(buffer, 0, fsb->cluster_size_bytes);  // Clear the new cluster
  }

  // Write the LFN entries in REVERSE order starting from entry_index
  struct fat32_lfn_entry* lfn_entries = buffer;

  for (int i = 0; i < lfn_count; i++) {
    int seq = lfn_count - i;
    int is_last = (seq == lfn_count);

    create_lfn_entry(&lfn_entries[entry_index + i], name, seq, is_last,
                     checksum);
  }

  struct fat32_dir_entry* parent_entries = buffer;

  // Write the Standard 8.3 Short Name Entry immediately after the LFN
  // entries
  const int short_index = entry_index + lfn_count;
  memcpy(parent_entries[short_index].name, short_name, 11);

  parent_entries[short_index].attributes = entry_data->attributes;
  parent_entries[short_index].cluster_high = entry_data->cluster_high;
  parent_entries[short_index].cluster_low = entry_data->cluster_low;
  parent_entries[short_index].file_size = entry_data->file_size;

  parent_entries[short_index].reserved_nt = entry_data->reserved_nt;
  parent_entries[short_index].creation_time_tenths =
      entry_data->creation_time_tenths;
  parent_entries[short_index].creation_time = entry_data->creation_time;
  parent_entries[short_index].creation_date = entry_data->creation_date;
  parent_entries[short_index].last_access_date = entry_data->last_access_date;
  parent_entries[short_index].last_mod_time = entry_data->last_mod_time;
  parent_entries[short_index].last_mod_date = entry_data->last_mod_date;

  const uint32_t cluster_lba = cluster_to_sector(fsb, current_cluster);
  const uint32_t sector_count = fsb->sectors_per_cluster;

  // Flush the updated parent directory buffer back to the disk
  result = device_write(fsb->device, cluster_lba, sector_count, buffer);

  if (result < 0) {
    kfree(buffer);
    return -1;
  }

  kfree(buffer);
  return 0;
}

static int modify_entry(struct inode* dir, const char* name,
                        uint32_t new_cluster, uint32_t new_size) {
  struct fat32_inode* fi = dir->private_data;

  if (!fi || !dir->sb) {
    return -1;
  }

  struct fat32_sb_info* fsb = dir->sb->private_data;

  if (!fsb) {
    return -1;
  }

  uint8_t* buffer = kmalloc(fsb->cluster_size_bytes);

  if (!buffer) {
    return -1;
  }

  int max_entries = fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry);

  char target_short_name[11] = {0};
  to_fat_filename(name, target_short_name);

  // --- state machine variables ---
  char lfn_buffer[256] = {0};  // holds the assembled long name
  uint8_t current_checksum = 0;
  uint32_t current_cluster = fi->first_cluster;
  bool has_lfn = false;  // flag: are we currently tracking an lfn?
  bool found = false;

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    uint32_t sector = cluster_to_sector(fsb, current_cluster);
    const uint32_t sector_count = fsb->sectors_per_cluster;
    int result = device_read(fsb->device, sector, sector_count, buffer);

    if (result < 0) {
      kfree(buffer);
      return -1;
    }

    struct fat32_dir_entry* entries = (struct fat32_dir_entry*)buffer;

    for (int i = 0; i < max_entries; i++) {
      // 1. end of directory?
      if (entries[i].name[0] == 0x00) {
        break;
      }

      // 2. deleted file? reset lfn state and skip.
      if ((unsigned char)entries[i].name[0] == 0xE5) {
        has_lfn = false;
        continue;
      }

      // 3. is it an lfn entry?
      if (entries[i].attributes == FAT_ATTR_LFN) {
        struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)&entries[i];

        // if this is the last chunk of the string
        if (lfn->order & 0x40) {
          memset(lfn_buffer, 0, sizeof(lfn_buffer));
          has_lfn = true;
          current_checksum = lfn->checksum;
        }

        // only process if the checksums match the chain we are building
        if (has_lfn && lfn->checksum == current_checksum) {
          extract_lfn_chars(lfn, lfn_buffer);
        } else {
          has_lfn = false;  // something broke, discard this lfn
        }
        continue;
      }

      // 4. it is a standard 8.3 entry!
      // volume labels aren't files, skip them.
      if (entries[i].attributes & FAT_ATTR_VOLUME_ID) {
        has_lfn = false;
        continue;
      }

      // -- did we find the file? --
      bool match = false;

      // case a: verify and check the long file name
      if (has_lfn &&
          lfn_checksum((uint8_t*)entries[i].name) == current_checksum &&
          strcmp(lfn_buffer, name) == 0) {
        match = true;
      }

      // case b: fallback check against the short 8.3 name
      if (!match && memcmp(&entries[i].name, target_short_name, 11) == 0) {
        match = true;
      }

      // if either matched, we are done!
      if (match) {
        entries[i].cluster_high = (new_cluster >> 16) & 0xFFFF;
        entries[i].cluster_low = new_cluster & 0xFFFF;
        entries[i].file_size = new_size;

        // Write the modified cluster back to disk
        result = device_write(fsb->device, sector, sector_count, buffer);

        if (result < 0) {
          kfree(buffer);
          return -1;
        }

        found = true;
        break;
      }

      has_lfn = false;
    }

    if (found) {
      break;
    }

    // We didn't find it in this cluster, so look up the next one!
    current_cluster = get_next_cluster(fsb, current_cluster);
  }

  kfree(buffer);
  return 0;
}

static inline struct superblock* _create_superblock(
    struct block_device* block_dev) {
  if (!block_dev) {
    return NULL;
  }

  struct fat32_sb_info* fsb = kmalloc(sizeof(*fsb));

  if (!fsb) {
    return NULL;
  }

  memset(fsb, 0, sizeof(*fsb));

  fsb->device = block_dev;

  uint8_t* buffer = kmalloc(fsb->device->sector_size);

  if (!buffer) {
    kfree(fsb);
    return NULL;
  }

  int result;
  result = device_read(fsb->device, 0, 1, buffer);

  if (result < 0) {
    kfree(buffer);
    kfree(fsb);
    return NULL;
  }

  struct fat32_bpb* bpb = (struct fat32_bpb*)buffer;

  // A standard FAT32 drive has 0 root directory entries (it uses clusters
  // instead) and fat_size_16 must be 0 (because it uses fat_size_32).
  if (bpb->root_dir_entries != 0 || bpb->fat_size_16 != 0) {
    kfree(buffer);
    kfree(fsb);
    return NULL;
  }

  fsb->bytes_per_sector = bpb->bytes_per_sector;
  fsb->sectors_per_cluster = bpb->sectors_per_cluster;
  fsb->cluster_size_bytes = fsb->bytes_per_sector * fsb->sectors_per_cluster;
  fsb->total_sectors = bpb->total_sectors_32;
  fsb->root_dir_cluster = bpb->root_cluster;
  fsb->fat_start_lba = bpb->reserved_sectors;

  // The Data region starts after all the FAT tables (usually 2 of them)
  // Formula: reserved_sectors + (number_of_FATs * sectors_per_FAT)
  fsb->data_start_lba =
      bpb->reserved_sectors + (bpb->fat_count * bpb->fat_size_32);

  kfree(buffer);

  struct superblock* sb = kmalloc(sizeof(*sb));

  if (!sb) {
    kfree(fsb);
    return NULL;
  }

  memset(sb, 0, sizeof(*sb));

  sb->magic = 0x32;
  sb->block_size = fsb->bytes_per_sector;
  sb->total_blocks = fsb->total_sectors;
  sb->free_blocks = 0;
  sb->total_inodes = 0;
  sb->free_inodes = 0;
  sb->private_data = fsb;
  sb->sops = NULL;

  struct inode* inode =
      create_inode(sb, NULL, VFS_IFDIR | 0755, fsb->root_dir_cluster, 0, 0, 0);

  if (!inode) {
    kfree(fsb);
    kfree(sb);
    return NULL;
  }

  sb->root = inode;

  return sb;
}

static struct superblock* fat32_mount(struct inode* device_inode) {
  struct devfs_inode* devfs_inode = device_inode->private_data;

  if (!devfs_inode || !devfs_inode->device) {
    return NULL;
  }

  struct block_device* block_dev = devfs_inode->device->private_data;
  return _create_superblock(block_dev);
}

struct superblock* fat32_create_superblock(struct block_device* block_dev) {
  return _create_superblock(block_dev);
}

int fat32_destroy_superblock(struct superblock* sb) {
  if (!sb) {
    return -1;
  }

  struct fat32_sb_info* fsb = sb->private_data;

  if (fsb) {
    kfree(fsb);
  }

  kfree(sb);
  return 0;
}

struct inode* fat32_lookup(struct inode* dir, const char* name) {
  if (name[0] == '\0') {
    return NULL;
  }

  struct fat32_inode* fi = dir->private_data;

  if (!fi) {
    return NULL;
  }

  if (strcmp(name, ".") == 0) {
    return dir;
  }

  if (strcmp(name, "..") == 0) {
    struct fat32_inode* parent = fi->parent ? fi->parent : fi;

    if (!parent->vfs) {
      return NULL;
    }

    return parent->vfs;
  }

  struct fat32_dir_entry entry;
  uint32_t found_cluster;
  uint32_t found_index;
  int result = find_entry(dir, name, &entry, &found_cluster, &found_index);

  if (result < 0) {
    return NULL;
  }

  if (!dir->sb || !dir->sb->private_data) {
    return NULL;
  }

  uint16_t mode;

  if (entry.attributes & FAT_ATTR_DIRECTORY) {
    mode = VFS_IFDIR | 0755;
  } else {
    mode = VFS_IFREG | 0644;
  }

  uint32_t cluster = get_full_cluster(&entry);
  uint32_t size = entry.file_size;

  return create_inode(dir->sb, dir->private_data, mode, cluster, size,
                      found_cluster, found_index);
}

int fat32_open_dir(struct file* file) {
  if (!file->inode || !file->inode->private_data) {
    return -1;
  }

  struct fat32_inode* fi = file->inode->private_data;

  file->f_pos = 0;

  if (!VFS_ISDIR(file->inode->mode)) {
    return -1;
  }

  struct fat32_dir_info* info = kmalloc(sizeof(*info));

  if (!info) {
    return -1;
  }

  info->current_cluster = fi->first_cluster;
  info->entry_index = 0;
  info->state = FAT_READDIR_DOT;

  file->private_data = info;
  return 0;
}

int fat32_close_dir(struct file* file) {
  if (file->private_data) {
    kfree(file->private_data);
    file->private_data = NULL;
    return 0;
  }

  return -1;
}

int fat32_readdir(struct file* file, void* ctx,
                  int (*fill)(void*, const char*, uint32_t, uint32_t)) {
  if (!file->inode || !fill || !file->inode->private_data) {
    return -1;
  }

  struct fat32_inode* fi = file->inode->private_data;

  if (!fi) {
    return -1;
  }

  struct fat32_dir_info* info = file->private_data;

  if (!info || !file->inode->sb) {
    return -1;
  }

  struct fat32_sb_info* fsb = file->inode->sb->private_data;

  if (!fsb) {
    return -1;
  }

  uint8_t* buffer = kmalloc(fsb->cluster_size_bytes);

  if (!buffer) {
    return -1;
  }

  int max_entries = fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry);

  // --- state machine variables ---
  char lfn_buffer[256] = {0};  // holds the assembled long name
  bool has_lfn = false;        // flag: are we currently tracking an lfn?
  uint8_t current_checksum = 0;
  uint32_t current_cluster = fi->first_cluster;

  while (info->state != FAT_READDIR_END) {
    if (info->state == FAT_READDIR_DOT) {
      if (fill(ctx, ".", file->inode->inode_num, VFS_DT_DIR) != 0) {
        kfree(buffer);
        return 0;
      }

      info->state = FAT_READDIR_DOTDOT;
      continue;
    }

    if (info->state == FAT_READDIR_DOTDOT) {
      struct fat32_inode* parent = fi->parent ? fi->parent : fi;

      if (!parent->vfs) {
        kfree(buffer);
        return -1;
      }

      if (fill(ctx, "..", parent->vfs->inode_num, VFS_DT_DIR) != 0) {
        kfree(buffer);
        return 0;
      }

      info->state = FAT_READDIR_CHILDREN;
      continue;
    }

    if (info->state == FAT_READDIR_CHILDREN) {
      uint32_t sector = cluster_to_sector(fsb, current_cluster);
      const size_t sector_count = fsb->sectors_per_cluster;

      int result;

      result = device_read(fsb->device, sector, sector_count, buffer);

      if (result < 0) {
        kfree(buffer);
        return -1;
      }

      struct fat32_dir_entry* entries = (struct fat32_dir_entry*)buffer;

      for (int i = 0; i < max_entries; i++) {
        // 1. end of directory?
        if (entries[i].name[0] == 0x00) {
          info->state = FAT_READDIR_END;
          kfree(buffer);
          return 0;
        }

        // 2. deleted file? reset lfn state and skip.
        if ((unsigned char)entries[i].name[0] == 0xE5) {
          has_lfn = false;
          continue;
        }

        // 3. is it an lfn entry?
        if (entries[i].attributes == FAT_ATTR_LFN) {
          struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)&entries[i];

          // if this is the last chunk of the string (the first one we read on
          // disk)
          if (lfn->order & 0x40) {
            memset(lfn_buffer, (int)'\0', sizeof(lfn_buffer));
            has_lfn = true;
            current_checksum = lfn->checksum;
          }

          // only process if the checksums match the chain we are building
          if (has_lfn && lfn->checksum == current_checksum) {
            extract_lfn_chars(lfn, lfn_buffer);
          } else {
            has_lfn = false;  // something broke, discard this lfn
          }
          continue;
        }

        // 4. it is a standard 8.3 entry!
        // volume labels aren't files, skip them.
        if (entries[i].attributes & FAT_ATTR_VOLUME_ID) {
          has_lfn = false;
          continue;
        }

        uint32_t inode_num = fi->vfs->inode_num;
        uint32_t type = VFS_DT_REG;

        if (entries[i].attributes & FAT_ATTR_DIRECTORY) {
          type = VFS_DT_DIR;
        }

        if (has_lfn &&
            lfn_checksum((unsigned char*)entries[i].name) == current_checksum) {
          if (fill(ctx, lfn_buffer, inode_num, type) != 0) {
            kfree(buffer);
            return 0;
          }
        }

        if (!has_lfn) {
          if (entries[i].name[0] == 0x2E) {
            // Skip "." and ".." entries, they are handled separately
            continue;
          }

          char short_name[12] = {0};
          memcpy(short_name, entries[i].name, 11);
          short_name[11] = '\0';

          if (fill(ctx, short_name, inode_num, type) != 0) {
            kfree(buffer);
            return 0;
          }
        }

        // reset state for the next file
        has_lfn = false;
      }

      current_cluster = get_next_cluster(fsb, current_cluster);

      if (current_cluster >= FAT32_EOC || current_cluster == 0) {
        info->state = FAT_READDIR_END;
        break;
      }
    }
  }

  kfree(buffer);
  return 0;
}

int fat32_mkdir(struct inode* dir, const char* name, uint32_t mode) {
  UNUSED(mode);

  if (!dir->private_data || !dir->sb->private_data) {
    return -1;
  }

  struct fat32_inode* fi = dir->private_data;
  struct fat32_sb_info* fsb = dir->sb->private_data;

  // check if same name already exists in the parent directory
  if (find_entry(dir, name, NULL, NULL, NULL) == 0) {
    return -1;  // Name already exists
  }

  const uint32_t new_cluster = allocate_free_cluster(fsb);

  if (new_cluster == 0) {
    return -1;
  }

  uint8_t* new_dir_buf = kmalloc(fsb->cluster_size_bytes);

  if (!new_dir_buf) {
    return -1;
  }

  memset(new_dir_buf, 0, fsb->cluster_size_bytes);

  struct fat32_dir_entry* dot_entries = (struct fat32_dir_entry*)new_dir_buf;

  memcpy(dot_entries[0].name, ".          ", 11);
  dot_entries[0].attributes = FAT_ATTR_DIRECTORY;
  dot_entries[0].cluster_high = (new_cluster >> 16) & 0xFFFF;
  dot_entries[0].cluster_low = new_cluster & 0xFFFF;

  memcpy(dot_entries[1].name, "..         ", 11);
  dot_entries[1].attributes = FAT_ATTR_DIRECTORY;

  const uint32_t parent_cluster = fi->first_cluster;
  dot_entries[1].cluster_high = (parent_cluster >> 16) & 0xFFFF;
  dot_entries[1].cluster_low = parent_cluster & 0xFFFF;

  const uint32_t child_lba = cluster_to_sector(fsb, new_cluster);
  const uint32_t sector_count = fsb->sectors_per_cluster;

  int result;
  result = device_write(fsb->device, child_lba, sector_count, new_dir_buf);

  kfree(new_dir_buf);

  if (result < 0) {
    return -1;
  }

  struct fat32_dir_entry new_entry = {0};

  new_entry.attributes = FAT_ATTR_DIRECTORY;
  new_entry.cluster_high = (new_cluster >> 16) & 0xFFFF;
  new_entry.cluster_low = new_cluster & 0xFFFF;

  result = create_entry(dir, name, &new_entry);

  if (result < 0) {
    return -1;  // Failed to create directory entry
  }

  return 0;  // Success!
}

static int is_directory_empty(struct fat32_sb_info* fsb,
                              uint32_t target_cluster) {
  // Verify the directory is empty
  void* target_buf = kmalloc(fsb->cluster_size_bytes);

  if (!target_buf) {
    return -1;  // Memory allocation failed
  }

  int max_entries = fsb->cluster_size_bytes / sizeof(struct fat32_dir_entry);
  uint32_t current_cluster = target_cluster;

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    const uint32_t target_sec = cluster_to_sector(fsb, current_cluster);
    int result = device_read(fsb->device, target_sec, fsb->sectors_per_cluster,
                             target_buf);

    if (result < 0) {
      kfree(target_buf);
      return -1;  // Read failed
    }

    struct fat32_dir_entry* entries = target_buf;

    for (int i = 0; i < max_entries; i++) {
      if (entries[i].name[0] == 0x00) break;  // End of list

      if ((unsigned char)entries[i].name[0] == 0xE5) continue;  // Deleted file

      // Skip '.' and '..'
      if (entries[i].name[0] == '.') continue;

      // If we hit ANY other valid file, the directory is not empty!
      kfree(target_buf);
      return -1;  // Error: Directory not empty
    }

    current_cluster = get_next_cluster(fsb, current_cluster);
  }

  kfree(target_buf);
  return 0;  // Directory is empty
}

int fat32_rmdir(struct inode* dir, const char* name) {
  if (!dir->private_data || !dir->sb->private_data) {
    return -1;
  }

  struct fat32_sb_info* fsb = dir->sb->private_data;

  // Find the target directory entry in the parent
  struct fat32_dir_entry target_entry;
  int result;

  result = find_entry(dir, name, &target_entry, NULL, NULL);

  if (result < 0) {
    return -1;  // Directory not found
  }

  if (!(target_entry.attributes & FAT_ATTR_DIRECTORY)) {
    return -1;  // It's a file, not a dir!
  }

  uint32_t target_cluster =
      (target_entry.cluster_high << 16) | target_entry.cluster_low;

  // Verify the directory is empty
  result = is_directory_empty(fsb, target_cluster);

  if (result < 0) {
    return -1;  // Directory not empty
  }

  // Free the clusters in the FAT table
  uint32_t current_cluster = target_cluster;

  while (current_cluster < FAT32_EOC && current_cluster != 0) {
    uint32_t next = get_next_cluster(fsb, current_cluster);
    set_fat_entry(fsb, current_cluster, 0x00000000);  // Mark as free
    current_cluster = next;
  }

  // Mark the entry as deleted in the Parent Directory
  result = remove_entry(dir, name, &target_entry);

  if (result < 0) {
    return -1;  // Failed to remove entry
  }

  return 0;  // Success
}

static uint32_t get_nth_cluster(struct fat32_sb_info* fsb,
                                uint32_t start_cluster, uint32_t n) {
  uint32_t* fat_buffer = kmalloc(fsb->bytes_per_sector);

  if (!fat_buffer) {
    return FAT32_EOC;  // Indicate end of cluster chain on error
  }

  uint32_t current_cluster = start_cluster;
  const uint32_t fat_lba = fsb->fat_start_lba;
  const uint32_t entries_per_sector = fsb->bytes_per_sector / sizeof(uint32_t);

  for (uint32_t i = 0; i < n; i++) {
    const uint32_t fat_sector =
        fat_lba + (current_cluster / entries_per_sector);
    const uint32_t fat_offset = (current_cluster % entries_per_sector);

    int result = device_read(fsb->device, fat_sector, 1, fat_buffer);

    if (result < 0) {
      kfree(fat_buffer);
      return FAT32_EOC;  // Indicate end of cluster chain on error
    }

    current_cluster = fat_buffer[fat_offset];

    if (current_cluster >= FAT32_EOC || current_cluster == 0) {
      kfree(fat_buffer);
      return FAT32_EOC;  // Reached end of cluster chain or invalid cluster
    }
  }

  kfree(fat_buffer);
  return current_cluster & 0x0FFFFFFF;
}

// Allocates `n` clusters starting from `parent_cluster` and returns the last
// allocated cluster.
// note: if parent cluster already has x clusters, then it will allocate n-x
// clusters and return the last one.
static uint32_t allocate_nclusters(struct fat32_sb_info* fsb,
                                   uint32_t parent_cluster, uint32_t n) {
  uint32_t* fat_buffer = kmalloc(fsb->bytes_per_sector);

  if (!fat_buffer) {
    return 0;
  }

  const uint32_t entries_per_sector = fsb->bytes_per_sector / sizeof(uint32_t);
  const uint32_t fat_lba = fsb->fat_start_lba;
  uint32_t previous_cluster = parent_cluster;
  uint32_t current_cluster = parent_cluster;
  uint32_t i;

  for (i = 0; i < n; i++) {
    // 128 entries per 512-byte sector (512 / 4 = 128)
    const uint32_t fat_sector =
        fat_lba + (current_cluster / entries_per_sector);
    const uint32_t fat_offset = (current_cluster % entries_per_sector);

    int result = device_read(fsb->device, fat_sector, 1, fat_buffer);

    if (result < 0) {
      kfree(fat_buffer);
      return 0;
    }

    previous_cluster = current_cluster;
    current_cluster = fat_buffer[fat_offset] & 0x0FFFFFFF;

    if (current_cluster >= FAT32_EOC || current_cluster == 0) {
      current_cluster = previous_cluster;
      break;
    }
  }

  // create cluster chain for remaining clusters
  for (uint32_t j = 0; j < n - i; j++) {
    uint32_t new_cluster = allocate_free_cluster(fsb);

    if (new_cluster == 0) {
      kfree(fat_buffer);
      return 0;
    }

    // Link the new cluster to the previous one
    // 128 entries per 512-byte sector (512 / 4 = 128)
    const uint32_t fat_sector = fat_lba + (current_cluster / 128);
    const uint32_t fat_offset = (current_cluster % 128);

    int result = device_read(fsb->device, fat_sector, 1, fat_buffer);

    if (result < 0) {
      kfree(fat_buffer);
      return 0;
    }

    fat_buffer[fat_offset] = new_cluster;
    current_cluster = new_cluster;

    result = device_write(fsb->device, fat_sector, 1, fat_buffer);

    if (result < 0) {
      kfree(fat_buffer);
      return 0;
    }
  }

  kfree(fat_buffer);
  return current_cluster;
}

int fat32_read(struct file* file, void* buf, size_t len, size_t* off) {
  if (!file || !file->inode || !buf || !off) {
    return -1;
  }

  struct inode* inode = file->inode;
  struct fat32_inode* fi = file->inode->private_data;

  if (!fi || !file->inode->sb || !file->inode->sb->private_data) {
    return -1;
  }

  struct fat32_sb_info* fsb = file->inode->sb->private_data;

  // End of File Check
  if (*off >= inode->size) {
    return 0;  // EOF
  }

  // Bound the read length to the actual file size
  size_t bytes_left = len;

  if (*off + bytes_left > inode->size) {
    bytes_left = inode->size - *off;
  }

  size_t total_bytes_read = 0;
  uint8_t* cluster_buf = kmalloc(fsb->cluster_size_bytes);

  if (!cluster_buf) {
    return -1;  // Memory allocation failed
  }

  while (bytes_left > 0) {
    uint32_t logical_cluster = *off / fsb->cluster_size_bytes;
    uint32_t offset_in_cluster = *off % fsb->cluster_size_bytes;

    uint32_t physical_cluster = 0;

    // -- CACHE --
    // Scenario A: We are exactly where we were last time
    if (logical_cluster == fi->cached_logical_cluster) {
      physical_cluster = fi->cached_physical_cluster;
    }

    // Scenario B: Sequential read! We just crossed into the VERY NEXT
    // cluster.
    else if (logical_cluster > fi->cached_logical_cluster) {
      uint32_t n = logical_cluster - fi->cached_logical_cluster;
      physical_cluster = get_nth_cluster(fsb, fi->cached_physical_cluster, n);
    }

    // Scenario C: Random Seek (lseek). Walk from the beginning.
    else {
      physical_cluster =
          get_nth_cluster(fsb, fi->first_cluster, logical_cluster);
    }

    // Safety check: Did we hit EOF unexpectedly?
    if (physical_cluster >= FAT32_EOC || physical_cluster == 0) {
      break;
    }

    // Update the cache
    fi->cached_logical_cluster = logical_cluster;
    fi->cached_physical_cluster = physical_cluster;

    // -- HARDWARE INTERACTION --
    // Read the actual physical cluster into our temp buffer
    const uint32_t target_sector = cluster_to_sector(fsb, physical_cluster);
    int result = device_read(fsb->device, target_sector,
                             fsb->sectors_per_cluster, cluster_buf);

    if (result < 0) {
      kfree(cluster_buf);
      return -1;  // Read failed
    }

    // -- DATA COPY --
    // How much can we copy from THIS specific cluster?
    // It's either the remaining bytes we need, or the space left in this
    // cluster.
    size_t chunk_size = fsb->cluster_size_bytes - offset_in_cluster;

    if (chunk_size > bytes_left) {
      chunk_size = bytes_left;
    }

    // Copy from the temp buffer (plus offset) into the user's buffer
    char* user_ptr = (char*)buf + total_bytes_read;
    memcpy(user_ptr, cluster_buf + offset_in_cluster, chunk_size);

    // Advance all our pointers
    *off += chunk_size;
    total_bytes_read += chunk_size;
    bytes_left -= chunk_size;
  }

  kfree(cluster_buf);
  return total_bytes_read;
}

int fat32_write(struct file* file, const void* buf, size_t len, size_t* off) {
  if (!file || !file->inode || !buf || !off || len == 0) {
    return 0;
  }

  if (!file->inode->private_data || !file->inode->sb ||
      !file->inode->sb->private_data) {
    return -1;
  }

  struct inode* inode = file->inode;
  struct fat32_inode* fi = inode->private_data;
  struct fat32_sb_info* fsb = inode->sb->private_data;

  size_t bytes_left = len;
  size_t total_bytes_written = 0;
  int result;
  uint8_t* cluster_buf = kmalloc(fsb->cluster_size_bytes);

  // Check if this is a brand new empty file (size 0, no clusters assigned
  // yet)
  if (fi->first_cluster == 0) {
    fi->first_cluster = allocate_free_cluster(fsb);

    if (fi->first_cluster == 0) {
      kfree(cluster_buf);
      return -1;
    }  // Disk Full!

    fi->cached_logical_cluster = 0;
    fi->cached_physical_cluster = fi->first_cluster;
  }

  while (bytes_left > 0) {
    uint32_t logical_cluster = *off / fsb->cluster_size_bytes;
    uint32_t offset_in_cluster = *off % fsb->cluster_size_bytes;
    uint32_t physical_cluster = 0;

    // -- CLUSTER NAVIGATION & ALLOCATION --
    if (logical_cluster == fi->cached_logical_cluster) {
      physical_cluster = fi->cached_physical_cluster;

    } else if (logical_cluster == fi->cached_logical_cluster + 1) {
      uint32_t next = get_next_cluster(fsb, fi->cached_physical_cluster);

      // Did we hit the end of the file? We need to GROW it!
      if (next >= FAT32_EOC || next == 0) {
        next = allocate_free_cluster(fsb);

        if (next == 0) {
          break;
        }

        // Link the old end of the file to this newly allocated cluster
        result = set_fat_entry(fsb, fi->cached_physical_cluster, next);

        if (result < 0) {
          kfree(cluster_buf);
          return -1;  // Failed to link new cluster
        }
      }

      physical_cluster = next;
    } else {
      // Random Seek - Walk the chain from the beginning
      physical_cluster =
          allocate_nclusters(fsb, fi->first_cluster, logical_cluster);
    }

    fi->cached_logical_cluster = logical_cluster;
    fi->cached_physical_cluster = physical_cluster;

    // -- READ-MODIFY-WRITE --
    uint32_t target_sector = cluster_to_sector(fsb, physical_cluster);

    // Only read the existing data from disk if we are doing a partial cluster
    // overwrite
    size_t chunk_size = fsb->cluster_size_bytes - offset_in_cluster;

    if (chunk_size > bytes_left) {
      chunk_size = bytes_left;
    }

    if (chunk_size != fsb->cluster_size_bytes) {
      result = device_read(fsb->device, target_sector, fsb->sectors_per_cluster,
                           cluster_buf);
      if (result < 0) {
        kfree(cluster_buf);
        return -1;  // Read failed
      }
    }

    // Copy the new user data into the cluster buffer
    const char* user_ptr = (const char*)buf + total_bytes_written;
    memcpy(cluster_buf + offset_in_cluster, user_ptr, chunk_size);

    // Write the modified cluster back to the hardware
    result = device_write(fsb->device, target_sector, fsb->sectors_per_cluster,
                          cluster_buf);

    if (result < 0) {
      kfree(cluster_buf);
      return -1;  // Write failed
    }

    // Advance pointers
    *off += chunk_size;
    total_bytes_written += chunk_size;
    bytes_left -= chunk_size;
  }

  // -- UPDATE FILE SIZE --
  if (*off > fi->file_size) {
    fi->file_size = *off;
    inode->size = *off;
  }

  // modify the directory entry to reflect the new file size and cluster_bu
  const uint32_t parent_cluster = fi->entry_cluster;
  const uint32_t entry_index = fi->entry_index;
  const uint32_t parent_lba = cluster_to_sector(fsb, parent_cluster);

  result = device_read(fsb->device, parent_lba, fsb->sectors_per_cluster,
                       cluster_buf);

  if (result < 0) {
    kfree(cluster_buf);
    return -1;  // Read Failed
  }

  struct fat32_dir_entry* entries = (struct fat32_dir_entry*)cluster_buf;
  entries[entry_index].file_size = fi->file_size;
  entries[entry_index].cluster_high = (fi->first_cluster >> 16) & 0xFFFF;
  entries[entry_index].cluster_low = fi->first_cluster & 0xFFFF;

  result = device_write(fsb->device, parent_lba, fsb->sectors_per_cluster,
                        cluster_buf);

  if (result < 0) {
    kfree(cluster_buf);
    return -1;  // Write Failed
  }

  kfree(cluster_buf);
  return total_bytes_written;
}

struct inode* fat32_create(struct inode* dir, const char* name, uint32_t mode) {
  UNUSED(mode);

  // check if same name already exists in the parent directory
  struct fat32_dir_entry existing_entry;

  if (find_entry(dir, name, &existing_entry, NULL, NULL) == 0) {
    return NULL;  // Name already exists
  }

  struct fat32_dir_entry new_entry = {0};
  new_entry.attributes = FAT_ATTR_ARCHIVE;

  int result = create_entry(dir, name, &new_entry);

  if (result < 0) {
    return NULL;  // Failed to create file entry
  }

  return fat32_lookup(dir, name);
}

static void free_cluster_chain(struct fat32_sb_info* fsb,
                               uint32_t start_cluster) {
  uint32_t* fat_buffer = kmalloc(fsb->bytes_per_sector);

  if (!fat_buffer) {
    return;
  }

  uint32_t current = start_cluster;
  const uint32_t fat_lba = fsb->fat_start_lba;

  while (current < FAT32_EOC && current != 0) {
    // 128 entries per 512-byte sector (512 / 4 = 128)
    const uint32_t fat_sector = fat_lba + (current / 128);
    const uint32_t fat_offset = (current % 128);

    int result = device_read(fsb->device, fat_sector, 1, fat_buffer);

    if (result < 0) {
      kfree(fat_buffer);
      return;
    }

    current = fat_buffer[fat_offset] & 0x0FFFFFFF;
    fat_buffer[fat_offset] = 0x00000000;  // Mark as free

    result = device_write(fsb->device, fat_sector, 1, fat_buffer);

    if (result < 0) {
      kfree(fat_buffer);
      return;
    }
  }

  kfree(fat_buffer);
}

int fat32_unlink(struct inode* dir, const char* name) {
  if (!dir->private_data || !dir->sb || !dir->sb->private_data) {
    return -1;
  }

  struct fat32_sb_info* fsb = dir->sb->private_data;

  // Find the target file entry in the parent directory
  struct fat32_dir_entry target_entry;
  uint32_t target_cluster;
  uint32_t target_index;

  int result =
      find_entry(dir, name, &target_entry, &target_cluster, &target_index);

  if (result < 0) {
    return -1;  // File not found
  }

  if (target_entry.attributes & FAT_ATTR_DIRECTORY) {
    return -1;  // It's a directory, not a file!
  }

  uint32_t first_cluster =
      (target_entry.cluster_high << 16) | target_entry.cluster_low;

  // Free the cluster chain associated with the file
  free_cluster_chain(fsb, first_cluster);

  // Mark the entry as deleted in the Parent Directory
  // for efficiency, we can target_cluster and target_index also
  result = remove_entry(dir, name, &target_entry);

  if (result < 0) {
    return -1;  // Failed to remove entry
  }

  return 0;  // Success
}

int fat32_rename(struct inode* dir, const char* old_name,
                 const char* new_name) {
  if (!dir->private_data || !dir->sb || !dir->sb->private_data) {
    return -1;
  }

  // Find the target file entry in the parent directory
  struct fat32_dir_entry target_entry;
  uint32_t target_cluster;
  uint32_t target_index;

  int result =
      find_entry(dir, old_name, &target_entry, &target_cluster, &target_index);

  if (result < 0) {
    return -1;  // File not found
  }

  // Check if new name already exists
  struct fat32_dir_entry existing_entry;

  if (find_entry(dir, new_name, &existing_entry, NULL, NULL) == 0) {
    return -1;  // New name already exists
  }

  // create first new file entry with the new name, then delete the old one
  result = create_entry(dir, new_name, &target_entry);

  if (result < 0) {
    return -1;  // Failed to update entry name
  }

  result = remove_entry(dir, old_name, &target_entry);
  return result;  // Success
}

int fat32_move(struct inode* dir, struct inode* new_dir, const char* name) {
  if (!dir->private_data || !dir->sb || !dir->sb->private_data ||
      !new_dir->private_data || !new_dir->sb || !new_dir->sb->private_data) {
    return -1;
  }

  // Find the target file entry in the parent directory
  struct fat32_dir_entry target_entry;
  uint32_t target_cluster;
  uint32_t target_index;

  int result =
      find_entry(dir, name, &target_entry, &target_cluster, &target_index);

  if (result < 0) {
    return -1;  // File not found
  }

  // Check if the file already exists in the new directory
  if (find_entry(new_dir, name, NULL, NULL, NULL) == 0) {
    return -1;  // File already exists in the new directory
  }

  // Create a new entry in the new directory with the same attributes
  result = create_entry(new_dir, name, &target_entry);

  if (result < 0) {
    return -1;  // Failed to create entry in the new directory
  }

  // Remove the entry from the old directory
  result = remove_entry(dir, name, &target_entry);

  return result;  // Success
}
