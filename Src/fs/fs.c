#include <device/block.h>
#include <device/device.h>
#include <fs/devfs/devfs.h>
#include <fs/fat/fat32.h>
#include <fs/fs.h>
#include <fs/tmpfs/tmpfs.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <process/locks.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>

#include "utils.h"

static struct mount_table mount_table = {0};
static spinlock_t mount_table_lock;

static struct file* file_open_table = NULL;
static uint32_t file_open_count = 0;
static spinlock_t file_open_table_lock;

static struct dentry* dcache_root = NULL;
static struct dentry* lru_head = NULL;  // Oldest entries
static struct dentry* lru_tail = NULL;  // Newest entries
static uint32_t dcache_count = 0;
static spinlock_t dcache_lock;

static struct inode* dev_dir_inode = NULL;

static void dcache_push(struct dentry* dentry);

struct mount_ops* get_fs_mount_ops(enum filesystem_type fs_type) {
  switch (fs_type) {
    case FS_TYPE_TMPFS:
      return tmpfs_get_mount_ops();

    case FS_TYPE_FAT32:
      return fat32_get_mount_ops();

    default:
      return NULL;
  }
}

struct superblock* get_fs_driver(enum filesystem_type fs_type,
                                 struct block_device* root_dev) {
  switch (fs_type) {
    case FS_TYPE_TMPFS:
      return tmpfs_create_superblock();

    case FS_TYPE_DEVFS:
      return devfs_create_superblock();

    case FS_TYPE_FAT32:
      return fat32_create_superblock(root_dev);

    default:
      return NULL;
  }
}

int fs_init(void) {
  // init all the locks
  spinlock_init(&mount_table_lock);
  spinlock_init(&file_open_table_lock);
  spinlock_init(&dcache_lock);

  struct block_device* root_dev = NULL;
  enum filesystem_type fs_type = FS_TYPE_UNKNOWN;

  int result = prepare_kernel_partition(&root_dev, &fs_type);

  if (result != 0 || !root_dev || fs_type == FS_TYPE_UNKNOWN) {
    log_error("Failed to prepare kernel partition. Error code: %d", result);
    return -1;
  }

  struct superblock* sb = get_fs_driver(fs_type, root_dev);

  if (!sb) {
    log_error("Failed to create filesystem superblock for type: %d", fs_type);
    return -1;
  }

  mount_table_init();

  result = mount_root(sb);

  if (result != 0) {
    log_error("Failed to mount root filesystem. Error code: %d", result);
    return -1;
  }

  create_required_directories();

  // mount virtual filesystems like /dev, /proc, /sys, etc.

  struct superblock* devfs_sb = get_fs_driver(FS_TYPE_DEVFS, NULL);
  result = mount_filesystem("/dev", devfs_sb);

  if (result != 0) {
    log_error("Failed to mount devfs at /dev. Error code: %d", result);
    return -1;
  }

  dev_dir_inode = devfs_sb->root;

  struct superblock* tmpfs_sb = get_fs_driver(FS_TYPE_TMPFS, NULL);
  result = mount_filesystem("/tmp", tmpfs_sb);

  if (result != 0) {
    log_error("Failed to mount tmpfs at /tmp. Error code: %d", result);
    return -1;
  }

  register_basic_devices();
  register_all_devices();

  print_dirs("/userprograms");
  return 0;
}

struct inode* fs_get_dev_dir_inode(void) { return dev_dir_inode; }

static struct dentry* dcache_add(struct dentry* parent, const char* name,
                                 struct inode* inode) {
  if (!parent || !name || !inode) {
    return NULL;
  }

  struct dentry* new_dentry = kmalloc(sizeof(struct dentry));

  if (!new_dentry) {
    return NULL;
  }

  memset(new_dentry, 0, sizeof(struct dentry));

  strcpy(new_dentry->name, name);
  new_dentry->inode = inode;
  new_dentry->child = NULL;
  new_dentry->sibling_prev = NULL;
  new_dentry->d_refcount = 1;
  new_dentry->d_flags = 0;
  new_dentry->lru_prev = NULL;
  new_dentry->lru_next = NULL;

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&dcache_lock, flags);

  new_dentry->parent = parent;
  new_dentry->sibling_next = parent->child;

  if (parent->child) {
    parent->child->sibling_prev = new_dentry;
  }

  parent->child = new_dentry;
  dcache_count++;

  SPIN_LOCK_RELEASE(&dcache_lock, flags);

  if (inode) {
    __atomic_fetch_add(&inode->refcount, 1, __ATOMIC_SEQ_CST);
  }

  __atomic_fetch_add(&parent->d_refcount, 1, __ATOMIC_SEQ_CST);

  return new_dentry;
}

