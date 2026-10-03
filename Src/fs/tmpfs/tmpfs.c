#include <fs/fs.h>
#include <fs/tmpfs/tmpfs.h>
#include <mm/vmm/kheap.h>
#include <platform/attributes.h>
#include <libs/string.h>
#include <process/locks.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct superblock *tmpfs_mount(struct inode *target);
int tmpfs_destroy_superblock(struct superblock *sb);
int tmpfs_open_dir(struct file *file);
int tmpfs_close_dir(struct file *file);
struct inode *tmpfs_lookup(struct inode *dir, const char *name);
int tmpfs_readdir(struct file *file, void *ctx,
                  int (*fill)(void *, const char *, uint32_t, uint32_t));
int tmpfs_rmdir(struct inode *dir, const char *name);
int tmpfs_mkdir(struct inode *dir, const char *name, uint32_t mode);
int tmpfs_open(struct file *file);
int tmpfs_read(struct file *file, void *buf, size_t len, size_t *off);
int tmpfs_write(struct file *file, const void *buf, size_t len, size_t *off);
int tmpfs_close(struct file *file);
int tmpfs_stat(struct file *file, struct fs_stat *stat);
int tmpfs_unlink(struct inode *dir, const char *name);
struct inode *tmpfs_create(struct inode *dir, const char *name, uint32_t mode);
void tmpfs_destroy(struct inode *inode);

static struct mount_ops tmpfs_mount_ops = {
    .target_mount = tmpfs_mount,
    .target_unmount = tmpfs_destroy_superblock,
};

// File operations for regular files
static struct file_operations tmpfs_file_ops = {
    .open = tmpfs_open,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .close = tmpfs_close,
    .stat = tmpfs_stat,
    .create = NULL,
    .unlink = NULL,
    .readdir = NULL, // Not a directory
    .destroy = tmpfs_destroy,
};

// File operations for directories
static struct file_operations tmpfs_dir_ops = {
    .open = tmpfs_open_dir,
    .read = NULL,  // Can't read directories
    .write = NULL, // Can't write directories
    .close = tmpfs_close_dir,
    .stat = tmpfs_stat,
    .readdir = tmpfs_readdir,
    .lookup = tmpfs_lookup,
    .mkdir = tmpfs_mkdir,
    .rmdir = tmpfs_rmdir,
    .create = tmpfs_create, // Allow creating files in directories
    .unlink = tmpfs_unlink, // Allow unlinking files in directories
    .destroy = tmpfs_destroy,
};

struct mount_ops *tmpfs_get_mount_ops(void) { return &tmpfs_mount_ops; }

struct superblock *tmpfs_mount(struct inode *target){
  UNUSED(target);
  return tmpfs_create_superblock();
}

// Create a new tmpfs inode
static struct inode *tmpfs_create_inode(struct superblock *sb, uint32_t type) {
  if (!sb) {
    return NULL;
  }

  struct inode *inode = kmalloc(sizeof(struct inode));

  if (!inode) {
    return NULL;
  }

  memset(inode, 0, sizeof(struct inode));

  // Allocate tmpfs private inode
  struct tmpfs_inode *ti = kmalloc(sizeof(struct tmpfs_inode));

  if (!ti) {
    kfree(inode);
    return NULL;
  }

  memset(ti, 0, sizeof(struct tmpfs_inode));

  struct tmpfs_sb_info *tsi = (struct tmpfs_sb_info *)sb->private_data;
  const size_t inode_number = ++(tsi->next_inode_num);

  ti->vfs = inode;
  ti->parent = NULL;
  ti->inode_number = inode_number;
  ti->type = type;
  ti->children = NULL;
  ti->nlink = 1;

  // Setup generic inode
  inode->inode_num = inode_number;
  inode->mode = 0;
  inode->size = 0;
  inode->blocks = 0;
  inode->uid = 0;
  inode->gid = 0;
  inode->sb = sb;
  inode->private_data = ti;
  inode->refcount = 0;

