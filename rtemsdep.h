/* $Id$ */
#ifndef NTP_KTIME_RTEMSDEP_H
#define NTP_KTIME_RTEMSDEP_H

#define TIMER_FREQ 		10
#define RATE_DIVISOR	10

#include <rtems.h>

#ifndef USE_DECREMENTER
extern rtems_id rtems_ntp_mqueue;
#endif


#define cpu_number() (0)

#define splsched() (0)
#define splextreme() (0)

#endif