static void dcache_shrink(void) {
  if (dcache_count <= DCACHE_MAX_ENTRIES || lru_head == NULL) {
    return;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&dcache_lock, flags);

  struct dentry* victim = lru_head;

  // Remove from LRU list
  lru_head = victim->lru_next;

  if (lru_head) {
    lru_head->lru_prev = NULL;
  } else {
    lru_tail = NULL;
  }

  // Unlink from the VFS tree (Parent/Sibling pointers)
  if (!victim->parent) {
    SPIN_LOCK_RELEASE(&dcache_lock, flags);
    return;  // Should not happen, but safety check
  }

  if (victim->sibling_prev) {
    victim->sibling_prev->sibling_next = victim->sibling_next;
  } else {
    victim->parent->child = victim->sibling_next;
  }

  if (victim->sibling_next) {
    victim->sibling_next->sibling_prev = victim->sibling_prev;
  }

  // add parent to LRU list, if it has no more children
  struct dentry* parent = victim->parent;

  dcache_count--;
  SPIN_LOCK_RELEASE(&dcache_lock, flags);

  // destroy inode
  if (victim->inode &&
      __atomic_sub_fetch(&victim->inode->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
    if (victim->inode->fops && victim->inode->fops->destroy) {
      victim->inode->fops->destroy(victim->inode);
    }
  }

  if (!parent) {
    kfree(victim);
    return;
  }

  SPIN_LOCK_ACQUIRE(&dcache_lock, flags);

  // If the parent is already in the LRU list, we don't need to push it again.
  if (CHECK_BIT(parent->d_flags, MASK_DENTRY_INLRU)) {
    SPIN_LOCK_RELEASE(&dcache_lock, flags);
    return;
  }

  if (__atomic_sub_fetch(&parent->d_refcount, 1, __ATOMIC_SEQ_CST) == 0) {
    parent->lru_prev = lru_tail;
    parent->lru_next = NULL;

    if (lru_tail) {
      lru_tail->lru_next = parent;
    } else {
      lru_head = parent;
    }

    lru_tail = parent;
    SET_BIT(parent->d_flags, MASK_DENTRY_INLRU);
  }

  SPIN_LOCK_RELEASE(&dcache_lock, flags);

  kfree(victim);
}

static void dcache_push(struct dentry* dentry) {
  if (!dentry || dentry == dcache_root) {
    return;
  }

  // If the dentry is already in the LRU list, we don't need to push it again.
  if (CHECK_BIT(dentry->d_flags, MASK_DENTRY_INLRU)) {
    return;
  }

  if (__atomic_sub_fetch(&dentry->d_refcount, 1, __ATOMIC_SEQ_CST) == 0) {
    unsigned long flags;
    SPIN_LOCK_ACQUIRE(&dcache_lock, flags);

    dentry->lru_prev = lru_tail;
    dentry->lru_next = NULL;

    if (lru_tail) {
      lru_tail->lru_next = dentry;
    } else {
      lru_head = dentry;
    }

    lru_tail = dentry;
    SET_BIT(dentry->d_flags, MASK_DENTRY_INLRU);

    SPIN_LOCK_RELEASE(&dcache_lock, flags);
  }

  // Check if we need to free memory
  dcache_shrink();
}

static void dcache_pop(struct dentry* dentry) {
  if (!dentry) {
    return;
  }

  if (CHECK_BIT(dentry->d_flags, MASK_DENTRY_INLRU)) {
    if (dentry->lru_prev) {
      dentry->lru_prev->lru_next = dentry->lru_next;
    } else {
      lru_head = dentry->lru_next;
    }

    if (dentry->lru_next) {
      dentry->lru_next->lru_prev = dentry->lru_prev;
    } else {
      lru_tail = dentry->lru_prev;
    }

    dentry->lru_prev = dentry->lru_next = NULL;
    CLEAR_BIT(dentry->d_flags, MASK_DENTRY_INLRU);
  }

  __atomic_fetch_add(&dentry->d_refcount, 1, __ATOMIC_SEQ_CST);
}

static void dcache_remove(struct dentry* target) {
  if (!target || !target->parent) {
    return;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&dcache_lock, flags);

  // remove from dcache tree
  struct dentry* parent = target->parent;

  if (target->sibling_prev) {
    target->sibling_prev->sibling_next = target->sibling_next;
  } else {
    parent->child = target->sibling_next;
  }

  if (target->sibling_next) {
    target->sibling_next->sibling_prev = target->sibling_prev;
  }

  // is in LRU list? if so, remove from LRU list
  if (CHECK_BIT(target->d_flags, MASK_DENTRY_INLRU)) {
    if (target->lru_prev) {
      target->lru_prev->lru_next = target->lru_next;
    } else {
      lru_head = target->lru_next;
    }

    if (target->lru_next) {
      target->lru_next->lru_prev = target->lru_prev;
    } else {
      lru_tail = target->lru_prev;
    }
  }

  dcache_count--;
  SPIN_LOCK_RELEASE(&dcache_lock, flags);

  // remove inode if refcount is 0
  if (target->inode &&
      __atomic_sub_fetch(&target->inode->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
    if (target->inode->fops && target->inode->fops->destroy) {
      target->inode->fops->destroy(target->inode);
    }
  }

  kfree(target);
  dcache_push(parent);
}

static struct dentry* dcache_lookup(struct dentry* parent, const char* name) {
  if (!parent || !parent->child) {
    return NULL;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&dcache_lock, flags);

  struct dentry* current = parent->child;

  while (current) {
    if (strcmp(current->name, name) == 0) {
      dcache_pop(current);
      SPIN_LOCK_RELEASE(&dcache_lock, flags);
      return current;
    }
    current = current->sibling_next;
  }

  SPIN_LOCK_RELEASE(&dcache_lock, flags);
  return NULL;
}

static struct file* add_file(struct dentry* dentry, struct inode* inode,
                             uint32_t flags) {
  struct file* new_file = kmalloc(sizeof(struct file));

  if (!new_file) {
    return NULL;
  }

  memset(new_file, 0, sizeof(struct file));

  new_file->dentry = dentry;
  new_file->inode = inode;
  new_file->f_pos = 0;
  new_file->f_flags = flags;
  new_file->f_refcount = 1;
  new_file->private_data = NULL;
  new_file->prev = NULL;

  unsigned long flags_lock;

  SPIN_LOCK_ACQUIRE(&file_open_table_lock, flags_lock);

  // Add to the head of the open file table
  new_file->next = file_open_table;

  if (file_open_table) {
    file_open_table->prev = new_file;
  }

  file_open_table = new_file;
  file_open_count++;

  SPIN_LOCK_RELEASE(&file_open_table_lock, flags_lock);

  return new_file;
}

static void remove_file(struct file* f) {
  if (!f) {
    return;
  }

  unsigned long flags;

  SPIN_LOCK_ACQUIRE(&file_open_table_lock, flags);

  // Remove from the open file table
  if (f->prev) {
    f->prev->next = f->next;
  } else {
    file_open_table = f->next;  // Update head if it's the first file
  }

  if (f->next) {
    f->next->prev = f->prev;
  }

  file_open_count--;

  SPIN_LOCK_RELEASE(&file_open_table_lock, flags);

  kfree(f);
}

static inline char* vfs_strdup(const char* s) {
  if (!s) {
    return NULL;
  }

  size_t len = strlen(s);
  char* copy = kmalloc(len + 1);

  if (!copy) {
    return NULL;
  }

  memcpy(copy, s, len);
  copy[len] = '\0';

  return copy;
}

static inline struct inode* follow_mount(struct inode* inode) {
  while (inode && inode->mounted) {
    if (!inode->mounted->sb || !inode->mounted->sb->root) {
      return NULL;
    }
    inode = inode->mounted->sb->root;
  }

  return inode;
}

static struct dentry* lookup_component(struct dentry* current,
                                       const char* component) {
  if (!current || !component || component[0] == '\0') {
    return NULL;
  }

  if (!VFS_ISDIR(current->inode->mode)) {
    return NULL;
  }

  if (component[0] == '.' && component[1] == '\0') {
    return current;
  }

  if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
    if (current->parent) {
      return current->parent;
    } else {
      return current;
    }
  }

  struct dentry* child = dcache_lookup(current, component);

  if (child) {
    return child;
  }

  struct inode* parent_inode = current->inode;

  if (!parent_inode || !parent_inode->fops || !parent_inode->fops->lookup) {
    return NULL;
  }

  parent_inode = follow_mount(parent_inode);

  if (!parent_inode) {
    return NULL;
  }

  struct inode* next = parent_inode->fops->lookup(parent_inode, component);

  if (!next) {
    return NULL;
  }

  struct dentry* new_dentry = dcache_add(current, component, next);
  return new_dentry;
}

struct inode* fs_resolve_path(const char* path, struct dentry** out_dentry) {
  if (!path || !mount_table.root || !mount_table.root->sb ||
      !mount_table.root->sb->root || !dcache_root || !out_dentry) {
    return NULL;
  }

  *out_dentry = NULL;

  /*
   * Only absolute paths are accepted.
   */
  if (path[0] != '/') {
    return NULL;
  }

  struct dentry* current = dcache_root;

  // Acquire a working reference for the root before we start walking
  __atomic_fetch_add(&current->d_refcount, 1, __ATOMIC_SEQ_CST);

  /*
   * Root path.
   */
  while (*path == '/') {
    path++;
  }

  if (*path == '\0') {
    *out_dentry = current;
    return current->inode;
  }

  char* path_copy = vfs_strdup(path);

  if (!path_copy) {
    dcache_push(current);
    return NULL;
  }

  char* saveptr = NULL;
  char* token = strtok_r(path_copy, "/", &saveptr);

  while (token) {
    struct dentry* next = lookup_component(current, token);

    if (!next) {
      dcache_push(current);
      kfree(path_copy);
      return NULL;
    }

    // SUCCESS PATH: We successfully stepped down.
    // 'next' was born active (ref=1). We no longer need to look at 'current'.
    // Drop the working reference to the parent directory!
    dcache_push(current);

    current = next;
    token = strtok_r(NULL, "/", &saveptr);
  }

  kfree(path_copy);
  *out_dentry = current;
  return follow_mount(current->inode);
}

struct inode* fs_resolve_parent(const char* path, char** final_name,
                                struct dentry** out_dentry) {
  if (!path || !final_name || !out_dentry) {
    return NULL;
  }

  *out_dentry = NULL;
  *final_name = NULL;

  if (!mount_table.root || !mount_table.root->sb ||
      !mount_table.root->sb->root || !dcache_root) {
    return NULL;
  }

  /*
   * This implementation accepts absolute paths only.
   */
  if (path[0] != '/') {
    return NULL;
  }

  struct inode* root = dcache_root->inode;
  struct dentry* current = dcache_root;

  // ACQUIRE WORKING REFERENCE for the start of our traversal
  __atomic_fetch_add(&current->d_refcount, 1, __ATOMIC_SEQ_CST);

  /*
   * Skip all leading slashes.
   *
   * "/" and "////" both refer to the root directory.
   */
  while (*path == '/') {
    path++;
  }

  if (*path == '\0') {
    *out_dentry = current;
    return root;
  }

  char* path_copy = vfs_strdup(path);

  if (!path_copy) {
    dcache_push(current);
    return NULL;
  }

  /*
   * Remove trailing slashes.
   *
   * strtok_r() already ignores repeated separators, but removing
   * trailing slashes makes the final component handling explicit.
   */
  size_t length = strlen(path_copy);

  while (length > 0 && path_copy[length - 1] == '/') {
    path_copy[length - 1] = '\0';
    length--;
  }

  if (length == 0) {
    kfree(path_copy);
    *out_dentry = current;
    return root;
  }

  char* saveptr = NULL;
  char* token = strtok_r(path_copy, "/", &saveptr);

  if (!token) {
    kfree(path_copy);
    *out_dentry = current;
    return root;
  }

  while (true) {
    char* next = strtok_r(NULL, "/", &saveptr);

    /*
     * No next component means token is the final component.
     */
    if (!next) {
      *final_name = vfs_strdup(token);

      if (!*final_name) {
        kfree(path_copy);
        dcache_push(current);
        return NULL;
      }

      break;
    }

    /*
     * token is an intermediate component and must be a directory.
     */
    struct dentry* next_dentry = lookup_component(current, token);

    if (!next_dentry) {
      kfree(path_copy);
      dcache_push(current);
      return NULL;
    }

    if (!VFS_ISDIR(next_dentry->inode->mode)) {
      kfree(path_copy);
      // We acquired 'next_dentry', but it's invalid.
      // We must drop both the child AND the parent!
      dcache_push(next_dentry);
      dcache_push(current);
      return NULL;
    }

    // SUCCESS PATH: We stepped down into a valid directory.
    // Drop the working reference to the old parent.
    dcache_push(current);

    current = next_dentry;
    token = next;
  }

  kfree(path_copy);
  *out_dentry = current;
  return follow_mount(current->inode);
}

struct file* fs_open(const char* path, uint32_t flags) {
  if (!path || path[0] == '\0') {
    return NULL;
  }

  /*
   * Validate the access mode.`
   *
   * Usually the access mode is encoded in a mask rather than
   * independently checking all three flags.
   */
  uint32_t access_mode = flags & VFS_O_ACCMODE;

  if (access_mode != VFS_O_RDONLY && access_mode != VFS_O_WRONLY &&
      access_mode != VFS_O_RDWR) {
    return NULL;
  }

  char* filename = NULL;
  struct dentry* parent_dentry = NULL;
  struct inode* dir_inode = fs_resolve_parent(path, &filename, &parent_dentry);

  if (!filename) {
    dcache_push(parent_dentry);
    return NULL;
  }

  if (!dir_inode || !parent_dentry) {
    kfree(filename);
    return NULL;
  }

  if (!VFS_ISDIR(dir_inode->mode) || !dir_inode->fops) {
    kfree(filename);
    dcache_push(parent_dentry);
    return NULL;
  }

  mutex_acquire(&dir_inode->lock);

  /*
   * Look up the final component.
   */
  struct dentry* file_dentry = lookup_component(parent_dentry, filename);
  struct inode* file_inode = file_dentry ? file_dentry->inode : NULL;

  /*
   * If the file already exists and O_CREAT and O_EXCL are both specified,
   * we should return an error.
   */
  if (file_inode && (flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
    mutex_release(&dir_inode->lock);
    kfree(filename);
    dcache_push(parent_dentry);
    dcache_push(file_dentry);
    return NULL;
  }

  /*
   * Create only if the final component does not exist.
   */
  if (!file_inode && (flags & VFS_O_CREAT)) {
    if (!dir_inode->fops->create) {
      mutex_release(&dir_inode->lock);
      kfree(filename);
      dcache_push(parent_dentry);
      return NULL;
    }

    uint32_t mode = flags & 0xFFF;  // Extract permission bits (lower 12 bits)
    mode |= VFS_IFREG;              // Set the file type to regular file

    file_inode = dir_inode->fops->create(dir_inode, filename, mode);

    if (!file_inode) {
      mutex_release(&dir_inode->lock);
      kfree(filename);
      dcache_push(parent_dentry);
      return NULL;
    }

    file_dentry = dcache_add(parent_dentry, filename, file_inode);

    if (!file_dentry) {
      mutex_release(&dir_inode->lock);
      kfree(filename);
      dcache_push(parent_dentry);
      return NULL;
    }
  }

  /*
   * The file does not exist and O_CREAT was not specified.
   */
  if (!file_inode) {
    mutex_release(&dir_inode->lock);
    kfree(filename);
    dcache_push(file_dentry);
    dcache_push(parent_dentry);
    return NULL;
  }

  mutex_release(&dir_inode->lock);
  kfree(filename);

  /*
   * Only regular files and directories are supported here.
   */
  // if (!VFS_ISREG(file_inode->mode) && !VFS_ISDIR(file_inode->mode)) {
  //   return NULL;
  // }

  /*
   * A directory cannot be opened for normal write access.
   */
  if (VFS_ISDIR(file_inode->mode) &&
      (access_mode == VFS_O_WRONLY || access_mode == VFS_O_RDWR)) {
    dcache_push(file_dentry);
    dcache_push(parent_dentry);
    return NULL;
  }

  /*
   * O_TRUNC requires write access and must not apply to directories.
   */
  if ((flags & VFS_O_TRUNC) && access_mode == VFS_O_RDONLY) {
    dcache_push(file_dentry);
    dcache_push(parent_dentry);
    return NULL;
  }

  struct file* f = add_file(file_dentry, file_inode, flags);

  if (!f) {
    dcache_push(file_dentry);
    dcache_push(parent_dentry);
    return NULL;
  }

  __atomic_fetch_add(&file_inode->refcount, 1, __ATOMIC_SEQ_CST);

  /*
   * Truncation should be performed through the filesystem operation,
   * not only by changing the generic inode size.
   */
  // not implemented:

  /*
   * Filesystem-specific open operation.
   */
  if (file_inode->fops && file_inode->fops->open) {
    int err = file_inode->fops->open(f);

    if (err != 0) {
      kfree(f);
      dcache_push(file_dentry);
      dcache_push(parent_dentry);
      return NULL;
    }
  }

  dcache_push(parent_dentry);
  return f;
}

size_t fs_read(struct file* f, void* buf, size_t len) {
  if (!f || !f->inode) {
    return -1;
  }

  if (len > 0 && !buf) {
    return -1;
  }

  /*
   * O_WRONLY cannot read.
   *
   * O_RDONLY is usually zero, so checking:
   *
   *     f->f_flags & O_RDONLY
   *
   * would not work.
   */
  uint32_t access_mode = f->f_flags & VFS_O_ACCMODE;

  if (access_mode == VFS_O_WRONLY) {
    return -1;
  }

  if (!f->inode->fops || !f->inode->fops->read) {
    return -1;
  }

  return f->inode->fops->read(f, buf, len, (size_t*)&f->f_pos);
}

size_t fs_write(struct file* f, const void* buf, size_t len) {
  if (!f || !f->inode) {
    return -1;
  }

  if (len > 0 && !buf) {
    return -1;
  }

  uint32_t access_mode = f->f_flags & VFS_O_ACCMODE;

  if (access_mode == VFS_O_RDONLY) {
    return -1;
  }

  if (!f->inode->fops || !f->inode->fops->write) {
    return -1;
  }

  return f->inode->fops->write(f, buf, len, (size_t*)&f->f_pos);
}

static inline int close(struct file* f) {
  if (!f) {
    return -1;
  }

  if (__atomic_sub_fetch(&f->f_refcount, 1, __ATOMIC_SEQ_CST) != 0) {
    return 0;
  }

  if (f->inode) {
    if (__atomic_sub_fetch(&f->inode->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
      if (f->inode->fops && f->inode->fops->close) {
        f->inode->fops->close(f);
      }
    }
  }

  dcache_push(f->dentry);
  remove_file(f);
  return 0;
}

int fs_close(struct file* f) { return close(f); }

off_t fs_lseek(struct file* f, off_t off, int whence) {
  if (!f || !f->inode) {
    return -1;
  }

  /*
   * Directories should normally not use lseek().
   * Remove this check if your design supports directory offsets.
   */
  if (VFS_ISDIR(f->inode->mode)) {
    return -1;
  }

  if (f->f_pos < 0 || f->inode->size < 0) {
    return -1;
  }

  size_t base;

  switch (whence) {
    case VFS_SEEK_SET:
      base = 0;
      break;

    case VFS_SEEK_CUR:
      base = f->f_pos;
      break;

    case VFS_SEEK_END:
      base = f->inode->size;
      break;

    default:
      return -1;
  }

  /*
   * Detect signed overflow in base + off.
   */
  if (off > 0 && base > INT64_MAX - off) {
    return -1;
  }

  if (off < 0 && base < INT64_MIN - off) {
    return -1;
  }

  off_t new_pos = base + off;

  if (new_pos < 0) {
    return -1;
  }

  f->f_pos = new_pos;
  return new_pos;
}

int fs_stat(const char* path, struct fs_stat* st) {
  char* filename = NULL;
  struct dentry* parent_dentry = NULL;
  struct inode* dir_inode = fs_resolve_parent(path, &filename, &parent_dentry);

  if (!filename) {
    dcache_push(parent_dentry);
    return -1;
  }

  if (!dir_inode || !parent_dentry) {
    kfree(filename);
    return -1;
  }

  struct dentry* file_dentry = lookup_component(parent_dentry, filename);
  struct inode* file_inode = file_dentry ? file_dentry->inode : NULL;

  if (!file_inode) {
    kfree(filename);
    dcache_push(parent_dentry);
    return -1;
  }

  kfree(filename);
  memset(st, 0, sizeof(*st));

  st->ino = file_inode->inode_num;
  st->mode = file_inode->mode;
  st->nlink = 0;
  st->uid = file_inode->uid;
  st->gid = file_inode->gid;
  st->size = file_inode->size;
  st->blksize = 4096;
  st->blocks = (file_inode->size + 511) / 512;
  st->atime = file_inode->atime;
  st->mtime = file_inode->mtime;
  st->ctime = file_inode->ctime;

  dcache_push(parent_dentry);
  dcache_push(file_dentry);
  return 0;
}

int fs_unlink(const char* path) {
  char* filename = NULL;
  struct dentry* dir_dentry = NULL;
  struct inode* dir_inode = fs_resolve_parent(path, &filename, &dir_dentry);

  if (!filename) {
    dcache_push(dir_dentry);
    return -1;
  }

  if (!dir_inode || !dir_dentry) {
    kfree(filename);
    return -1;
  }

  mutex_acquire(&dir_inode->lock);

  struct dentry* target_dentry = lookup_component(dir_dentry, filename);
  struct inode* target_inode = target_dentry->inode;

  if (!target_dentry || !target_inode) {
    mutex_release(&dir_inode->lock);
    kfree(filename);
    dcache_push(dir_dentry);
    return -1;
  }

  // check is file open
  if (target_dentry->d_refcount > 1 || target_inode->refcount > 1) {
    mutex_release(&dir_inode->lock);
    kfree(filename);
    dcache_push(dir_dentry);
    dcache_push(target_dentry);
    return -1;
  }

  if (!dir_inode->fops || !dir_inode->fops->unlink) {
    mutex_release(&dir_inode->lock);
    kfree(filename);
    dcache_push(dir_dentry);
    dcache_push(target_dentry);
    return -1;
  }

  int rc = dir_inode->fops->unlink(dir_inode, filename);

  if (rc == 0) {
    dcache_remove(target_dentry);
  }

  mutex_release(&dir_inode->lock);
  kfree(filename);
  dcache_push(dir_dentry);
  return rc;
}

struct file* fs_open_dir(const char* path) {
  struct dentry* target_dentry = NULL;
  struct inode* target_inode = fs_resolve_path(path, &target_dentry);

  if (!target_inode || !target_dentry) {
    return NULL;
  }

  if (!VFS_ISDIR(target_inode->mode)) {
    dcache_push(target_dentry);
    return NULL;
  }

  struct file* f = add_file(target_dentry, target_inode, VFS_O_RDONLY);

  if (!f) {
    dcache_push(target_dentry);
    return NULL;
  }

  __atomic_fetch_add(&target_inode->refcount, 1, __ATOMIC_SEQ_CST);

  if (target_inode->fops && target_inode->fops->open) {
    int err = target_inode->fops->open(f);

    if (err != 0) {
      kfree(f);
      dcache_push(target_dentry);
      return NULL;
    }
  }

  return f;
}

int fs_readdir(struct file* file,
               int (*fill)(void*, const char*, uint32_t, uint32_t), void* ctx) {
  if (!file) {
    return -1;
  }

  if (!VFS_ISDIR(file->inode->mode)) {
    return -1;
  }

  int err = 0;

  if (file->inode->fops && file->inode->fops->readdir) {
    err = file->inode->fops->readdir(file, ctx, fill);
  }

  return err;
}

int fs_close_dir(struct file* f) { return close(f); }

int fs_mkdir(const char* path, uint32_t mode) {
  char* child_name = NULL;
  struct dentry* parent_dentry = NULL;
  struct inode* parent_inode =
      fs_resolve_parent(path, &child_name, &parent_dentry);

  mutex_acquire(&parent_inode->lock);

  if (!child_name) {
    mutex_release(&parent_inode->lock);
    dcache_push(parent_dentry);
    return -1;
  }

  if (!parent_inode || !parent_dentry) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    return -2;
  }

  if (!VFS_ISDIR(parent_inode->mode)) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    return -3;
  }

  if (!parent_inode->fops || !parent_inode->fops->mkdir) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    return -4;
  }

  mode |= VFS_IFDIR;  // Ensure the mode indicates a directory

  int rc = parent_inode->fops->mkdir(parent_inode, child_name, mode);

  mutex_release(&parent_inode->lock);
  kfree(child_name);
  dcache_push(parent_dentry);
  return rc;
}