  mutex_init(&inode->lock);

  // Set appropriate operations
  if (type == TMPFS_DIR) {
    inode->fops = &tmpfs_dir_ops;
  } else {
    inode->fops = &tmpfs_file_ops;
  }

  return inode;
}

void tmpfs_destroy(struct inode *inode) {
  (void)inode; // Unused parameter
}


// Create superblock
struct superblock *tmpfs_create_superblock(void) {
  struct superblock *sb = kmalloc(sizeof(struct superblock));

  if (!sb) {
    return NULL;
  }

  memset(sb, 0, sizeof(struct superblock));

  // Allocate tmpfs superblock info
  struct tmpfs_sb_info *tsi = kmalloc(sizeof(struct tmpfs_sb_info));

  if (!tsi) {
    kfree(sb);
    return NULL;
  }

  memset(tsi, 0, sizeof(struct tmpfs_sb_info));

  // Fill superblock fields
  sb->magic = TMPFS_MAGIC;
  sb->block_size = TMPFS_BLOCK_SIZE;
  sb->total_blocks = TMPFS_MAX_BLOCKS;
  sb->free_blocks = sb->total_blocks;
  sb->total_inodes = TMPFS_MAX_FILES;
  sb->free_inodes = sb->total_inodes;
  sb->private_data = tsi;
  // sb->sops = &tmpfs_sb_ops;

  // Create root inode
  tsi->next_inode_num = 0; // Start inode numbering from 1
  struct inode *root_inode = tmpfs_create_inode(sb, TMPFS_DIR);

  if (!root_inode) {
    kfree(tsi);
    kfree(sb);
    return NULL;
  }

  // Store root in superblock
  sb->root = root_inode;

  tsi->root_inode = (struct tmpfs_inode *)root_inode->private_data;
  tsi->total_files = 1;

  root_inode->mode = VFS_IFDIR | 0755; // Directory with rwxr-xr-x permissions
  return sb;
}

void tmpfs_free_inode(struct tmpfs_inode *ti) {
  if (!ti) {
    return;
  }

  /* Files own their data buffer. */
  if (ti->data) {
    kfree(ti->data);
    ti->data = NULL;
  }

  /* Free the VFS inode wrapper too. The private_data pointer and
   * the wrapper must both go. */
  if (ti->vfs) {
    kfree(ti->vfs);
    ti->vfs = NULL;
  }

  kfree(ti);
}

void tmpfs_free_rec_inode(struct tmpfs_inode *ti) {
  struct tmpfs_inode *root_ti = ti;

  /* --- 1. Iteratively strip leaves until only the root remains. --- */
  if (!root_ti) {
    return;
  }

  struct tmpfs_inode *cur = root_ti;

  while (cur) {
    if (cur->children) {
      /* Descend into first child. */
      cur = cur->children->inode;
      continue;
    }

    /* cur is a leaf (childless). */
    if (cur == root_ti) {
      /* Root is childless — nothing left to free below it. */
      break;
    }

    /* Remember where to go after freeing. */
    struct tmpfs_inode *parent = cur->parent;

    /* Detach cur from parent's entry list. */
    if (parent) {
      struct tmpfs_dentry **pp = &parent->children;

      while (*pp && (*pp)->inode != cur) {
        pp = &(*pp)->next;
      }

      if (*pp) {
        struct tmpfs_dentry *dead = *pp;
        *pp = dead->next;
        kfree(dead);
      }
    }

    /* Free cur's contents and cur itself. */
    tmpfs_free_inode(cur);

    /* Pop back up. Parent may now be a leaf. */
    cur = parent;
  }
}

