#pragma once

#include <process/locks.h>
#include <stddef.h>
#include <stdint.h>

#define off_t int64_t

// File types
#define VFS_IFMT 0170000
#define VFS_IFREG 0100000  // regular file
#define VFS_IFDIR 0040000  // directory
#define VFS_IFLNK 0120000  // symlink
#define VFS_IFCHR 0020000  // char device
#define VFS_IFBLK 0060000  // block device

#define VFS_ISREG(m) (((m) & VFS_IFMT) == VFS_IFREG)
#define VFS_ISDIR(m) (((m) & VFS_IFMT) == VFS_IFDIR)

// Open flags
#define VFS_O_RDONLY 0x0000
#define VFS_O_WRONLY 0x0001
#define VFS_O_RDWR 0x0002
#define VFS_O_CREAT 0x0040
#define VFS_O_EXCL 0x0080
#define VFS_O_TRUNC 0x0200
#define VFS_O_APPEND 0x0400
#define VFS_O_NONBLOCK 0x0800
#define VFS_O_DSYNC 0x1000
#define VFS_O_SYNC 0x101000
#define VFS_O_RSYNC 0x101000
#define VFS_O_DIRECTORY 0x200000
#define VFS_O_ACCMODE 0x0003

// Seek whence
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

#define VFS_DT_UNKNOWN 0
#define VFS_DT_REG 8
#define VFS_DT_DIR 4
#define VFS_DT_LNK 10

#define VFS_NAME_MAX 25

#define DCACHE_MAX_ENTRIES 10000

#define MASK_DENTRY_DIRTY (1 << 0)
#define MASK_DENTRY_INLRU (1 << 1)

#define SET_BIT(flags, bit) ((flags) |= (bit))
#define CLEAR_BIT(flags, bit) ((flags) &= ~(bit))
#define CHECK_BIT(flags, bit) ((flags) & (bit))

enum filesystem_type {
  FS_TYPE_UNKNOWN,
  FS_TYPE_TMPFS,
  FS_TYPE_DEVFS,
  FS_TYPE_FAT16,
  FS_TYPE_FAT32,
};

struct file;
struct inode;
struct dentry;

struct fs_stat {
  uint32_t dev;
  uint32_t ino;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
  uint32_t blksize;
  uint32_t blocks;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
};

struct file_operations {
  int (*open)(struct file* file);
  int (*close)(struct file* file);
  int (*read)(struct file* file, void* buf, size_t len, size_t* off);
  int (*write)(struct file* file, const void* buf, size_t len, size_t* off);
  int (*unlink)(struct inode* dir, const char* name);
  struct inode* (*create)(struct inode* dir, const char* name, uint32_t mode);
  int (*stat)(struct file* file, struct fs_stat* stat);

  struct inode* (*lookup)(struct inode* dir, const char* name);
  int (*mkdir)(struct inode* dir, const char* name, uint32_t mode);
  int (*rmdir)(struct inode* dir, const char* name);
  int (*readdir)(struct file* file, void* ctx,
                 int (*fill)(void* ctx, const char* name, uint32_t ino,
                             uint32_t type));
  void (*destroy)(struct inode* inode);
};

// Inode - represents a file/directory on disk
struct inode {
  uint32_t inode_num;            // Unique identifier
  uint16_t mode;                 // File type and permissions
  uint32_t size;                 // File size
  uint32_t blocks;               // Number of blocks
  uint32_t uid, gid;             // Owner info
  uint32_t atime, mtime, ctime;  // Timestamps
  void* private_data;            // Filesystem-specific data
  struct superblock* sb;         // Back pointer to superblock
  struct file_operations* fops;  // File operations for this inode
  uint32_t refcount;             // Reference count for open files
  struct mount* mounted;         // NULL if not a mount point
  mutex_t lock;                  // Mutex for thread-safe operations
};

// File - represents an open file instance
struct file {
  struct dentry* dentry;
  struct inode* inode;
  off_t f_pos;          // Current file position
  uint32_t f_flags;     // Open flags
  uint32_t f_refcount;  // Reference count
  void* private_data;
  struct file* prev;
  struct file* next;
};

// Directory entry
struct dentry {
  char name[VFS_NAME_MAX + 1];
  struct inode* inode;
  struct dentry* parent;
  struct dentry* child;
  struct dentry* sibling_prev;
  struct dentry* sibling_next;

  // LRU and Cache Management
  uint32_t d_refcount;  // Number of open files using this exact path
  uint32_t d_flags;     // Flags for cache management
  struct dentry* lru_prev;
  struct dentry* lru_next;
};

// Superblock - represents a mounted filesystem
struct superblock {
  uint32_t magic;  // Filesystem magic number
  uint32_t block_size;
  uint32_t total_blocks;
  uint32_t free_blocks;
  uint32_t total_inodes;
  uint32_t free_inodes;
  void* private_data;            // Filesystem-specific data
  struct file_operations* sops;  // Superblock operations
  struct inode* root;            // Root directory entry
};

struct mount_ops {
  struct superblock* (*target_mount)(struct inode* target);
  int (*target_unmount)(struct superblock* sb);
  enum filesystem_type fs_type;
};

// Mount entry
struct mount {
  struct dentry* mount_point;  // Where it's mounted (e.g., "/mnt/usb")
  struct superblock* sb;       // The filesystem superblock
  uint32_t flags;
  struct mount_ops* ops;  // target-specific mount/unmount operations
  struct mount* next;
};

// Mount table - simple global list
struct mount_table {
  struct mount* head;
  struct mount* root;  // Root filesystem mount
  uint32_t count;
};

struct inode* fs_get_dev_dir_inode(void);

int fs_init(void);

struct inode* fs_resolve_path(const char* path, struct dentry** out_dentry);

struct inode* fs_resolve_parent(const char* path, char** final_name,
                                struct dentry** out_parent_dentry);

struct file* fs_open(const char* path, uint32_t flags);

size_t fs_read(struct file* f, void* buf, size_t len);

size_t fs_write(struct file* f, const void* buf, size_t len);

int fs_close(struct file* f);

off_t fs_lseek(struct file* f, off_t off, int whence);

int fs_stat(const char* path, struct fs_stat* st);

int fs_unlink(const char* path);

int fs_mkdir(const char* path, uint32_t mode);

int fs_rmdir(const char* path);

struct file* fs_open_dir(const char* path);

int fs_close_dir(struct file* file);

int fs_readdir(struct file* file,
               int (*fill)(void*, const char*, uint32_t, uint32_t), void* ctx);

void mount_table_init(void);

int mount_root(struct superblock* sb);

int mount_filesystem(const char* mount_point, struct superblock* sb);

int mount_device(const char* device, const char* mount_point,
                 enum filesystem_type fs_type);

int unmount_device(const char* mount_point);

void debug_dump_dcache(void);

void debug_dump_dcache_lru(void);

void debug_dump_mount_table(void);

void debug_dump_open_files(void);