int fs_rmdir(const char* path) {
  char* child_name = NULL;
  struct dentry* parent_dentry = NULL;
  struct inode* parent_inode =
      fs_resolve_parent(path, &child_name, &parent_dentry);

  mutex_acquire(&parent_inode->lock);

  if (!child_name) {
    mutex_release(&parent_inode->lock);
    dcache_push(parent_dentry);
    return -1;
  }

  if (!parent_inode || !parent_dentry) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    return -1;
  }

  if (!VFS_ISDIR(parent_inode->mode)) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    return -1;
  }

  struct dentry* target_dentry = NULL;
  target_dentry = lookup_component(parent_dentry, child_name);

  struct inode* target_inode = target_dentry->inode;

  if (!target_dentry || !target_inode) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    return -1;
  }

  /* Refuse to rmdir a non-directory; use unlink. */
  if (!VFS_ISDIR(target_inode->mode)) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    dcache_push(target_dentry);
    return -1;
  }

  // check if directory is busy
  if (target_dentry->d_refcount > 1 || target_inode->refcount > 1) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    dcache_push(target_dentry);
    return -1;
  }

  if (!parent_inode->fops || !parent_inode->fops->rmdir) {
    mutex_release(&parent_inode->lock);
    kfree(child_name);
    dcache_push(parent_dentry);
    dcache_push(target_dentry);
    return -1;
  }

  int rc = parent_inode->fops->rmdir(parent_inode, child_name);

  if (rc == 0) {
    dcache_remove(target_dentry);
  }

  mutex_release(&parent_inode->lock);
  kfree(child_name);
  dcache_push(parent_dentry);
  return rc;
}