int tmpfs_destroy_superblock(struct superblock *sb) {
  if (!sb) {
    return -1;
  }

  /* --- 1. Recover our root inode and tmpfs payload. --- */
  struct inode *root = sb->root;

  if (!root) {
    kfree(sb->private_data);
    kfree(sb);
    return -1;
  }

  struct tmpfs_inode *root_ti = (struct tmpfs_inode *)root->private_data;

  /* --- 2. Recursively free all inodes and their data. --- */
  tmpfs_free_rec_inode(root_ti);

  /* --- 3. Free the root inode last. --- */
  if (root_ti) {
    tmpfs_free_inode(root_ti);
  }

  /* --- 4. Free superblock and its payload. --- */
  kfree(sb->private_data);
  kfree(sb);
  return 0;
}

// Find a directory entry by name
struct tmpfs_dentry *tmpfs_find_dentry(struct tmpfs_inode *dir,
                                       const char *name) {
  if (!dir || dir->type != TMPFS_DIR) {
    return NULL;
  }

  // check name length
  if (strlen(name) > TMPFS_NAME_MAX) {
    return NULL;
  }

  struct tmpfs_dentry *entry = dir->children;

  while (entry) {
    if (strcmp(entry->name, name) == 0) {
      return entry;
    }

    entry = entry->next;
  }

  return NULL;
}

