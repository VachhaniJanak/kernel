#pragma once

#if defined(__GNUC__)
#ifndef WEAK
#define WEAK __attribute__((weak))
#endif
#ifndef FORCE_INLINE
#define FORCE_INLINE __attribute__((__always_inline__))
#endif
#ifndef FORCE_NOINLINE
#define FORCE_NOINLINE __attribute__((noinline))
#endif
#ifndef INTERRUPT_HANDLER
#define INTERRUPT_HANDLER __attribute__((interrupt))
#endif
#else
#define WEAK
#define FORCE_INLINE
#define FORCE_NOINLINE
#define INTERRUPT_HANDLER
#endif

#ifndef __IO
#define __IO volatile
#endif

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
