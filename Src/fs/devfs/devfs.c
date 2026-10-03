#include <fs/devfs/devfs.h>
#include <fs/fs.h>
#include <libs/string.h>
#include <mm/vmm/kheap.h>
#include <platform/attributes.h>
#include <process/locks.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct superblock *devfs_mount(struct inode *target);
int devfs_destroy_superblock(struct superblock *sb);
int devfs_open_dir(struct file *file);
int devfs_close_dir(struct file *file);
struct inode *devfs_lookup(struct inode *dir, const char *name);
int devfs_readdir(struct file *file, void *ctx,
                  int (*fill)(void *, const char *, uint32_t, uint32_t));
int devfs_stat(struct file *file, struct fs_stat *stat);
int devfs_write(struct file *file, const void *buf, size_t len, size_t *off);
int devfs_read(struct file *file, void *buf, size_t len, size_t *off);
int devfs_open(struct file *file);
int devfs_close(struct file *file);
void devfs_destroy(struct inode *inode);

static struct mount_ops devfs_mount_ops = {
    .target_mount = devfs_mount,
    .target_unmount = devfs_destroy_superblock,
};

// File operations for regular files
static struct file_operations devfs_file_ops = {
    .open = devfs_open,
    .read = devfs_read,
    .write = devfs_write,
    .close = devfs_close,
    .stat = devfs_stat,
    .create = NULL,
    .unlink = NULL,
    .readdir = NULL, // Not a directory
    .destroy = devfs_destroy,
};

// File operations for directories
static struct file_operations devfs_dir_ops = {
    .open = devfs_open_dir,
    .read = NULL,  // Can't read directories
    .write = NULL, // Can't write directories
    .close = devfs_close_dir,
    .stat = devfs_stat,
    .readdir = devfs_readdir,
    .lookup = devfs_lookup,
    .mkdir = NULL,
    .rmdir = NULL,
    .create = NULL,
    .unlink = NULL,
    .destroy = devfs_destroy,
};

struct mount_ops *devfs_get_mount_ops(void) { return &devfs_mount_ops; }

struct superblock *devfs_mount(struct inode *target) {
  (void)target; // Suppress unused parameter warning
  return devfs_create_superblock();
}

// Superblock operations
int devfs_write_super(struct superblock *sb) {
  // devfs is in-memory, nothing to write to disk
  UNUSED(sb);
  return 0;
}

int devfs_sync_fs(struct superblock *sb) {
  // devfs is in-memory, nothing to sync
  UNUSED(sb);
  return 0;
}

// Create a new devfs inode
struct inode *devfs_create_inode(struct superblock *sb, uint32_t type) {
  if (!sb) {
    return NULL;
  }

  struct inode *inode = kmalloc(sizeof(struct inode));

  if (!inode) {
    return NULL;
  }

  memset(inode, 0, sizeof(struct inode));

  // Allocate devfs private inode
  struct devfs_inode *di = kmalloc(sizeof(struct devfs_inode));

  if (!di) {
    kfree(inode);
    return NULL;
  }

  memset(di, 0, sizeof(struct devfs_inode));

  struct devfs_sb_info *dsi = (struct devfs_sb_info *)sb->private_data;
  const size_t inode_number = ++(dsi->next_inode_num);

  di->vfs = inode;
  di->parent = NULL;
  di->inode_number = inode_number;
  di->device = NULL;
  di->flags = type;
  di->children = NULL;

  // Setup generic inode
  inode->inode_num = inode_number;
  inode->sb = sb;
  inode->private_data = di;
  inode->refcount = 0;

  mutex_init(&inode->lock);

  // Set appropriate operations
  if (type == DEVFS_DIR) {
    inode->fops = &devfs_dir_ops;
  } else {
    inode->fops = &devfs_file_ops;
  }

  return inode;
}

void devfs_destroy(struct inode *inode) {
  if (!inode) {
    return;
  }

  struct devfs_inode *dfi = inode->private_data;

  if (dfi) {
    kfree(dfi);
  }

  kfree(inode);
}

struct superblock *devfs_create_superblock(void) {
  struct superblock *sb = kmalloc(sizeof(struct superblock));

  if (!sb) {
    return NULL;
  }

  memset(sb, 0, sizeof(struct superblock));

  // Allocate devfs superblock info
  struct devfs_sb_info *dsi = kmalloc(sizeof(struct devfs_sb_info));

  if (!dsi) {
    kfree(sb);
    return NULL;
  }

  memset(dsi, 0, sizeof(struct devfs_sb_info));

  // Fill superblock fields
  sb->magic = DEVFS_MAGIC;
  sb->private_data = dsi;

