/* $Id$ */
#ifndef NTP_KTIME_RTEMSDEP_H
#define NTP_KTIME_RTEMSDEP_H

#define TIMER_FREQ 10

#include <rtems.h>

static inline int splclock()
{
int rval;
	rtems_interrupt_disable(rval);
	return rval;
}

static inline void splx(int s)
{
	rtems_interrupt_enable(s);
}

#define cpu_number() (0)

#ifdef __PPC__
/* NOTE: definition of PCC_WIDTH in 'micro.c' must be <= 32 */
static inline unsigned rpcc()
{
unsigned rval;
	asm volatile("mftb %0":"=r"(rval));
	return rval;
}
#endif

#define splsched splclock
#define splextreme splclock

#endif
