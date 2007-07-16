/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 2001				       *
 *								       *
 * Permission to use, copy, modify, and distribute this software and   *
 * its documentation for any purpose and without fee is hereby	       *
 * granted, provided that the above copyright notice appears in all    *
 * copies and that both the copyright notice and this permission       *
 * notice appear in supporting documentation, and that the name	       *
 * University of Delaware not be used in advertising or publicity      *
 * pertaining to distribution of the software without specific,	       *
 * written prior permission. The University of Delaware makes no       *
 * representations about the suitability this software for any	       *
 * purpose. It is provided "as is" without express or implied	       *
 * warranty.							       *
 *								       *
 **********************************************************************/

#include "kern.h"
#include "pcc-host.h"

/*
 * Nanosecond time routines
 *
 * The following routines implement both a nanosecond and microsecond
 * system clock. They are intended as drop-in replacements for kernel
 * clock routines running on 64-bit architectures where a processor
 * cycle counter (PCC) is available. The implementation provides a true
 * nanosecond clock for single and multiple processor systems.
 *
 * When the kernel time is reckoned directly in nanoseconds (NTP_NANO
 * defined), the time at each tick interrupt is derived directly from
 * the kernel time variable. When the kernel time is reckoned in
 * microseconds, (NTP_NANO undefined), the time is derived from the
 * kernel time variable together with a variable representing the
 * leftover nanoseconds at the last tick interrupt. In either case, the
 * current nanosecond time is reckoned from these values plus an
 * interpolated value derived by the clock routines in another
 * architecture-specific module. The interpolation can use either a
 * dedicated counter or a processor cycle counter (PCC) implemented in
 * some architectures.
 *
 * The current nanosecond time is reckoned from the above values at the
 * most recent tick interrupt, plus for each individual processor its
 * PCC scaled by the number of nanoseconds per PCC cycle. The scale
 * factor is determined by counting the number of PCC cycles that occur
 * during one second for each processor. This requires an interprocessor
 * interrupt at intervals of about one second for each processor, in
 * order to establish the base values.
 *
 * The design requires no designated master processor and is robust
 * against individual fail-stop processor faults. The design allows
 * system clock frequency and tick interval to be changed while the
 * system is running. It also provides for the unlikely case where the
 * individual processor clock rates may be different. 
 *
 * Note that all routines must run at priority splclock or higher.
 */
/*
 * Multiprocessor definitions
 *
 * The TIME_READ() macro returns the current system time in a timespec
 * structure as an atomic action. The NCPUS define specifies the number
 * of processors in the system. The cpu_number() routine returns the
 * processor number executing the request. The rpcc() routine returns
 * the current PCC contents, where PCC_WIDTH is the number of signficant
 * bits.
 */
#define TIME_READ(t)	((t) = TIMEVAR) /* read microsecond clock */

/*
 * The following arrays are used to discipline the time in each
 * processor of a multiprocessor system to a nominal timescale based on
 * the tick inteval.
 */
struct timespec pcc_time[NCPUS]; /* time at last microset() call */
int64_t pcc_pcc[NCPUS];	/* PCC at last microset() */
int64_t pcc_numer[NCPUS];	/* change in time last interval */
int64_t pcc_denom[NCPUS];	/* change in PCC last interval */
long pcc_master[NCPUS];		/* master PCC at last microset() (ns) */
int microset_flag[NCPUS];	/* microset() initialization flag */
long master_pcc;		/* master PCC at interrupt (ns) */

/*
 * nano_time_rpcc() - read the system clock and PCC
 *
 * This routine reads the system clock and process cycle counter (PCC)
 * as an atomic operation. Note that in some architectures the PCC width
 * is less than the machine word, but in no case less than PCC_WIDTH
 * bits, and the high order bits may be junk.
 */
pcc_t
nano_time_rpcc(tsp)
	struct timespec *tsp;	/* nanosecond clock */
{
#ifdef NTP_NANO
	struct timespec t;	/* nanosecond clock */
#else
	struct timeval t;	/* microsecond clock */
#endif /* NTP_NANO */
	int64_t pcc;		/* process cycle counter */

	TIME_READ(t);		/* must be atomic */
	pcc = rpcc();
#ifdef NTP_NANO
	tsp->tv_sec = t.tv_sec;
	tsp->tv_nsec = t.tv_nsec;
#else
	tsp->tv_sec = t.tv_sec;
	tsp->tv_nsec = t.tv_usec * 1000 + time_nano;
#endif /* NTP_NANO */
	return (pcc & ((1LL << PCC_WIDTH) - 1));
}

/*
 * nano_time() - return the current nanosecond time
 *
 * The system microsecond and nanosecond clocks are updated at each tick
 * interrupt. When the kernel time variable counts in microseconds, the
 * nanosecond clock is assembled from the microsecond clock (timeval
 * time) and the leftover nanoseconds at the last tick interrupt
 * (nano_time). When it counts in nanoseconds (timespec time), the value
 * is used directly. In either case, the actual time is interpolated
 * between microset() calls using the PCC in each processor.
 *
 * Since the PCC's may not be syntonic with each other and the tick
 * interrupt clock, small differences between the reported times in the
 * order of a few nanoseconds are normal. For this reason and as the
 * result of clock phase adjustments, the apparent time may not always
 * be monotonic increasing; therefore, the reported time is adjusted to
 * be greater than the last previously reported time by at least one
 * nanosecond. In the next era when reading the clock takes less than a
 * nanosecond, we have a problem and may have to upgrade to a picosecond
 * clock.
 */