// Initialize mount table
void mount_table_init(void) {
  memset(&mount_table, 0, sizeof(struct mount_table));
}

// mount root '/'
int mount_root(struct superblock* sb) {
  struct mount* root_mount = kmalloc(sizeof(struct mount));

  if (!root_mount) {
    return -1;
  }

  root_mount->mount_point = NULL;
  root_mount->sb = sb;
  root_mount->next = NULL;
  root_mount->flags = 0;
  root_mount->ops = NULL;

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&mount_table_lock, flags);

  mount_table.root = root_mount;
  mount_table.count = 1;

  SPIN_LOCK_RELEASE(&mount_table_lock, flags);

  dcache_root = kmalloc(sizeof(struct dentry));

  if (!dcache_root) {
    kfree(root_mount);
    return -1;
  }

  memset(dcache_root, 0, sizeof(struct dentry));

  strcpy(dcache_root->name, "/");
  dcache_root->inode = sb->root;
  dcache_root->parent = NULL;
  dcache_root->child = NULL;
  dcache_root->sibling_prev = NULL;
  dcache_root->sibling_next = NULL;
  dcache_root->d_refcount = 1;

  __atomic_fetch_add(&sb->root->refcount, 1, __ATOMIC_SEQ_CST);
  return 0;
}

// mount a filesystem
int mount_filesystem(const char* mount_point, struct superblock* sb) {
  if (!mount_point || !sb) {
    return -1;
  }

  struct dentry* mount_dentry = NULL;
  struct inode* mount_inode = fs_resolve_path(mount_point, &mount_dentry);

  if (!mount_inode || !mount_dentry) {
    return -1;
  }

  if (!VFS_ISDIR(mount_inode->mode) || mount_inode->mounted) {
    dcache_push(mount_dentry);
    return -1;
  }

  struct mount* mnt = kmalloc(sizeof(struct mount));

  if (!mnt) {
    dcache_push(mount_dentry);
    return -1;
  }

  mnt->mount_point = mount_dentry;
  mnt->sb = sb;
  mnt->flags = 0;
  mnt->ops = NULL;

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&mount_table_lock, flags);

  mnt->next = mount_table.head;

  mount_table.head = mnt;
  mount_table.count++;

  SPIN_LOCK_RELEASE(&mount_table_lock, flags);

  mount_inode->mounted = mnt;
  __atomic_fetch_add(&mount_inode->refcount, 1, __ATOMIC_SEQ_CST);

  if (sb->root) {
    __atomic_fetch_add(&sb->root->refcount, 1, __ATOMIC_SEQ_CST);
  }

  return 0;
}

