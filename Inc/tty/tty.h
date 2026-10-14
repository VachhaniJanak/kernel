#pragma once

#include <stdint.h>

void tty_init(void);

struct device_operations* device_get_dev_console_fops(void);