  // Create root inode
  dsi->next_inode_num = 0; // Start inode numbering from 1

  struct inode *root_inode = devfs_create_inode(sb, DEVFS_DIR);

  if (!root_inode) {
    kfree(dsi);
    kfree(sb);
    return NULL;
  }

  // Store root in superblock
  sb->root = root_inode;

  dsi->root_inode = (struct devfs_inode *)root_inode->private_data;
  dsi->total_files = 1;

  root_inode->mode = VFS_IFDIR | 0755;
  return sb;
}

struct devfs_dentry *devfs_find_dentry(struct devfs_inode *dir,
                                       const char *name) {
  if (!dir || dir->flags != DEVFS_DIR) {
    return NULL;
  }

  // check name length
  if (strlen(name) > DEVFS_NAME_MAX) {
    return NULL;
  }

  struct devfs_dentry *entry = dir->children;

  while (entry) {
    if (strcmp(entry->name, name) == 0) {
      return entry;
    }

    entry = entry->next;
  }

  return NULL;
}

int devfs_add_dentry(struct devfs_inode *dir, const char *name,
                     struct devfs_inode *inode) {
  if (!dir || dir->flags != DEVFS_DIR) {
    return -1;
  }

  // check name length
  if (strlen(name) > DEVFS_NAME_MAX) {
    return -1;
  }

  // Check if entry already exists
  if (devfs_find_dentry(dir, name)) {
    return -1;
  }

  // Create new dentry
  struct devfs_dentry *entry = kmalloc(sizeof(struct devfs_dentry));

  if (!entry) {
    return -1;
  }

  strcpy(entry->name, name);
  entry->inode = inode;
  entry->next = dir->children;

  inode->parent = dir;

  dir->children = entry;
  dir->child_count++;

  return 0;
}

void devfs_free_inode(struct devfs_inode *di) {
  if (!di) {
    return;
  }

  if (di->vfs) {
    kfree(di->vfs);
    di->vfs = NULL;
  }

  kfree(di);
}

void devfs_free_recursive_inode(struct devfs_inode *di) {
  struct devfs_inode *root_di = di;

  /* --- 1. Iteratively strip leaves until only the root remains. --- */
  if (!root_di) {
    return;
  }

  struct devfs_inode *cur = root_di;

  while (cur) {
    if (cur->children) {
      /* Descend into first child. */
      cur = cur->children->inode;
      continue;
    }

    /* cur is a leaf (childless). */
    if (cur == root_di) {
      /* Root is childless — nothing left to free below it. */
      break;
    }

    /* Remember where to go after freeing. */
    struct devfs_inode *parent = cur->parent;

    /* Detach cur from parent's entry list. */
    if (parent) {
      struct devfs_dentry **pp = &parent->children;

      while (*pp && (*pp)->inode != cur) {
        pp = &(*pp)->next;
      }

      if (*pp) {
        struct devfs_dentry *dead = *pp;
        *pp = dead->next;
        kfree(dead);
      }
    }

    /* Free cur's contents and cur itself. */
    devfs_free_inode(cur);

    /* Pop back up. Parent may now be a leaf. */
    cur = parent;
  }
}

int devfs_destroy_superblock(struct superblock *sb) {
  if (!sb) {
    return -1;
  }

  /* --- 1. Recover our root inode and devfs payload. --- */
  struct inode *root = sb->root;

  if (!root) {
    kfree(sb->private_data);
    kfree(sb);
    return -1;
  }

  struct devfs_inode *root_di = (struct devfs_inode *)root->private_data;

  /* --- 2. Recursively free all inodes and their data. --- */
  devfs_free_recursive_inode(root_di);

  /* --- 3. Free the root inode last. --- */
  if (root_di) {
    devfs_free_inode(root_di);
  }

  /* --- 4. Free superblock and its payload. --- */
  kfree(sb->private_data);
  kfree(sb);
  return 0;
}

int devfs_remove_entry(struct devfs_inode *dir, const char *name) {
  if (!dir || dir->flags != DEVFS_DIR) {
    return -1;
  }

  // check name length
  if (strlen(name) > DEVFS_NAME_MAX) {
    return -1;
  }

  struct devfs_dentry **pp = &dir->children;

  while (*pp) {
    if (strcmp((*pp)->name, name) == 0) {
      struct devfs_dentry *dead = *pp;
      *pp = dead->next;
      kfree(dead);
      return 0;
    }

    pp = &(*pp)->next;
  }
  return -1;
}