int mount_device(const char* device, const char* mount_point,
                 enum filesystem_type fs_type) {
  if (!device || !mount_point) {
    return -1;
  }

  struct dentry* device_dentry = NULL;
  struct inode* device_inode = fs_resolve_path(device, &device_dentry);

  if (!device_inode || !device_dentry) {
    return -1;
  }

  struct dentry* mount_point_dentry = NULL;
  struct inode* mount_point_inode =
      fs_resolve_path(mount_point, &mount_point_dentry);

  if (!mount_point_inode || !mount_point_dentry) {
    dcache_push(device_dentry);
    return -1;
  }

  if (!VFS_ISDIR(mount_point_inode->mode) || mount_point_inode->mounted) {
    dcache_push(mount_point_dentry);
    dcache_push(device_dentry);
    return -1;
  }

  struct mount_ops* mnt_ops = get_fs_mount_ops(fs_type);

  if (!mnt_ops) {
    dcache_push(mount_point_dentry);
    dcache_push(device_dentry);
    return -1;
  }

  struct superblock* sb = mnt_ops->target_mount(device_inode);

  if (!sb) {
    dcache_push(mount_point_dentry);
    dcache_push(device_dentry);
    return -1;
  }

  struct mount* mnt = kmalloc(sizeof(struct mount));

  if (!mnt) {
    dcache_push(mount_point_dentry);
    dcache_push(device_dentry);
    return -1;
  }

  mnt->mount_point = mount_point_dentry;
  mnt->sb = sb;
  mnt->flags = 0;
  mnt->ops = mnt_ops;

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&mount_table_lock, flags);

  mnt->next = mount_table.head;

  mount_table.head = mnt;
  mount_table.count++;

  SPIN_LOCK_RELEASE(&mount_table_lock, flags);

  mount_point_inode->mounted = mnt;
  __atomic_fetch_add(&mount_point_inode->refcount, 1, __ATOMIC_SEQ_CST);

  if (sb->root) {
    __atomic_fetch_add(&sb->root->refcount, 1, __ATOMIC_SEQ_CST);
  }

  dcache_push(device_dentry);
  return 0;
}

