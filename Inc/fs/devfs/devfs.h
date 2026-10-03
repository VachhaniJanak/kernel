#pragma once

#include <stddef.h>
#include <stdint.h>
#include <fs/fs.h>

enum devfs_device_type { DEVFS_DEVICE_CHAR = 0x01, DEVFS_DEVICE_BLOCK = 0x02 };

enum devfs_flags {
  DEVFS_FILE = 0x01,
  DEVFS_DIR = 0x02,
};

enum {
  DEVFS_READDIR_DOT = 0,
  DEVFS_READDIR_DOTDOT,
  DEVFS_READDIR_CHILDREN,
  DEVFS_READDIR_END,
};

#define DEVFS_MAGIC 0x52544A49
#define DEVFS_NAME_MAX 63

struct devfs_device;

struct device_operations {
  int (*read)(struct devfs_device *dev, void *buf, size_t len, size_t *off);
  int (*write)(struct devfs_device *dev, const void *buf, size_t len,
               size_t *off);
};

struct devfs_device {
  uint32_t device_id; // Unique identifier for the device
  enum devfs_device_type type;
  struct device_operations *fops;
  void *private_data; // driver-specific data
};

// devfs file info structure
typedef struct devfs_file_info {
  struct devfs_dentry *next_entry; // For readdir state
  int state;                       // State for readdir
} devfs_file_info_t;

// devfs inode structure
struct devfs_inode {
  struct inode *vfs;          // back-pointer to the VFS inode wrapper
  struct devfs_inode *parent; // parent directory (NULL for root)

  uint64_t inode_number;
  enum devfs_flags flags;

  struct devfs_dentry *children; // Directory entries (for dirs)
  uint32_t child_count;          // Number of children (for dirs)

  struct devfs_device *device;
};

// Directory entry
struct devfs_dentry {
  char name[DEVFS_NAME_MAX + 1];
  struct devfs_inode *inode;
  struct devfs_dentry *next; // Next entry in directory
};

// Superblock private data
struct devfs_sb_info {
  struct devfs_inode *root_inode;
  size_t next_inode_num;
  size_t total_files;
};

struct mount_ops *devfs_get_mount_ops(void);

struct superblock *devfs_create_superblock(void);

int devfs_mkdir(struct inode *dir, const char *name, uint32_t mode);

int devfs_rmdir(struct inode *dir, const char *name);

int devfs_register(struct inode *dir, const char *name, uint32_t type,
                   uint32_t device_id, struct device_operations *fops,
                   void *driver_data);

void *devfs_unregister(struct inode *dir, const char *name);