long
nano_time(tsp)
	struct timespec *tsp;
{
	struct timespec t, u;		/* nanosecond time */
	static struct timespec lasttime; /* last time returned */
	int64_t pcc, nsec, psec;	/* 64-bit temporaries */
	time_t sec;
	int i, s;

	i = cpu_number();		/* read the time on this CPU */
	s = splsched();
	pcc = nano_time_rpcc(&t);

	/*
	 * Determine the current clock time as the time at the last
	 * microset() call plus the normalized PCC accumulation since
	 * then. This time must fall between the time at the most recent
	 * tick interrupt to the time at one tick later. Note that the
	 * apparent accumulation can exceed one second, since the
	 * nanosecond time can roll over before the tick interrupt that
	 * rolls the second.
	 */
	if (microset_flag[i]) {
		psec = pcc - pcc_pcc[i];
		if (psec < 0)
			psec += 1LL << PCC_WIDTH;
		u = pcc_time[i];
		psec = psec * pcc_numer[i] / pcc_denom[i] -
		    (t.tv_sec - u.tv_sec) * NANOSECOND;
		nsec = u.tv_nsec + psec;
		if (nsec < t.tv_nsec)
			nsec = t.tv_nsec;
		else if (nsec > t.tv_nsec + time_tick)
			nsec = t.tv_nsec + time_tick;
		t.tv_nsec = (long)nsec;
		if (t.tv_nsec >= NANOSECOND) {
			t.tv_nsec -= NANOSECOND;
			t.tv_sec++;
		}
	} else {
		psec = 0;
	}

	/*
	 * Ordinarily, the current clock time is guaranteed to be later
	 * by at least one nanosecond than the last time the clock was
	 * read. However, this rule applies only if the current time is
	 * within one second of the last time. Otherwise, the clock will
	 * (shudder) be set backward. The clock adjustment daemon or
	 * human equivalent is presumed to be correctly implemented and
	 * to set the clock backward only upon unavoidable catastrophe.
	 */
	sec = lasttime.tv_sec - t.tv_sec;
	nsec = lasttime.tv_nsec - t.tv_nsec;
	if (nsec < 0) {
		nsec += NANOSECOND;
		sec--;
	}
	if (sec == 0) {
		t.tv_nsec += nsec + 1;
		if (t.tv_nsec >= NANOSECOND) {
			t.tv_nsec -= NANOSECOND;
			t.tv_sec++;
		}
	}
	*tsp = t;
	lasttime = *tsp;
	splx(s);
	psec += pcc_master[i];
	return ((long)psec);
}

/*
 * This routine implements the microsecond clock. It simply calls
 * nano_time() and tosses the nanos. Rounding is a charitable deduction.
 */
void
micro_time(tv)
	struct timeval *tv;		/* microsecond time */
{
	struct timespec ts;		/* nanosecond time */

	nano_time(&ts);			/* convert and round */
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = (ts.tv_nsec + 500) / 1000;
}

/*
 * This routine is called via interprocessor interrupt from the tick
 * interrupt routine for each processor separately. It updates the PCC
 * and time values for the processor relative to the kernel time at the
 * last tick interrupt. These values are used by nano_time() to
 * interpolate the nanoseconds since the last call of this routine. Note
 * that we assume the kernel variables have been zeroed early in life.
 */

void
microset()
{
struct timespec t;
int	            s;
pcc_t           pcc;
	s = splextreme();
	pcc = nano_time_rpcc(&t);
	splx(s);
	microset_from_saved(pcc, &t);
}

void
microset_from_saved(pcc_t saved_pcc, struct timespec *pt)
{
	struct timespec u;		/* nanosecond time */
	int64_t pcc, numer, denom;	/* 64-bit temporaries */
	int i;

	i = cpu_number();		/* read the time on this CPU */

	pcc = saved_pcc & ((1LL << PCC_WIDTH) - 1);

	/*
	 * Intialize for first reading. Use the processor rate from the
	 * system-dependent firmware.
	 */
	if (!microset_flag[i]) {
		microset_flag[i]++;
		pcc_pcc[i] = pcc;
		pcc_master[i] = master_pcc;
		pcc_time[i] = *pt;
		pcc_numer[i] = NANOSECOND;
		pcc_denom[i] = CPU_CLOCK; 
		return;
	}

	/*
	 * If the counter wraps or the clock lunges backwards, just
	 * ignore it. Things will get well on the next call.
	 */
	u = pcc_time[i];
	pcc_time[i] = *pt;
	numer = (pt->tv_sec - u.tv_sec) * NANOSECOND + pt->tv_nsec -
	    u.tv_nsec; 
	denom = pcc - pcc_pcc[i];
	pcc_pcc[i] = pcc;
	pcc_master[i] = master_pcc;
	if (denom < 0)
		denom += 1LL << PCC_WIDTH;
	if (denom <= 0 || numer <= 0)
		return;

	/*
	 * Save the numerator and denominator for later.
	 */
	pcc_numer[i] = numer;
	pcc_denom[i] = denom;
}