static int remove_from_mount_table(struct mount* mnt) {
  if (!mnt) {
    return -1;
  }

  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&mount_table_lock, flags);

  if (mount_table.head == mnt) {
    mount_table.head = mnt->next;
    mount_table.count--;
    SPIN_LOCK_RELEASE(&mount_table_lock, flags);
    kfree(mnt);
    return 0;
  }

  struct mount* prev = mount_table.head;

  while (prev && prev->next != mnt) {
    prev = prev->next;
  }

  if (prev) {
    prev->next = mnt->next;
  }

  mount_table.count--;
  SPIN_LOCK_RELEASE(&mount_table_lock, flags);

  kfree(mnt);
  return 0;
}

int unmount_device(const char* mount_point) {
  if (!mount_point) {
    return -1;
  }

  struct dentry* mount_point_dentry = NULL;
  struct inode* mount_point_inode =
      fs_resolve_path(mount_point, &mount_point_dentry);

  if (!mount_point_inode || !mount_point_dentry) {
    return -1;
  }

  if (!mount_point_inode->mounted) {
    dcache_push(mount_point_dentry);
    return -1;
  }

  struct mount* mnt = mount_point_inode->mounted;

  // check if the mounted filesystem is busy (open files)
  if (mnt->sb && mnt->sb->root && mnt->sb->root->refcount > 1) {
    dcache_push(mount_point_dentry);
    return -1;
  }

  mount_point_inode->mounted = NULL;

  if (__atomic_sub_fetch(&mount_point_inode->refcount, 1, __ATOMIC_SEQ_CST) ==
      0) {
    if (mount_point_inode->fops && mount_point_inode->fops->destroy) {
      mount_point_inode->fops->destroy(mount_point_inode);
    }
  }

  dcache_push(mnt->mount_point);

  if (mnt && mnt->ops && mnt->ops->target_unmount) {
    mnt->ops->target_unmount(mnt->sb);
  }

  dcache_push(mount_point_dentry);
  return remove_from_mount_table(mnt);
}

