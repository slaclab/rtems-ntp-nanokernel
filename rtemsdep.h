/* $Id$ */
#ifndef NTP_KTIME_RTEMSDEP_H
#define NTP_KTIME_RTEMSDEP_H

#define TIMER_FREQ 		10
#define RATE_DIVISOR	10

#include <rtems.h>

#define cpu_number() (0)

#define splsched() (0)
#define splextreme() (0)

#if defined(USE_PICTIMER) && !defined(__PPC__)
#error Configuration error -- cannot use PICTIMER on non-PowerPC arch
#endif

#endif
