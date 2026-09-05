#pragma once

#include <input/keyboard/keyboard.h>

void tty_init_input(void);

void tty_kdb_event_read_thread(void* arg);

int tty_read_input(char* buffer, size_t buffer_size);