// Add a directory entry
int tmpfs_add_dentry(struct tmpfs_inode *dir, const char *name,
                     struct tmpfs_inode *inode) {
  if (!dir || dir->type != TMPFS_DIR) {
    return -1;
  }

  // Check if entry already exists
  if (tmpfs_find_dentry(dir, name)) {
    return -1;
  }

  // check name length
  if (strlen(name) > TMPFS_NAME_MAX) {
    return -1;
  }

  // Create new dentry
  struct tmpfs_dentry *entry = kmalloc(sizeof(struct tmpfs_dentry));

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

int tmpfs_remove_entry(struct tmpfs_inode *dir, const char *name) {
  if (!dir || dir->type != TMPFS_DIR) {
    return -1;
  }

  // check name length
  if (strlen(name) > TMPFS_NAME_MAX) {
    return -1;
  }

  struct tmpfs_dentry **pp = &dir->children;

  while (*pp) {
    if (strcmp((*pp)->name, name) == 0) {
      struct tmpfs_dentry *dead = *pp;
      *pp = dead->next;
      kfree(dead);
      return 0;
    }

    pp = &(*pp)->next;
  }
  return -1;
}

// Superblock operations
int tmpfs_write_super(struct superblock *sb) {
  // tmpfs is in-memory, nothing to write to disk
  UNUSED(sb);
  return 0;
}

int tmpfs_sync_fs(struct superblock *sb) {
  // tmpfs is in-memory, nothing to sync
  UNUSED(sb);
  return 0;
}

static inline uint32_t tmpfs_dirent_type(uint32_t tmpfs_type) {
  switch (tmpfs_type) {
  case TMPFS_DIR:
    return VFS_DT_DIR;

  case TMPFS_FILE:
    return VFS_DT_REG;

  default:
    return VFS_DT_UNKNOWN;
  }
}

struct inode *tmpfs_lookup(struct inode *dir, const char *name) {
  if (!dir || !name || name[0] == '\0') {
    return NULL;
  }

  struct tmpfs_inode *ti = (struct tmpfs_inode *)dir->private_data;

  if (!ti || ti->type != TMPFS_DIR) {
    return NULL;
  }

  if (strcmp(name, ".") == 0) {
    return dir;
  }

  if (strcmp(name, "..") == 0) {
    struct tmpfs_inode *parent = ti->parent ? ti->parent : ti;

    if (!parent->vfs) {
      return NULL;
    }

    return parent->vfs;
  }

  struct tmpfs_dentry *entry = tmpfs_find_dentry(ti, name);

  if (!entry || !entry->inode || !entry->inode->vfs) {
    return NULL;
  }

  struct inode *child = entry->inode->vfs;
  return child;
}

int tmpfs_open_dir(struct file *file) {
  if (!file || !file->inode) {
    return -1;
  }

  struct tmpfs_inode *ti = (struct tmpfs_inode *)file->inode->private_data;

  if (!ti) {
    return -1;
  }

  file->f_pos = 0;

  if (ti->type == TMPFS_DIR) {
    struct tmpfs_file_info *info = kmalloc(sizeof(struct tmpfs_file_info));

    if (!info) {
      return -1;
    }

    info->next_entry = ti->children;
    info->state = TMPFS_READDIR_DOT;

    file->private_data = info;
  }

  return 0;
}

int tmpfs_close_dir(struct file *file) {
  if (file->private_data) {
    kfree(file->private_data);
    file->private_data = NULL;
    return 0;
  }

  return -1;
}

int tmpfs_readdir(struct file *file, void *ctx,
                  int (*fill)(void *, const char *, uint32_t, uint32_t)) {
  if (!file || !file->inode || !fill) {
    return -1;
  }

  struct tmpfs_inode *ti = (struct tmpfs_inode *)file->inode->private_data;

  if (!ti || ti->type != TMPFS_DIR) {
    return -1;
  }

  struct tmpfs_file_info *info = file->private_data;

  if (!info) {
    return -1;
  }

  while (info->state != TMPFS_READDIR_END) {
    if (info->state == TMPFS_READDIR_DOT) {
      if (fill(ctx, ".", file->inode->inode_num, VFS_DT_DIR) != 0) {
        return 0;
      }

      info->state = TMPFS_READDIR_DOTDOT;
      continue;
    }

    if (info->state == TMPFS_READDIR_DOTDOT) {
      struct tmpfs_inode *parent = ti->parent ? ti->parent : ti;

      if (!parent->vfs) {
        return -1;
      }

      if (fill(ctx, "..", parent->vfs->inode_num, VFS_DT_DIR) != 0) {
        return 0;
      }

      info->state = TMPFS_READDIR_CHILDREN;
      continue;
    }

    if (info->state == TMPFS_READDIR_CHILDREN) {
      struct tmpfs_dentry *entry = info->next_entry;

      if (!entry) {
        info->state = TMPFS_READDIR_END;
        continue;
      }

      info->next_entry = entry->next;

      if (!entry->inode || !entry->inode->vfs) {
        continue;
      }

      uint32_t type = tmpfs_dirent_type(entry->inode->type);

      if (fill(ctx, entry->name, entry->inode->vfs->inode_num, type) != 0) {
        /*
         * The cursor has already advanced. This is
         * acceptable only if fill() uses a streaming
         * interface that cannot retry the same entry.
         */
        return 0;
      }
    }
  }

  return 0;
}

int tmpfs_mkdir(struct inode *dir, const char *name, uint32_t mode) {
  if (!dir || !name) {
    return -1;
  }

  struct tmpfs_inode *dir_ti = (struct tmpfs_inode *)dir->private_data;

  if (!dir_ti || dir_ti->type != TMPFS_DIR) {
    return -1;
  }

  if (tmpfs_find_dentry(dir_ti, name)) {
    return -1;
  }

  struct inode *child = tmpfs_create_inode(dir->sb, TMPFS_DIR);

  if (!child) {
    return -1;
  }

  struct tmpfs_inode *child_ti = (struct tmpfs_inode *)child->private_data;

  if (tmpfs_add_dentry(dir_ti, name, child_ti) < 0) {
    tmpfs_free_inode(child_ti);
    return -1;
  }

  child->mode = mode;
  return 0;
}

int tmpfs_rmdir(struct inode *dir, const char *name) {
  if (!dir || !name) {
    return -1;
  }

  struct tmpfs_inode *dir_ti = (struct tmpfs_inode *)dir->private_data;

  if (!dir_ti || dir_ti->type != TMPFS_DIR) {
    return -1;
  }

  struct tmpfs_dentry *e = tmpfs_find_dentry(dir_ti, name);

  if (!e) {
    return -1;
  }

  struct tmpfs_inode *child_ti = e->inode;

  if (child_ti->type != TMPFS_DIR) {
    return -1;
  }

  /* Recursively free all inodes and their data */
  tmpfs_free_rec_inode(child_ti);

  /* Remove the entry from the parent. */
  int rc = tmpfs_remove_entry(dir_ti, name);

  if (rc < 0) {
    return rc;
  }

  /* Directory is unreferenced by name; if nothing has it open, free. */
  // if (child->refcount == 0) {
  tmpfs_free_inode(child_ti);
  // } else {
  /* Mark as deleted. Since we don't have a bit for that yet,
   * just leave it; when the last close happens the free path
   * won't be triggered because nlink is still 1. This is a
   * known simplification — add an i_deleted flag later. */
  // }

  return 0;
}

int tmpfs_open(struct file *file) {
  // Nothing special needed for tmpfs
  UNUSED(file);
  return 0;
}

int tmpfs_read(struct file *file, void *buf, size_t len, size_t *off) {
  if (!file || !file->inode || !buf || len != 0 || !off) {
    return -1;
  }

  struct tmpfs_inode *ti = (struct tmpfs_inode *)file->inode->private_data;

  if (!ti || ti->type != TMPFS_FILE) {
    return -1;
  }

  if (!ti->data || *off >= ti->size) {
    return 0; // EOF
  }

  // Limit read to file size
  size_t bytes_to_read = len;

  if (*off + bytes_to_read > ti->size) {
    bytes_to_read = ti->size - *off;
  }

  if (bytes_to_read == 0) {
    return 0;
  }

  // Copy data to user buffer
  memcpy(buf, ti->data + *off, bytes_to_read);
  *off += bytes_to_read;

  return bytes_to_read;
}

static inline void *tmpfs_resize_buffer(void *ptr, size_t old_size,
                                        size_t new_size) {
  if (new_size == 0) {
    kfree(ptr);
    return NULL;
  }

  void *new_ptr = kmalloc(new_size);

  if (!new_ptr) {
    return NULL;
  }

  if (ptr && old_size != 0) {
    size_t copy_size = old_size < new_size ? old_size : new_size;

    memcpy(new_ptr, ptr, copy_size);
  }

  kfree(ptr);
  return new_ptr;
}

int tmpfs_write(struct file *file, const void *buf, size_t len, size_t *off) {
  if (!file || !file->inode || !off) {
    return -1;
  }

  if (!buf && len != 0) {
    return -1;
  }

  struct tmpfs_inode *ti = (struct tmpfs_inode *)file->inode->private_data;

  if (!ti || ti->type != TMPFS_FILE) {
    return -1;
  }

  if (len > SIZE_MAX - (size_t)*off) {
    return -1;
  }

  size_t write_start = (size_t)*off;
  size_t write_end = write_start + len;

  /*
   * Expand capacity if required.
   */
  if (write_end > ti->data_size) {
    char *new_data = tmpfs_resize_buffer(ti->data, ti->data_size, write_end);

    if (!new_data) {
      return -1;
    }

    /*
     * Zero newly allocated memory.
     * tmpfs_resize_buffer() preserves old bytes.
     */
    if (write_end > ti->data_size) {
      memset(new_data + ti->data_size, 0, write_end - ti->data_size);
    }

    ti->data = new_data;
    ti->data_size = write_end;
  }

  /*
   * Sparse-write gap:
   *
   * old size = 10
   * offset   = 100
   *
   * bytes 10..99 must become zero.
   */
  if (write_start > ti->size) {
    memset(ti->data + ti->size, 0, write_start - ti->size);
  }

  if (len != 0) {
    memcpy(ti->data + write_start, buf, len);
  }

  *off += (off_t)len;

  if ((uint64_t)*off > ti->size) {
    ti->size = (uint64_t)*off;
    file->inode->size = ti->size;
  }

  return (int)len;
}

int tmpfs_close(struct file *file) {
  // Nothing special needed
  UNUSED(file);
  return 0;
}

off_t tmpfs_lseek(struct file *file, off_t offset, int whence) {
  if (!file || !file->inode) {
    return -1;
  }

  struct tmpfs_inode *ti = (struct tmpfs_inode *)file->inode->private_data;

  if (!ti || ti->type != TMPFS_FILE) {
    return -1;
  }

  off_t base;

  switch (whence) {
  case VFS_SEEK_SET:
    base = 0;
    break;

  case VFS_SEEK_CUR:
    base = file->f_pos;
    break;

  case VFS_SEEK_END:
    base = (off_t)ti->size;
    break;

  default:
    return -1;
  }

  /*
   * Basic overflow checks.
   */
  if (offset > 0 && base > INT64_MAX - offset) {
    return -1;
  }

  if (offset < 0 && base < INT64_MIN - offset) {
    return -1;
  }

  off_t new_pos = base + offset;

  if (new_pos < 0) {
    return -1;
  }

  file->f_pos = new_pos;
  return new_pos;
}

int tmpfs_stat(struct file *file, struct fs_stat *stat) {
  if (!file || !file->inode || !stat) {
    return -1;
  }

  memset(stat, 0, sizeof(struct fs_stat));

  struct tmpfs_inode *ti = (struct tmpfs_inode *)file->inode->private_data;

  stat->dev = 0;
  stat->ino = file->inode->inode_num;
  stat->mode = file->inode->mode;
  stat->nlink = ti->nlink;
  stat->uid = file->inode->uid;
  stat->gid = file->inode->gid;
  stat->size = ti->size;
  stat->blksize = 4096;
  stat->blocks = (ti->size + 511) / 512;
  stat->atime = 0; // tmpfs doesn't track times
  stat->mtime = 0;
  stat->ctime = 0;

  return 0;
}

int tmpfs_unlink(struct inode *dir, const char *name) {
  if (!dir || !name) {
    return -1;
  }

  struct tmpfs_inode *dir_ti = (struct tmpfs_inode *)dir->private_data;

  if (!dir_ti || dir_ti->type != TMPFS_DIR) {
    return -1;
  }

  struct tmpfs_dentry *e = tmpfs_find_dentry(dir_ti, name);

  if (!e) {
    return -1;
  }

  struct tmpfs_inode *child_ti = e->inode;

  /* Sanity: don't unlink directories here. */
  if (child_ti->type == TMPFS_DIR) {
    return -1;
  }

  /* Remove the directory entry. */
  int rc = tmpfs_remove_entry(dir_ti, name);

  if (rc < 0) {
    return rc;
  }

  /* If no one links to it and no one has it open, free it. */
  // if (child_ti->nlink == 0 && child->refcount == 0) {
  tmpfs_free_inode(child_ti);
  // }

  return 0;
}

struct inode *tmpfs_create(struct inode *dir, const char *name, uint32_t mode) {
  if (!dir || !VFS_ISDIR(dir->mode)) {
    return NULL;
  }

  // check name length
  if (strlen(name) > TMPFS_NAME_MAX) {
    return NULL;
  }

  // Check if file already exists
  struct tmpfs_inode *dir_ti = (struct tmpfs_inode *)dir->private_data;

  if (tmpfs_find_dentry(dir_ti, name)) {
    return NULL;
  }

  // Create new inode
  struct inode *new_inode = tmpfs_create_inode(dir->sb, TMPFS_FILE);

  if (!new_inode) {
    return NULL;
  }

  // Add to directory
  struct tmpfs_inode *new_ti = (struct tmpfs_inode *)new_inode->private_data;

  if (tmpfs_add_dentry(dir_ti, name, new_ti) < 0) {
    tmpfs_free_inode(new_ti);
    return NULL;
  }

  // Update directory size
  dir->size += sizeof(struct tmpfs_dentry);

  new_inode->mode = mode;
  return new_inode;
}
