#pragma once

#define DISABLE_INT __asm__ volatile("cli");
#define ENABLE_INT __asm__ volatile("sti");