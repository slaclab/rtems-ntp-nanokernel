#include <rtems.h>
#include "pictimer.h"
#include <bsp.h>
#include "kern.h"
#include "timex.h"
#include <time.h>
#include <stdio.h>

#define NumberOf(arr) (sizeof(arr)/sizeof((arr)[0]))

#define SLOTS	1000000

#define PROF_TIMER 3

#undef PROFILE_IN_NS

#ifdef PROFILE_IN_NS
typedef struct timespec hist_t;
#else
typedef unsigned long long hist_t;
#endif

hist_t nanoclock_history[SLOTS];
volatile int slotno=0;

#ifndef PROFILE_IN_NS
extern unsigned long tsillticks;
extern unsigned long Clock_Decrementer_value;
#endif

unsigned long max_pcilat=0;
unsigned long max_nanolat=0;

static void
profile_isr(void *arg)
{
int	            timer_no = (int)arg;
unsigned long   clicks;
register hist_t *pts;
struct timespec dummy;
unsigned		flags;
unsigned		long tsill,dcr0,dcr,dcr1;

	if ( slotno >= NumberOf(nanoclock_history) || slotno<0 )
		return;
	pts = &nanoclock_history[slotno];
rtems_interrupt_disable(flags);
	asm volatile("mfdec %0;eieio":"=r"(dcr));
	clicks = in_le32( &OpenPIC->Global.Timer[timer_no].Current_Count );
	asm volatile("eieio; mfdec %0":"=r"(dcr0)); dcr0 = dcr - dcr0;
#ifdef PROFILE_IN_NS
	nano_time(pts);
#else
#if 1
	*pts = (unsigned long)nano_time(&dummy);
	asm volatile("eieio; mfdec %0":"=r"(dcr1)); dcr1 = dcr - dcr1;
#else
	tsill  = Clock_driver_ticks;
	dcr = Clock_Decrementer_value - dcr;
	*pts = dcr;
#define tsillticks tsill
#endif
#endif
rtems_interrupt_enable(flags);
	if ( dcr0 > max_pcilat )
		max_pcilat = dcr0;
	if ( dcr1 > max_nanolat )
		max_nanolat = dcr1;
	clicks = in_le32( &OpenPIC->Global.Timer[timer_no].Base_Count ) - clicks;
#ifdef PROFILE_IN_NS
	clicks*=240; /* ns/click @ 4.1666MHy */
	if ( pts->tv_nsec < clicks ) {
		pts->tv_nsec+=NANOSECOND;
		pts->tv_sec--;
	}
	pts->tv_nsec-=clicks;
#else
	clicks*=4;   /* decrementer clicks */
	*pts += (hist_t)Clock_Decrementer_value*(hist_t)tsillticks - clicks;
#endif
	slotno++;
}

int
pictimerProfileInstall()
{
	slotno=-1;
	if ( 0 == pictimerInstall(PROF_TIMER, 11, -83313, profile_isr) ) {
		pictimerEnable(PROF_TIMER, 1);
		return 0;
	}
	return -1;
}

int
pictimerProfileCleanup()
{
	return pictimerCleanup(PROF_TIMER);
}

int
dump_nanohistory(FILE *f, int max)
{
int i;
hist_t *pts;
	if ( !f )
		f = stdout;
	if ( max <=0 || max > slotno )
		max = slotno;
	for ( i=0, pts=nanoclock_history; i<max; i++,pts++ ) {
#ifdef PROFILE_IN_NS
#if 0
unsigned long long x;
		x = (pts->tv_sec - nanoclock_history[0].tv_sec) * NANOSECOND + pts->tv_nsec;
		fprintf(f,"%llu\n",x);	
#else
		fprintf(f,"%u %u\n",pts->tv_sec - nanoclock_history[0].tv_sec, pts->tv_nsec);
#endif
#else
		fprintf(f,"%llu\n",*pts);
#endif
	}
	return i;
}