// Recursive helper to print children and siblings with indentation
static void print_dcache_level(struct dentry* node, int depth) {
  struct dentry* current = node;

  // Loop through all siblings at the current directory level
  while (current != NULL) {
    // 1. Print the indentation bars based on how deep we are
    for (int i = 0; i < depth; i++) {
      log_print("    | ");
    }

    // 2. Print the current node
    // (Add a trailing slash if it's a directory to make it easy to read)
    if (current->inode && VFS_ISDIR(current->inode->mode)) {
      log_print("|-- %s/\n", current->name);
    } else {
      log_print("|-- %s\n", current->name);
    }

    // 3. If this folder has children, recurse down into them!
    if (current->child != NULL) {
      print_dcache_level(current->child, depth + 1);
    }

    // 4. Move to the next sibling in the same folder
    current = current->sibling_next;
  }
}

// The main wrapper function you can call from anywhere in your kernel
void debug_dump_dcache(void) {
  if (dcache_root == NULL) {
    log_print("Dentry Cache is empty.\n");
    return;
  }

  log_print("\n=== DENTRY CACHE TREE ===\n");
  log_print("/\n");  // Print the absolute root

  // Start the recursion on the root's first child
  print_dcache_level(dcache_root->child, 0);

  log_print("=========================\n\n");
}