static inline uint32_t devfs_dirent_type(uint32_t devfs_type) {
  switch (devfs_type) {
  case DEVFS_DIR:
    return VFS_DT_DIR;

  case DEVFS_FILE:
    return VFS_DT_REG;

  default:
    return VFS_DT_UNKNOWN;
  }
}

struct inode *devfs_lookup(struct inode *dir, const char *name) {
  if (!dir || !name || name[0] == '\0') {
    return NULL;
  }

  struct devfs_inode *di = (struct devfs_inode *)dir->private_data;

  if (!di || di->flags != DEVFS_DIR) {
    return NULL;
  }

  if (strcmp(name, ".") == 0) {
    return dir;
  }

  if (strcmp(name, "..") == 0) {
    struct devfs_inode *parent = di->parent ? di->parent : di;

    if (!parent->vfs) {
      return NULL;
    }

    return parent->vfs;
  }

  struct devfs_dentry *entry = devfs_find_dentry(di, name);

  if (!entry || !entry->inode || !entry->inode->vfs) {
    return NULL;
  }

  struct inode *child = entry->inode->vfs;
  return child;
}

int devfs_open_dir(struct file *file) {
  if (!file || !file->inode) {
    return -1;
  }

  struct devfs_inode *di = (struct devfs_inode *)file->inode->private_data;

  if (!di) {
    return -1;
  }

  file->f_pos = 0;

  if (di->flags == DEVFS_DIR) {
    struct devfs_file_info *info = kmalloc(sizeof(struct devfs_file_info));

    if (!info) {
      return -1;
    }

    info->next_entry = di->children;
    info->state = DEVFS_READDIR_DOT;

    file->private_data = info;
  }

  return 0;
}

int devfs_close_dir(struct file *file) {
  if (file->private_data) {
    kfree(file->private_data);
    file->private_data = NULL;
    return 0;
  }

  return -1;
}

int devfs_readdir(struct file *file, void *ctx,
                  int (*fill)(void *, const char *, uint32_t, uint32_t)) {
  if (!file || !file->inode || !fill) {
    return -1;
  }

  struct devfs_inode *di = (struct devfs_inode *)file->inode->private_data;

  if (!di || di->flags != DEVFS_DIR) {
    return -1;
  }

  struct devfs_file_info *info = file->private_data;

  if (!info) {
    return -1;
  }

  while (info->state != DEVFS_READDIR_END) {
    if (info->state == DEVFS_READDIR_DOT) {
      if (fill(ctx, ".", file->inode->inode_num, VFS_DT_DIR) != 0) {
        return 0;
      }

      info->state = DEVFS_READDIR_DOTDOT;
      continue;
    }

    if (info->state == DEVFS_READDIR_DOTDOT) {
      struct devfs_inode *parent = di->parent ? di->parent : di;

      if (!parent->vfs) {
        return -1;
      }

      if (fill(ctx, "..", parent->vfs->inode_num, VFS_DT_DIR) != 0) {
        return 0;
      }

      info->state = DEVFS_READDIR_CHILDREN;
      continue;
    }

    if (info->state == DEVFS_READDIR_CHILDREN) {
      struct devfs_dentry *entry = info->next_entry;

      if (!entry) {
        info->state = DEVFS_READDIR_END;
        continue;
      }

      info->next_entry = entry->next;

      if (!entry->inode || !entry->inode->vfs) {
        continue;
      }

      uint32_t type = devfs_dirent_type(entry->inode->flags);

      if (fill(ctx, entry->name, entry->inode->vfs->inode_num, type) != 0) {
        return 0;
      }
    }
  }

  return 0;
}

int devfs_mkdir(struct inode *dir, const char *name, uint32_t mode) {
  if (!dir || !name) {
    return -1;
  }

  struct devfs_inode *dir_di = (struct devfs_inode *)dir->private_data;

  if (!dir_di || dir_di->flags != DEVFS_DIR) {
    return -1;
  }

  if (devfs_find_dentry(dir_di, name)) {
    return -1;
  }

  struct inode *child = devfs_create_inode(dir->sb, DEVFS_DIR);

  if (!child) {
    return -1;
  }

  struct devfs_inode *child_di = (struct devfs_inode *)child->private_data;

  if (devfs_add_dentry(dir_di, name, child_di) < 0) {
    devfs_free_inode(child_di);
    return -1;
  }

  child->mode = mode;
  return 0;
}

