#include <arch/x86_64/syscall.h>
#include <drivers/serial/serial.h>
#include <fs/devfs/devfs.h>
#include <process/thread.h>
#include <tty/tty.h>
#include <tty/tty_input.h>
#include <tty/tty_output.h>

// #define TTY_DEBUG

static int dev_console_write(struct devfs_device* dev, const void* buffer,
                             size_t length, size_t* off);

static int dev_console_read(struct devfs_device* dev, void* buffer,
                            size_t length, size_t* off);

static struct device_operations dev_console_fops = {
    .read = dev_console_read,
    .write = dev_console_write,
};

struct device_operations* device_get_dev_console_fops(void) {
  return &dev_console_fops;
}

void tty_init(void) {
  // Initialize the TTY
  tty_init_output();
  tty_init_input();

  // Start the keyboard event reading thread
  kthread_create("tty_kdb_event_read", NULL, tty_kdb_event_read_thread, NULL);

  // Clear the screen with black color
  tty_clear_screen();

  // Initialize the TTY input system
  tty_write("Welcome to the TTY!\n\n");
  tty_write("> ");  // Print a prompt
}

static int dev_console_write(struct devfs_device* dev, const void* buffer,
                             size_t length, size_t* off) {
  (void)dev;
  (void)off;

  if (length > 0 && !buffer) {
    return -1;
  }

  return tty_write_buffer(buffer, length);
}

static int dev_console_read(struct devfs_device* dev, void* buffer,
                            size_t length, size_t* off) {
  (void)dev;
  (void)off;

  return tty_read_input((char*)buffer, (size_t)length);
}
