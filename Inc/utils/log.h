#pragma once

#include <drivers/serial/serial.h>

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_WHITE "\x1b[37m"
#define ANSI_COLOR_PINK "\x1b[35m"
#define ANSI_COLOR_BRIGHT_RED "\x1b[91m"
#define ANSI_COLOR_BRIGHT_GREEN "\x1b[92m"
#define ANSI_COLOR_BRIGHT_YELLOW "\x1b[93m"
#define ANSI_COLOR_BRIGHT_BLUE "\x1b[94m"
#define ANSI_COLOR_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_COLOR_BRIGHT_CYAN "\x1b[96m"
#define ANSI_COLOR_BRIGHT_WHITE "\x1b[97m"
#define ANSI_BOLD "\x1b[1m"
#define ANSI_UNDERLINE "\x1b[4m"
#define ANSI_ITALIC "\x1b[3m"
#define ANSI_RESET "\x1b[0m"

#define MARK_AS_RED(val) ANSI_COLOR_RED val ANSI_RESET
#define MARK_AS_GREEN(val) ANSI_COLOR_GREEN val ANSI_RESET
#define MARK_AS_YELLOW(val) ANSI_COLOR_YELLOW val ANSI_RESET
#define MARK_AS_BLUE(val) ANSI_COLOR_BLUE val ANSI_RESET
#define MARK_AS_MAGENTA(val) ANSI_COLOR_MAGENTA val ANSI_RESET
#define MARK_AS_CYAN(val) ANSI_COLOR_CYAN val ANSI_RESET
#define MARK_AS_WHITE(val) ANSI_COLOR_WHITE val ANSI_RESET
#define MARK_AS_PINK(val) ANSI_COLOR_PINK val ANSI_RESET
#define MARK_AS_BRIGHT_RED(val) ANSI_COLOR_BRIGHT_RED val ANSI_RESET
#define MARK_AS_BRIGHT_GREEN(val) ANSI_COLOR_BRIGHT_GREEN val ANSI_RESET
#define MARK_AS_BRIGHT_YELLOW(val) ANSI_COLOR_BRIGHT_YELLOW val ANSI_RESET
#define MARK_AS_BRIGHT_BLUE(val) ANSI_COLOR_BRIGHT_BLUE val ANSI_RESET
#define MARK_AS_BRIGHT_MAGENTA(val) ANSI_COLOR_BRIGHT_MAGENTA val ANSI_RESET
#define MARK_AS_BRIGHT_CYAN(val) ANSI_COLOR_BRIGHT_CYAN val ANSI_RESET
#define MARK_AS_BRIGHT_WHITE(val) ANSI_COLOR_BRIGHT_WHITE val ANSI_RESET
#define MARK_AS_BOLD(val) ANSI_BOLD val ANSI_RESET
#define MARK_AS_UNDERLINE(val) ANSI_UNDERLINE val ANSI_RESET
#define MARK_AS_ITALIC(val) ANSI_ITALIC val ANSI_RESET

#define LOG_ERROR(fmt, ...)                                                    \
  serial_printf(ANSI_COLOR_RED ANSI_BOLD "[ERROR] " fmt ANSI_RESET "\r\n",     \
                ##__VA_ARGS__);

#define LOG_WARN(fmt, ...)                                                     \
  serial_printf(ANSI_COLOR_YELLOW ANSI_BOLD "[WARN] " fmt ANSI_RESET "\r\n",   \
                ##__VA_ARGS__);

#define LOG_DEBUG(fmt, ...)                                                    \
  serial_printf(ANSI_COLOR_CYAN ANSI_BOLD "[DEBUG] " fmt ANSI_RESET,           \
                ##__VA_ARGS__);

#define LOG_INFO(fmt, ...)                                                     \
  serial_printf(ANSI_COLOR_GREEN "[INFO] " fmt ANSI_RESET "\r\n",              \
                ##__VA_ARGS__);

#define LOG_NEWLINE() serial_printf("\r\n");

#define LOG_PRINT(fmt, ...) serial_printf(fmt, ##__VA_ARGS__);


#define log_info LOG_INFO
#define log_print LOG_PRINT
#define log_debug LOG_DEBUG
#define log_warn LOG_WARN
#define log_error LOG_ERROR
#define log_newline LOG_NEWLINE