int devfs_rmdir(struct inode *dir, const char *name) {
  if (!dir || !name) {
    return -1;
  }

  struct devfs_inode *dir_di = (struct devfs_inode *)dir->private_data;

  if (!dir_di || dir_di->flags != DEVFS_DIR) {
    return -1;
  }

  struct devfs_dentry *e = devfs_find_dentry(dir_di, name);

  if (!e) {
    return -1;
  }

  struct devfs_inode *child_di = e->inode;

  if (child_di->flags != DEVFS_DIR) {
    return -1;
  }

  /* Recursively free all inodes and their data */
  devfs_free_recursive_inode(child_di);

  /* Remove the entry from the parent. */
  int rc = devfs_remove_entry(dir_di, name);

  if (rc < 0) {
    return rc;
  }

  devfs_free_inode(child_di);
  return 0;
}

int devfs_stat(struct file *file, struct fs_stat *stat) {
  if (!file || !file->inode || !stat) {
    return -1;
  }

  memset(stat, 0, sizeof(struct fs_stat));

  stat->dev = 0;
  stat->ino = file->inode->inode_num;
  stat->mode = file->inode->mode;
  stat->uid = file->inode->uid;
  stat->gid = file->inode->gid;
  return 0;
}

int devfs_open(struct file *file) {
  UNUSED(file);
  return 0;
}

int devfs_close(struct file *file) {
  UNUSED(file);
  return 0;
}

int devfs_read(struct file *file, void *buf, size_t len, size_t *off) {
  if (!file || !file->inode || !buf || len != 0 || !off) {
    return -1;
  }

  struct devfs_inode *di = file->inode->private_data;

  if (!di || !di->device || !di->device->fops || !di->device->fops->read) {
    return -1;
  }

  return di->device->fops->read(di->device, buf, len, off);
}

int devfs_write(struct file *file, const void *buf, size_t len, size_t *off) {
  if (!file || !file->inode || !off) {
    return -1;
  }

  if (!buf && len != 0) {
    return -1;
  }

  struct devfs_inode *di = file->inode->private_data;

  if (!di || !di->device || !di->device->fops || !di->device->fops->write) {
    return -1;
  }

  return di->device->fops->write(di->device, buf, len, off);
}

static struct inode *devfs_create(struct inode *dir, struct devfs_device *dev,
                                  const char *name, uint32_t mode) {
  if (!dir || !VFS_ISDIR(dir->mode)) {
    return NULL;
  }

  // check name length
  if (strlen(name) > DEVFS_NAME_MAX) {
    return NULL;
  }

  // Check if file already exists
  struct devfs_inode *dir_di = (struct devfs_inode *)dir->private_data;

  if (devfs_find_dentry(dir_di, name)) {
    return NULL;
  }

  // Create new inode
  struct inode *new_inode = devfs_create_inode(dir->sb, DEVFS_FILE);

  if (!new_inode) {
    return NULL;
  }

  // Add to directory
  struct devfs_inode *new_di = (struct devfs_inode *)new_inode->private_data;

  if (devfs_add_dentry(dir_di, name, new_di) < 0) {
    devfs_free_inode(new_di);
    return NULL;
  }

  // Update directory size
  dir->size += sizeof(struct devfs_dentry);

  new_inode->mode = mode;
  new_di->device = dev;
  return new_inode;
}

int devfs_register(struct inode *dir, const char *name, uint32_t type,
                   uint32_t device_id, struct device_operations *fops,
                   void *private_data) {
  if (!dir || !name || !fops) {
    return -1;
  }

  struct devfs_device *dev = kmalloc(sizeof(struct devfs_device));

  if (!dev) {
    return -1;
  }

  memset(dev, 0, sizeof(*dev));

  dev->type = type;
  dev->device_id = device_id;
  dev->fops = fops;
  dev->private_data = private_data;

  if (!devfs_create(dir, dev, name, VFS_IFREG)) {
    kfree(dev);
    return -1;
  }

  return 0;
}

void *devfs_unregister(struct inode *dir, const char *name) {
  if (!dir || !name) {
    return NULL;
  }

  struct devfs_inode *dir_di = (struct devfs_inode *)dir->private_data;

  if (!dir_di || dir_di->flags != DEVFS_DIR) {
    return NULL;
  }

  struct devfs_dentry *e = devfs_find_dentry(dir_di, name);

  if (!e) {
    return NULL;
  }

  struct devfs_inode *child_di = e->inode;

  if (child_di->flags == DEVFS_DIR) {
    return NULL;
  }

  /* Remove the directory entry. */
  int rc = devfs_remove_entry(dir_di, name);

  if (rc < 0) {
    return NULL;
  }

  /* free devfs_device */
  if (!child_di->device) {
    devfs_free_inode(child_di);
    return NULL;
  }

  void *private_data = child_di->device->private_data;

  kfree(child_di->device);
  devfs_free_inode(child_di);
  return private_data;
}