void debug_dump_dcache_lru(void) {
  log_print("\n=== DENTRY CACHE LRU LIST ===\n");
  struct dentry* current = lru_head;
  int index = 0;

  while (current) {
    log_print("[%d] Dentry: %s, Inode: %p, Refcount: %u\n", index,
              current->name, current->inode, current->d_refcount);
    current = current->lru_next;
    index++;
  }

  log_print("=============================\n\n");
}

void debug_dump_mount_table(void) {
  log_print("\n=== MOUNT TABLE ===\n");
  struct mount* current = mount_table.head;
  int index = 0;

  while (current) {
    log_print(
        "[%d] Mount Point: %s, Filesystem Type: %d, Superblock Root Inode: "
        "%p\n",
        index, current->mount_point ? current->mount_point->name : "NULL",
        current->ops ? current->ops->fs_type : -1,
        current->sb ? current->sb->root : NULL);
    current = current->next;
    index++;
  }

  log_print("===================\n\n");
}

void debug_dump_open_files(void) {
  log_print("\n=== OPEN FILES ===\n");
  struct file* current = file_open_table;
  int index = 0;

  while (current) {
    log_print(
        "[%d] File: %s, Inode: %p, Position: %ld, Flags: %u, Refcount: %u\n",
        index, current->dentry ? current->dentry->name : "NULL", current->inode,
        current->f_pos, current->f_flags, current->f_refcount);
    current = current->next;
    index++;
  }

  log_print("==================\n\n");
}
