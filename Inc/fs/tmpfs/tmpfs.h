#pragma once

#include <fs/fs.h>
#include <stddef.h>
#include <stdint.h>

#define TMPFS_MAGIC 0x54504D46  // "TMPF"
#define TMPFS_BLOCK_SIZE 4096
#define TMPFS_MAX_BLOCKS ((1 << 20) * 2)  // 2MB maximum size for tmpfs
#define TMPFS_MAX_FILES 100000
#define TMPFS_NAME_MAX 255

enum {
  TMPFS_READDIR_DOT = 0,
  TMPFS_READDIR_DOTDOT,
  TMPFS_READDIR_CHILDREN,
  TMPFS_READDIR_END,
};

// tmpfs-specific inode types
typedef enum tmpfs_inode_type {
  TMPFS_FILE = 0x01,
  TMPFS_DIR = 0x02,
  TMPFS_SYMLINK = 0x04
} tmpfs_inode_type_t;

// tmpfs file info structure
typedef struct tmpfs_file_info {
  struct tmpfs_dentry* next_entry;  // For readdir state
  int state;                        // State for readdir
} tmpfs_file_info_t;

// tmpfs inode structure
struct tmpfs_inode {
  struct inode* vfs;              // back-pointer to the VFS inode wrapper
  struct tmpfs_inode* parent;     // parent directory (NULL for root)
  uint64_t inode_number;          // Unique identifier for the inode
  uint32_t type;                  // File type (FILE, DIR, etc.)
  uint32_t size;                  // File size
  char* data;                     // File data (for files)
  uint32_t data_size;             // Allocated data size
  struct tmpfs_dentry* children;  // Directory entries (for dirs)
  uint32_t child_count;           // Number of children (for dirs)
  uint32_t nlink;                 // Number of hard links
};

// Directory entry
struct tmpfs_dentry {
  char name[TMPFS_NAME_MAX + 1];  // File/dir name
  struct tmpfs_inode* inode;      // Pointer to inode
  struct tmpfs_dentry* next;      // Next entry in directory
};

// Superblock private data
struct tmpfs_sb_info {
  struct tmpfs_inode* root_inode;
  size_t next_inode_num;
  size_t total_files;
};

// Function prototypes
struct superblock* tmpfs_create_superblock(void);

struct mount_ops* tmpfs_get_mount_ops(void);
