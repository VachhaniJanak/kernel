#include <input/keyboard/keyboard.h>
#include <input/keyboard/keycode.h>
#include <libs/string.h>
#include <platform/attributes.h>
#include <process/locks.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <process/waitqueue.h>
#include <stdint.h>
#include <tty/tty_input.h>
#include <tty/tty_output.h>
#include <utils/log.h>

#define LINE_BUFFER_SIZE 512

static char line_buffer[LINE_BUFFER_SIZE];
static uint32_t line_buffer_index = 0;
static bool is_reading;
static uint32_t read_count;
static waitqueue_t waitqueue;
static mutex_t tty_lock;

void tty_init_input(void) {
  memset(line_buffer, 0, LINE_BUFFER_SIZE);

  line_buffer_index = 0;
  line_buffer[0] = '\0';
  is_reading = false;
  read_count = 0;

  waitqueue_init(&waitqueue);
  mutex_init(&tty_lock);
}

static inline void tty_process_keyboard_event(kbd_key_event_t event) {
  // Ignore Key Releases for standard text input
  if (!event.is_pressed) {
    return;
  }

  // Handle System Commands (e.g., Ctrl+C, Ctrl+L)
  if (event.modifiers & KBD_MODIFIER_LCTRL ||
      event.modifiers & KBD_MODIFIER_RCTRL) {
    if (event.unicode == 'c' || event.unicode == 'C') {
      // Send SIGINT to the running foreground process
      tty_write("^C\n");
      line_buffer_index = 0; // Clear the buffer
      return;
    }

    if (event.unicode == 'l' || event.unicode == 'L') {
      tty_clear_screen();
      return;
    }
  }

  // Handle Backspace
  if (event.keycode == KEY_BACKSPACE) {
    if (line_buffer_index > 0) {
      line_buffer_index--; // Delete the character from the line buffer

      // Visually erase the character on the screen:
      tty_putchar('\b');
      tty_putchar(' ');
      tty_putchar('\b');
    }
    return;
  }

  // Handle Enter Key
  if (event.keycode == KEY_ENTER) {
    tty_putchar('\n');

    // Terminate the string
    line_buffer[line_buffer_index++] = '\n';
    line_buffer[line_buffer_index] = '\0';
    read_count = line_buffer_index;
    line_buffer_index = 0;

    // WAKE UP THE SHELL!
    if (waitqueue_wake(&waitqueue)) {
      is_reading = true;
    }
    return;
  }

  // Handle Standard Printable Characters
  if (event.unicode != 0) {
    // Prevent buffer overflow
    if (line_buffer_index < LINE_BUFFER_SIZE - 2 && !is_reading) {
      line_buffer[line_buffer_index++] = (char)event.unicode;

      // Echo it to the screen so the user sees what they typed!
      tty_putchar((char)event.unicode);
    }
  }
}

void tty_kdb_event_read_thread(void *arg) {
  UNUSED(arg);

  kbd_key_event_t events = {0};

  kbd_aquire();

  while (true) {
    if (kbd_dequeue_key_event(&events)) {
      tty_process_keyboard_event(events);
    }
  }

  kbd_release();
}

int tty_read_input(char *buffer, size_t length) {
  if (!buffer || length == 0) {
    return -1;
  }

  mutex_acquire(&tty_lock);

  if (read_count == 0) {
    waitqueue_wait(&waitqueue);
  }

  size_t max_length = length - 1;

  if (read_count < max_length) {
    max_length = read_count;
  }

  memcpy(buffer, line_buffer, max_length);
  buffer[max_length] = '\0';

  read_count = 0;
  is_reading = false;

  mutex_release(&tty_lock);
  return (int)max_length;
}
