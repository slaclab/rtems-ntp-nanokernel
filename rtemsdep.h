/* $Id$ */
#ifndef NTP_KTIME_RTEMSDEP_H
#define NTP_KTIME_RTEMSDEP_H

#define TIMER_FREQ 10

#include <rtems.h>

extern rtems_id rtems_ntp_mutex;

static inline int splclock()
{
	rtems_semaphore_obtain(rtems_ntp_mutex, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
	return 1;
}

static inline void splx(int s)
{
	if (s)
		rtems_semaphore_release(rtems_ntp_mutex);
}

#define cpu_number() (0)

#if defined(__PPC__)
#if defined(USE_MICRO)
/* NOTE: definition of PCC_WIDTH in 'micro.c' must be <= 32;
 *       perhaps, CPU_CLOCK should be properly defined also
 */
static inline long long rpcc()
{
long rval;
	asm volatile("mftb %0":"=r"(rval));
	return rval;
}

extern int  CLOCK_INTERRUPT_DISABLE();
extern void CLOCK_INTERRUPT_ENABLE(int);
#else

#include <bsp.h>

#define TIMER_NO	0	/* note that svgmWatchdog uses Timer #3 */

static inline int
CLOCK_INTERRUPT_DISABLE()
{
int rval = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority );
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, rval | OPENPIC_MASK );
	return rval;
}

static inline void
CLOCK_INTERRUPT_ENABLE(int restore)
{
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, restore );
}

#endif

#endif

#ifdef USE_MICRO
/* 'microset()' is always called from the clock tick handler
 * (i.e., at ISR level) - hence no extra protection should be necessary.
 *  --> splextreme() splx() should do NOTHING
 */
#define splextreme() (0)	/* splx MUST NOT unlock (unheld) mutex */
/* 'nano_time()' should always be interrupt protected
 * --> splsched()/splx() must lock/unlock the clock interrupt level
 */
#define splsched() (0)
#endif

#endif
