/* $Id$ */
#ifndef NTP_KTIME_RTEMSDEP_H
#define NTP_KTIME_RTEMSDEP_H

#ifdef USE_PICTIMER
#define TIMER_NO  		0	/* note that svgmWatchdog uses T3 */
#define TIMER_FREQ 		50
#else
#define RATE_DIVISOR	1
#endif

#include <rtems.h>

#define cpu_number() (0)

#define splsched() (0)
#define splextreme() (0)

#if defined(USE_PICTIMER) && !defined(__PPC__)
#error Configuration error -- cannot use PICTIMER on non-PowerPC arch
#endif

#endif
