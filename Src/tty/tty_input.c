#include <input/keyboard/keyboard.h>
#include <input/keyboard/keycode.h>
#include <platform/attributes.h>
#include <process/locks.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <process/waitqueue.h>
#include <stdint.h>
#include <tty/tty_input.h>
#include <tty/tty_output.h>
#include <utils/log.h>
#include <utils/utils.h>

#define TTY_INPUT_DEBUG

#define LINE_BUFFER_SIZE 256
#define LINE_BUFFER_QUEUE_SIZE 1024

static char line_buffer[LINE_BUFFER_SIZE];
static uint32_t line_buffer_index;
static waitqueue_t line_buffer_waitqueue;

static char line_buffer_queue[LINE_BUFFER_QUEUE_SIZE];
static uint16_t head = 0;
static uint16_t tail = 0;
static mutex_t queue_lock;

static inline void queue_init(void) {
  head = 0;
  tail = 0;
}

static inline bool queue_is_empty(void) { return head == tail; }

static inline bool queue_is_full(void) {
  return ((tail + 1) % LINE_BUFFER_QUEUE_SIZE) == head;
}

static inline bool queue_enqueue(char c) {
  if (queue_is_full()) {
    return false;
  }

  line_buffer_queue[tail] = c;
  tail = (tail + 1) % LINE_BUFFER_QUEUE_SIZE;
  return true;
}

static inline bool queue_dequeue(char* c) {
  if (queue_is_empty()) {
    return false;
  }

  *c = line_buffer_queue[head];
  head = (head + 1) % LINE_BUFFER_QUEUE_SIZE;
  return true;
}

void tty_init_input(void) {
  line_buffer_index = 0;
  line_buffer[0] = '\0';

  queue_init();
  waitqueue_init(&line_buffer_waitqueue);
  mutex_init(&queue_lock);
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
      tty_write("> ");        // Print a prompt after the newline
      line_buffer_index = 0;  // Clear the buffer
      return;
    }

    if (event.unicode == 'l' || event.unicode == 'L') {
      tty_clear_screen();
      tty_write("> ");  // Print a prompt after clearing the screen
      return;
    }
  }

  // Handle Backspace
  if (event.keycode == KEY_BACKSPACE) {
    if (line_buffer_index > 0) {
      line_buffer_index--;  // Delete the character from the line buffer

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
    tty_write("> ");

    // Terminate the string
    line_buffer[line_buffer_index++] = '\n';
    line_buffer[line_buffer_index] = '\0';

#ifdef TTY_INPUT_DEBUG
    log_print("TTY Input: %s\n", line_buffer);
    log_print("Line Buffer Index: %u\n", line_buffer_index);
#endif

    for (uint32_t i = 0; i < line_buffer_index; i++) {
      if (!queue_enqueue(line_buffer[i])) {
        kthread_sleep(10);
      }
    }

    line_buffer_index = 0;

    // WAKE UP THE SHELL!
#ifdef TTY_INPUT_DEBUG
    log_print("Waking up the shell...\n");
#endif

    waitqueue_wake(&line_buffer_waitqueue);

#ifdef TTY_INPUT_DEBUG
    log_print("Shell woken up.\n");
#endif
    return;
  }

  // Handle Standard Printable Characters
  if (event.unicode != 0) {
    // Prevent buffer overflow
    if (line_buffer_index < LINE_BUFFER_SIZE - 2) {
      line_buffer[line_buffer_index++] = (char)event.unicode;

      // Echo it to the screen so the user sees what they typed!
      tty_putchar((char)event.unicode);
    }
  }
}

void tty_kdb_event_read_thread(void* arg) {
  UNUSED(arg);

  kbd_key_event_t events = {0};

  while (true) {
    if (kbd_dqueue_key_event(&events)) {
      tty_process_keyboard_event(events);
      continue;
    }

    waitqueue_wait(&kbd_waitqueue);
  }
}

int tty_read_input(char* buffer, size_t length) {
  if (buffer == NULL || length == 0) {
    return -1;
  }

#ifdef TTY_INPUT_DEBUG
  log_print("tty_read_input called with length: %zu\n", length);
#endif

  mutex_acquire(&queue_lock);

  while (queue_is_empty()) {
    waitqueue_wait(&line_buffer_waitqueue);
  }

#ifdef TTY_INPUT_DEBUG
  log_print("Queue is not empty, proceeding with input read.\n");
#endif

  int count = 0;
  char c;

  while (count < (int)length - 1) {
    if (queue_dequeue(&c)) {
      buffer[count++] = c;
      if (c == '\n') break;
    } else {
      break;
    }
  }

#ifdef TTY_INPUT_DEBUG
  log_print("Read %d characters from the queue.\n", count);
#endif

  mutex_release(&queue_lock);

#ifdef TTY_INPUT_DEBUG
  log_print("tty_read_input completed, returning count: %d\n", count);
#endif

  // Null-terminate the string
  buffer[count] = '\0';
  return count;
}
