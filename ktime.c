/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1993-2001			       *
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

/* RTEMS port by Till Straumann <strauman@slac.stanford.edu>, 2004 */

#include "kern.h"

/*
 * Generic NTP kernel interface
 *
 * These routines constitute the Network Time Protocol (NTP) interfaces
 * for user and daemon application programs. The ntp_gettime() routine
 * provides the time, maximum error (synch distance) and estimated error
 * (dispersion) to client user application programs. The ntp_adjtime()
 * routine is used by the NTP daemon to adjust the system clock to an
 * externally derived time. The time offset and related variables set by
 * this routine are used by other routines in this module to adjust the
 * phase and frequency of the clock discipline loop which controls the
 * system clock.
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
 * Note that all routines must run at priority splclock or higher.
 */
/*
 * Phase/frequency-lock loop (PLL/FLL) definitions
 *
 * The nanosecond clock discipline uses two variable types, time
 * variables and frequency variables. Both types are represented as 64-
 * bit fixed-point quantities with the decimal point between two 32-bit
 * halves. On a 32-bit machine, each half is represented as a single
 * word and mathematical operations are done using multiple-precision
 * arithmetic. On a 64-bit machine, ordinary computer arithmetic is
 * used.
 *
 * A time variable is a signed 64-bit fixed-point number in ns and
 * fraction. It represents the remaining time offset to be amortized
 * over succeeding tick interrupts. The maximum time offset is about
 * 0.5 s and the resolution is about 2.3e-10 ns.
 *
 *			1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |s s s|			 ns				   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |			    fraction				   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A frequency variable is a signed 64-bit fixed-point number in ns/s
 * and fraction. It represents the ns and fraction to be added to the
 * kernel time variable at each second. The maximum frequency offset is
 * about +-500000 ns/s and the resolution is about 2.3e-10 ns/s.
 *
 *			1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |s s s s s s s s s s s s s|	          ns/s			   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |			    fraction				   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
/*
 * The following variables establish the state of the PLL/FLL and the
 * residual time and frequency offset of the local clock.
 */
#define SHIFT_PLL	4	/* PLL loop gain (shift) */
#define SHIFT_FLL	2	/* FLL loop gain (shift) */

int time_state = TIME_OK;	/* clock state */
int time_status = STA_UNSYNC;	/* clock status bits */
long time_tai;			/* TAI offset (s) */
long time_monitor;		/* last time offset scaled (ns) */
long time_constant;		/* poll interval (shift) (s) */
long time_precision = 1;	/* clock precision (ns) */
long time_maxerror = MAXPHASE / 1000; /* maximum error (us) */
long time_esterror = MAXPHASE / 1000; /* estimated error (us) */
long time_reftime;		/* time at last adjustment (s) */
long time_tick;			/* nanoseconds per tick (ns) */
#if !defined(NTP_NANO)
long time_nano;			/* nanoseconds past last tick */
#endif /* NTP_NANO */
l_fp time_offset;		/* time offset (ns) */
l_fp time_freq;			/* frequency offset (ns/s) */
l_fp time_adj;			/* tick adjust (ns/s) */
l_fp time_phase;		/* time phase (ns) */

#ifdef PPS_SYNC
/*
 * The following variables are used when a pulse-per-second (PPS) signal
 * is available and connected via a modem control lead. They establish
 * the engineering parameters of the clock discipline loop when
 * controlled by the PPS signal.
 */
#define PPS_FAVG	2	/* min freq avg interval (s) (shift) */
#define PPS_FAVGDEF	8	/* default freq avg int (s) (shift) */
#define PPS_FAVGMAX	15	/* max freq avg interval (s) (shift) */
#define PPS_PAVG	4	/* phase avg interval (s) (shift) */
#define PPS_VALID	120	/* PPS signal watchdog max (s) */
#define PPS_MAXWANDER	100000	/* max PPS wander (ns/s) */
#define PPS_POPCORN	2	/* popcorn spike threshold (shift) */

struct timespec pps_tf[3];	/* phase median filter */
l_fp pps_freq;			/* scaled frequency offset (ns/s) */
long pps_lastfreq;		/* last scaled freq offset (ns/s) */
long pps_fcount;		/* frequency accumulator */
long pps_jitter;		/* nominal jitter (ns) */
long pps_stabil;		/* nominal stability (scaled ns/s) */
long pps_lastcount;		/* last counter offset */
long pps_lastsec;		/* time at last calibration (s) */
int pps_valid;			/* signal watchdog counter */
int pps_shift = PPS_FAVG;	/* interval duration (s) (shift) */
int pps_shiftmax = PPS_FAVGDEF;	/* max interval duration (s) (shift) */
int pps_intcnt;			/* wander counter */

/*
 * PPS signal quality monitors
 */
long pps_calcnt;		/* calibration intervals */
long pps_jitcnt;		/* jitter limit exceeded */
long pps_stbcnt;		/* stability limit exceeded */
long pps_errcnt;		/* calibration errors */
#endif /* PPS_SYNC */
/*
 * End of phase/frequency-lock loop (PLL/FLL) definitions
 */

void hardupdate();

/*
 * ntp_gettime() - NTP user application interface
 *
 * See the timex.h header file for synopsis and API description. Note
 * that the TAI offset is returned in the ntvtimeval.tai structure
 * member. 
 */
int
ntp_gettime(tp)
	struct ntptimeval *tp;	/* pointer to argument structure */
{
	struct ntptimeval ntv;	/* temporary structure */
#ifndef NTP_NANO
	struct timespec atv;	/* nanosecond time */
#endif
	int s;					/* caller priority */
	int st;					/* time_status cache */

	s = splclock();
	st = time_status;
#ifdef NTP_NANO
	nano_time(&ntv.time);
#else
	nano_time(&atv);
	if (!(st & STA_NANO))
		atv.tv_nsec /= 1000;
	ntv.time.tv_sec = atv.tv_sec;
	ntv.time.tv_usec = atv.tv_nsec;
#endif /* NTP_NANO */
	/* RTEMS INFO: the time, time_status, maxerror, time_tai and time_state
	 *             are *not* read atomically -- ISR might have updated any
	 *             of them since reading nano_time()
	 *             (which is supposed to read its argument atomically).
	 *             However - I don't care! I'm more concerned about
	 *             interrupt latency.
	 */
	ntv.maxerror = time_maxerror;
	ntv.esterror = time_esterror;
	ntv.tai = time_tai;
	splx(s);
	*tp = ntv;		/* copy out the result structure */

	/*
	 * Status word error decode. If any of these conditions occur,
	 * an error is returned, instead of the status word. Most
	 * applications will care only about the fact the system clock
	 * may not be trusted, not about the details.
	 *
	 * Hardware or software error
	 */
	if ((st & (STA_UNSYNC | STA_CLOCKERR)) ||

	/*
	 * PPS signal lost when either time or frequency synchronization
	 * requested
	 */
	    (st & (STA_PPSFREQ | STA_PPSTIME) &&
	    !(st & STA_PPSSIGNAL)) ||

	/*
	 * PPS jitter exceeded when time synchronization requested
	 */
	    (st & STA_PPSTIME &&
	    st & STA_PPSJITTER) ||

	/*
	 * PPS wander exceeded or calibration error when frequency
	 * synchronization requested
	 */
	    (st & STA_PPSFREQ &&
	    st & (STA_PPSWANDER | STA_PPSERROR)))
		return (TIME_ERROR);
	return (time_state);
}

/*
 * ntp_adjtime() - NTP daemon application interface
 *
 * See the timex.h header file for synopsis and API description. Note
 * that the timex.constant structure member has a dual purpose to set
 * the time constant and to set the TAI offset.
 */

/* RTEMS INFO: some global variables are used by ISR-level code as well
 *             as ntp_adjtime():
 *                                  ISR-code              ntp_adjtime
 *            	time_maxerror:         w                       w
 *              time_status:           w (PPSSIGNAL only)      w 
 *              time_state:            w                       w
 *              time_constant:         r                       w
 *              time_tai               w                       w
 *              time_freq              r                       w
 *
 *
 * In order to avoid long IRQ-disabled sections of code, we only
 * attempt to atomically modify individual fields - not the
 * entire set of data. This shouldn't disturb the algorithm
 * as most fields can be set individually by separate calls
 * to ntp_adjtime(). Rare cases when the user needs to atomically
 * update multiple fields should be handled by a special routine
 * (to be implemented by the user).
 * We assume that the hardware writes 32-bit variables atomically.
 */
int
ntp_adjtime(tp)
	struct timex *tp;	/* pointer to argument structure */
{
	struct timex ntv;	/* temporary structure */
	long freq;		/* frequency ns/s) */
	int modes;		/* mode bits from structure */
	int s;			/* caller priority */
	long	status_mask_on  = 0;
	long    status_mask_off = -1;
	long    flags;

	ntv = *tp;		/* copy in the argument structure */

	/*
	 * Update selected clock variables - only the superuser can
	 * change anything. Note that there is no error checking here on
	 * the assumption the superuser should know what it is doing.
	 * Note that either the time constant or TAI offset are loaded
	 * from the ntv.constant member, depending on the mode bits. If
	 * the STA_PLL bit in the status word is cleared, the state and
	 * status words are reset to the initial values at boot.
	 */
	modes = ntv.modes;

	if (modes & MOD_NANO)
		status_mask_on |= STA_NANO;

	if (modes & MOD_MICRO) {
		status_mask_off &= ~STA_NANO;
		status_mask_on  &= ~STA_NANO;
	}

	if (modes & MOD_CLKB)
		status_mask_on |= STA_CLK;
	if (modes & MOD_CLKA) {
		status_mask_on  &= ~STA_CLK;
		status_mask_off &= ~STA_CLK;
	}

	if (modes & MOD_STATUS) {
		status_mask_off &= STA_RONLY;
		status_mask_on  |= (ntv.status & ~STA_RONLY);
	} else {
		/* make sure STA_PLL is not clear as we test it a few lines down */
		ntv.status |= STA_PLL;
	}

	if (ROOT)
		return (EPERM);
	s = splclock();
	if (modes & MOD_MAXERROR)
		time_maxerror = ntv.maxerror;	/* RTEMS-RACE (updated by ISR in second_overflow) */
	if (modes & MOD_ESTERROR)
		time_esterror = ntv.esterror;	/* not used by anyone else; informative only */
	flags = CLOCK_INTERRUPT_DISABLE();
		if ( (time_status & STA_PLL) && !(ntv.status & STA_PLL) ) {
			time_state = TIME_OK;		/* set by IRQ-level code */
			time_status = STA_UNSYNC;	/* set by IRQ-level code  (MODE and PPS bits only [ all RO ]) */
#ifdef PPS_SYNC
			pps_shift = PPS_FAVG;
#endif /* PPS_SYNC */
		}
		time_status &= status_mask_off;
		time_status |= status_mask_on;
	CLOCK_INTERRUPT_ENABLE(flags);

	if (modes & MOD_TIMECONST) {
		if (ntv.constant < 0)
			time_constant = 0;			/* read by IRQ-level code */
		else if (ntv.constant > MAXTC)
			time_constant = MAXTC;
		else
			time_constant = ntv.constant;
	}
	if (modes & MOD_TAI) {
		if (ntv.constant > 0)
			time_tai = ntv.constant;	/* written by IRQ-level code; assume write is atomic */
	}
#ifdef PPS_SYNC
	if (modes & MOD_PPSMAX) {
		if (ntv.shift < PPS_FAVG)
			pps_shiftmax = PPS_FAVG;	/* read by IRQ-level code  */
		else if (ntv.shift > PPS_FAVGMAX)
			pps_shiftmax = PPS_FAVGMAX;
		else
			pps_shiftmax = ntv.shift;
	}
#endif /* PPS_SYNC */
	if (modes & MOD_OFFSET) {
		if (time_status & STA_NANO)
			hardupdate(&TIMEVAR, ntv.offset);	/* assume hardupdate is safe */
		else
			hardupdate(&TIMEVAR, ntv.offset * 1000);
	}
	if (modes & MOD_FREQUENCY) {
		freq = ntv.freq / SCALE_PPM;
		if (freq > MAXFREQ)
			freq = MAXFREQ;
		else if (freq < -MAXFREQ)
			freq = -MAXFREQ;
		flags = CLOCK_INTERRUPT_DISABLE();
		/* protect 64bit entity */
		L_LINT(time_freq, freq);
		CLOCK_INTERRUPT_ENABLE(flags);
#ifdef PPS_SYNC
		pps_freq = time_freq;					/* pps_freq (64-bit!) is not used by IRQ-level code */
#endif /* PPS_SYNC */
	}

	/*
	 * Retrieve all clock variables. Note that the TAI offset is
	 * returned only by ntp_gettime();
	 */
	if (time_status & STA_NANO)
		ntv.offset = time_monitor;
	else
		ntv.offset = time_monitor / 1000;
	/* IRQ-code doesn't write time_freq; reading should be safe */
	ntv.freq = L_GINT(time_freq) * SCALE_PPM;
	ntv.maxerror = time_maxerror;
	ntv.esterror = time_esterror;
	ntv.status = time_status;
	ntv.constant = time_constant;
	if (time_status & STA_NANO)
		ntv.precision = time_precision;
	else
		ntv.precision = time_precision / 1000;
	ntv.tolerance = MAXFREQ * SCALE_PPM;
#ifdef PPS_SYNC
	ntv.shift = pps_shift;
	ntv.ppsfreq = L_GINT(pps_freq) * SCALE_PPM;
	if (time_status & STA_NANO)
		ntv.jitter = pps_jitter;
	else
		ntv.jitter = pps_jitter / 1000;
	ntv.stabil = pps_stabil;
	ntv.calcnt = pps_calcnt;
	ntv.errcnt = pps_errcnt;
	ntv.jitcnt = pps_jitcnt;
	ntv.stbcnt = pps_stbcnt;
#endif /* PPS_SYNC */
	splx(s);
	*tp = ntv;		/* copy out the result structure */

	/*
	 * Status word error decode. See comments in
	 * ntp_gettime() routine.
	 */
	if ((time_status & (STA_UNSYNC | STA_CLOCKERR)) ||
	    (time_status & (STA_PPSFREQ | STA_PPSTIME) &&
	    !(time_status & STA_PPSSIGNAL)) ||
	    (time_status & STA_PPSTIME &&
	    time_status & STA_PPSJITTER) ||
	    (time_status & STA_PPSFREQ &&
	    time_status & (STA_PPSWANDER | STA_PPSERROR)))
		return (TIME_ERROR);
	return (time_state);
}

/*
 * ntp_tick_adjust() - called every tick for precision time adjustment
 *
 * This routine is ordinarily called from the tick interrupt routine
 * hardclock(). To minimize the jitter that might result when a higher
 * priority interrupt occurs after the tick interrupt is taken and
 * before the system clock is updated, this routine (and the following
 * routine second_overflow()) should be called early in the hardclock()
 * code path. 
 */

/* 
 * TSILLVARS
 *  time_update, time_phase, time_adj, time_nano
 */
void
ntp_tick_adjust(tvp, tick_update)
#ifdef NTP_NANO
	struct timespec *tvp;	/* pointer to nanosecond clock */
	int tick_update;	/* residual from adjtime() (ns) */
#else
	struct timeval *tvp;	/* pointer to microsecond clock */
	int tick_update;	/* residual from adjtime() (us) */
#endif /* NTP_NANO */
{
	long ltemp, time_update;

	/*
	 * Update the nanosecond and microsecond clocks. If the phase
	 * increment exceeds the tick period, update the clock phase.
	 */
#ifdef NTP_NANO
	time_update = tick_update;
	L_ADD(time_phase, time_adj);
	ltemp = L_GINT(time_phase) / hz;
	time_update += ltemp;
	L_ADDHI(time_phase, -ltemp * hz);
	tvp->tv_nsec += time_update;
#else
	time_update = tick_update;
	L_ADD(time_phase, time_adj);
	ltemp = L_GINT(time_phase) / (1000 * hz);
	time_update += ltemp;
	L_ADDHI(time_phase, -ltemp * (1000 * hz));
	tvp->tv_usec += time_update;
	time_nano = L_GINT(time_phase) / hz;
#endif /* NTP_NANO */
}

/*
 * second_overflow() - called after ntp_tick_adjust()
 *
 * This routine is ordinarily called immediately following the above
 * routine ntp_tick_adjust(). While these two routines are normally
 * combined, they are separated here only for the purposes of
 * simulation.
 */
/* TSILLVARS
 * time_maxerror, time_tai, time_state, time_status (read)
 * time_adj, time_offset, time_constant, time_freq
 * (PPS modifies time_status)
 */
void
second_overflow(tvp)
#ifdef NTP_NANO
	struct timespec *tvp;	/* pointer to nanosecond clock */
#else
	struct timeval *tvp;	/* pointer to microsecond clock */
#endif /* NTP_NANO */
{
	l_fp ftemp;		/* 32/64-bit temporary */

	/*
	 * On rollover of the second both the nanosecond and microsecond
	 * clocks are updated and the state machine cranked as
	 * necessary. The phase adjustment to be used for the next
	 * second is calculated and the maximum error is increased by
	 * the tolerance.
	 */
#ifdef NTP_NANO
	if (tvp->tv_nsec >= NANOSECOND) {
		tvp->tv_nsec -= NANOSECOND;
#else
	if (tvp->tv_usec >= 1000000) {
		tvp->tv_usec -= 1000000;
#endif /* NTP_NANO */
		tvp->tv_sec++;
		time_maxerror += MAXFREQ / 1000;

		/*
		 * Leap second processing. If in leap-insert state at
		 * the end of the day, the system clock is set back one
		 * second; if in leap-delete state, the system clock is
		 * set ahead one second. The nano_time() routine or
		 * external clock driver will insure that reported time
		 * is always monotonic.
		 */
		switch (time_state) {

			/*
			 * No warning.
			 */
			case TIME_OK:
			if (time_status & STA_INS)
				time_state = TIME_INS;
			else if (time_status & STA_DEL)
				time_state = TIME_DEL;
			break;

			/*
			 * Insert second 23:59:60 following second
			 * 23:59:59.
			 */
			case TIME_INS:
			if (!(time_status & STA_INS))
				time_state = TIME_OK;
			else if (tvp->tv_sec % 86400 == 0) {
				tvp->tv_sec--;
				time_state = TIME_OOP;
			}
			break;

			/*
			 * Delete second 23:59:59.
			 */
			case TIME_DEL:
			if (!(time_status & STA_DEL))
				time_state = TIME_OK;
			else if ((tvp->tv_sec + 1) % 86400 == 0) {
				tvp->tv_sec++;
				time_tai--;
				time_state = TIME_WAIT;
			}
			break;

			/*
			 * Insert second in progress.
			 */
			case TIME_OOP:
			time_tai++;
			time_state = TIME_WAIT;
			break;

			/*
			 * Wait for status bits to clear.
			 */
			case TIME_WAIT:
			if (!(time_status & (STA_INS | STA_DEL)))
				time_state = TIME_OK;
		}

		/*
		 * Compute the total time adjustment for the next second
		 * in ns. The offset is reduced by a factor depending on
		 * whether the PPS signal is operating. Note that the
		 * value is in effect scaled by the clock frequency,
		 * since the adjustment is added at each tick interrupt.
		 */
		ftemp = time_offset;
#ifdef PPS_SYNC
		if (time_status & STA_PPSTIME && time_status &
		    STA_PPSSIGNAL)
			L_RSHIFT(ftemp, pps_shift);
		else
			L_RSHIFT(ftemp, SHIFT_PLL + time_constant);
#else
		L_RSHIFT(ftemp, SHIFT_PLL + time_constant);
#endif /* PPS_SYNC */
		time_adj = ftemp;
		L_SUB(time_offset, ftemp);
		L_ADD(time_adj, time_freq);
		L_ADDHI(time_adj, NANOSECOND);
#ifdef PPS_SYNC
		if (pps_valid > 0)
			pps_valid--;
		else
			time_status &= ~STA_PPSSIGNAL;
#endif /* PPS_SYNC */
	}
}

/*
 * ntp_init() - initialize variables and structures
 *
 * This routine must be called after the kernel variables hz and tick
 * are set or changed and before the next tick interrupt. In this
 * particular implementation, these values are assumed set elsewhere in
 * the kernel. The design allows the clock frequency and tick interval
 * to be changed while the system is running. So, this routine should
 * probably be integrated with the code that does that.
 */
void
ntp_init()
{
	int i;

	/*
	 * The following variable must be initialized any time the
	 * kernel variable hz is changed.
	 */
	time_tick = NANOSECOND / hz;

	/*
	 * The following variables are initialized only at startup. Only
	 * those structures not cleared by the compiler need to be
	 * initialized, and these only in the simulator. In the actual
	 * kernel, any nonzero values here will quickly evaporate.
	 */
	L_CLR(time_offset);
	L_CLR(time_freq);
	L_LINT(time_adj, NANOSECOND);
	L_CLR(time_phase);
	for (i = 0; i < NCPUS; i++)
		microset_flag[i] = 0;
#ifdef PPS_SYNC
	pps_tf[0].tv_sec = pps_tf[0].tv_nsec = 0;
	pps_tf[1].tv_sec = pps_tf[1].tv_nsec = 0;
	pps_tf[2].tv_sec = pps_tf[2].tv_nsec = 0;
	pps_fcount = 0;
	L_CLR(pps_freq);
#endif /* PPS_SYNC */ 
}

/*
 * hardupdate() - local clock update
 *
 * This routine is called by ntp_adjtime() to update the local clock
 * phase and frequency. The implementation is of an adaptive-parameter,
 * hybrid phase/frequency-lock loop (PLL/FLL). The routine computes new
 * time and frequency offset estimates for each call. If the kernel PPS
 * discipline code is configured (PPS_SYNC), the PPS signal itself
 * determines the new time offset, instead of the calling argument.
 * Presumably, calls to ntp_adjtime() occur only when the caller
 * believes the local clock is valid within some bound (+-128 ms with
 * NTP). If the caller's time is far different than the PPS time, an
 * argument will ensue, and it's not clear who will lose.
 *
 * For uncompensated quartz crystal oscillators and nominal update
 * intervals less than 256 s, operation should be in phase-lock mode,
 * where the loop is disciplined to phase. For update intervals greater
 * than 1024 s, operation should be in frequency-lock mode, where the
 * loop is disciplined to frequency. Between 256 s and 1024 s, the mode
 * is selected by the STA_MODE status bit.
 */

/* TSILLVARS
 *
 * time_status:   PPS stuff FREQHOLD, FLL are read;  STA_MODE is modified
 *
 * time_monitor, time_offset, time_reftime, time_constant, time_freq, time_status
 */
void
hardupdate(tvp, offset)
#ifdef NTP_NANO
	struct timespec *tvp;	/* pointer to nanosecond clock */
#else
	struct timeval *tvp;	/* pointer to microsecond clock */
#endif /* NTP_NANO */
	long offset;		/* clock offset (ns) */
{
	long mtemp;
	l_fp ftemp, ftemp1;
	long flags;
	long reftime;
	int  st;

	/*
	 * Select how the phase is to be controlled and from which
	 * source. If the PPS signal is present and enabled to
	 * discipline the time, the PPS offset is used; otherwise, the
	 * argument offset is used.
	 */
	if (!(time_status & STA_PLL))
		return;

	if (offset > MAXPHASE)
		time_monitor = MAXPHASE;
	else if (offset < -MAXPHASE)
		time_monitor = -MAXPHASE;
	else
		time_monitor = offset;

	/* caution: time_offset and *tvp are shared with IRQ-level routines */
	flags = CLOCK_INTERRUPT_DISABLE();
	/* caching time_status is safe; none of the bits (other than PPSSIGNAL 
	 * --- even if PPSSIGNAL changes we should be ok as long as we work
	 * on the cached copy) is changed by interrupt-level routines 
	 * (adj_ticks/second_overflow)
	 */
	st = time_status;
	if ( ((st & (STA_PPSTIME | STA_PPSSIGNAL)) ^ (STA_PPSTIME | STA_PPSSIGNAL)) ) {
		L_LINT(time_offset, time_monitor);
	}
	reftime = tvp->tv_sec;
	CLOCK_INTERRUPT_ENABLE(flags);

	/*
	 * Select how the frequency is to be controlled and in which
	 * mode (PLL or FLL). If the PPS signal is present and enabled
	 * to discipline the frequency, the PPS frequency is used;
	 * otherwise, the argument offset is used to compute it.
	 */
	if ( (st & STA_PPSFREQ) && (st & STA_PPSSIGNAL) ) {
		time_reftime = reftime;
		return;
	}
	if ( (st & STA_FREQHOLD) || time_reftime == 0)
		time_reftime = reftime;
	mtemp = reftime - time_reftime;
	L_LINT(ftemp, time_monitor);
	L_RSHIFT(ftemp, (SHIFT_PLL + 2 + time_constant) << 1);
	L_MPY(ftemp, mtemp);
	/* unprotected read from time_freq is OK; not modified at ISR level */
	ftemp1 = time_freq;
	L_ADD(ftemp1, ftemp);
	st &= ~STA_MODE;
	if (mtemp >= MINSEC && (st & STA_FLL || mtemp >
	    MAXSEC)) {
		L_LINT(ftemp, (time_monitor << 4) / mtemp);
		L_RSHIFT(ftemp, SHIFT_FLL + 4);
		L_ADD(ftemp1, ftemp);
		st |= STA_MODE;
	}
	time_reftime = reftime;
	if (L_GINT(ftemp1) > MAXFREQ)
		L_LINT(ftemp1, MAXFREQ);
	else if (L_GINT(ftemp1) < -MAXFREQ)
		L_LINT(ftemp1, -MAXFREQ);
	flags = CLOCK_INTERRUPT_DISABLE();
	time_freq = ftemp1;
	if ( st & STA_MODE )
		time_status |= STA_MODE;
	else
		time_status &= STA_MODE;
	CLOCK_INTERRUPT_ENABLE(flags);
}

#ifdef PPS_SYNC
/*
 * hardpps() - discipline CPU clock oscillator to external PPS signal
 *
 * This routine is called at each PPS interrupt in order to discipline
 * the CPU clock oscillator to the PPS signal. There are two independent
 * first-order feedback loops, one for the phase, the other for the
 * frequency. The phase loop measures and grooms the PPS phase offset
 * and leaves it in a handy spot for the seconds overflow routine. The
 * frequency loop averages successive PPS phase differences and
 * calculates the PPS frequency offset, which is also processed by the
 * seconds overflow routine. The code requires the caller to capture the
 * time and architecture-dependent hardware counter values in
 * nanoseconds at the on-time PPS signal transition.
 *
 * Note that, on some Unix systems this routine runs at an interrupt
 * priority level higher than the timer interrupt routine hardclock().
 * Therefore, the variables used are distinct from the hardclock()
 * variables, except for the actual time and frequency variables, which
 * are determined by this routine and updated atomically.
 */
void
hardpps(tsp, nsec)
	struct timespec *tsp;	/* time at PPS */
	long nsec;		/* hardware counter at PPS */
{
	long u_sec, u_nsec, v_nsec; /* temps */
	l_fp ftemp;

	/*
	 * The signal is first processed by a range gate and frequency
	 * discriminator. The range gate rejects noise spikes outside
	 * the range +-500 us. The frequency discriminator rejects input
	 * signals with apparent frequency outside the range 1 +-500
	 * PPM. If two hits occur in the same second, we ignore the
	 * later hit; if not and a hit occurs outside the range gate,
	 * keep the later hit for later comparison, but do not process
	 * it.
	 */
	time_tick = NANOSECOND / hz;
	time_status |= STA_PPSSIGNAL | STA_PPSJITTER;
	time_status &= ~(STA_PPSWANDER | STA_PPSERROR);
	pps_valid = PPS_VALID;
	u_sec = tsp->tv_sec;
	u_nsec = tsp->tv_nsec;
	if (u_nsec >= (NANOSECOND >> 1)) {
		u_nsec -= NANOSECOND;
		u_sec++;
	}
	v_nsec = u_nsec - pps_tf[0].tv_nsec;
	if (u_sec == pps_tf[0].tv_sec && v_nsec < NANOSECOND -
	    MAXFREQ)
		return;
	pps_tf[2] = pps_tf[1];
	pps_tf[1] = pps_tf[0];
	pps_tf[0].tv_sec = u_sec;
	pps_tf[0].tv_nsec = u_nsec;

	/*
	 * Compute the difference between the current and previous
	 * counter values. If the difference exceeds 0.5 s, assume it
	 * has wrapped around, so correct 1.0 s. If the result exceeds
	 * the tick interval, the sample point has crossed a tick
	 * boundary during the last second, so correct the tick. Very
	 * intricate.
	 */
	u_nsec = nsec - pps_lastcount;
	pps_lastcount = nsec;
	if (u_nsec > (NANOSECOND >> 1))
		u_nsec -= NANOSECOND;
	else if (u_nsec < -(NANOSECOND >> 1))
		u_nsec += NANOSECOND;
	if (u_nsec > (time_tick >> 1))
		u_nsec -= time_tick;
	else if (u_nsec < -(time_tick >> 1))
		u_nsec += time_tick;
	pps_fcount += u_nsec;
	if (v_nsec > MAXFREQ || v_nsec < -MAXFREQ)
		return;
	time_status &= ~STA_PPSJITTER;

	/*
	 * A three-stage median filter is used to help denoise the PPS
	 * time. The median sample becomes the time offset estimate; the
	 * difference between the other two samples becomes the time
	 * dispersion (jitter) estimate.
	 */
	if (pps_tf[0].tv_nsec > pps_tf[1].tv_nsec) {
		if (pps_tf[1].tv_nsec > pps_tf[2].tv_nsec) {
			v_nsec = pps_tf[1].tv_nsec;	/* 0 1 2 */
			u_nsec = pps_tf[0].tv_nsec - pps_tf[2].tv_nsec;
		} else if (pps_tf[2].tv_nsec > pps_tf[0].tv_nsec) {
			v_nsec = pps_tf[0].tv_nsec;	/* 2 0 1 */
			u_nsec = pps_tf[2].tv_nsec - pps_tf[1].tv_nsec;
		} else {
			v_nsec = pps_tf[2].tv_nsec;	/* 0 2 1 */
			u_nsec = pps_tf[0].tv_nsec - pps_tf[1].tv_nsec;
		}
	} else {
		if (pps_tf[1].tv_nsec < pps_tf[2].tv_nsec) {
			v_nsec = pps_tf[1].tv_nsec;	/* 2 1 0 */
			u_nsec = pps_tf[2].tv_nsec - pps_tf[0].tv_nsec;
		} else if (pps_tf[2].tv_nsec < pps_tf[0].tv_nsec) {
			v_nsec = pps_tf[0].tv_nsec;	/* 1 0 2 */
			u_nsec = pps_tf[1].tv_nsec - pps_tf[2].tv_nsec;
		} else {
			v_nsec = pps_tf[2].tv_nsec;	/* 1 2 0 */
			u_nsec = pps_tf[1].tv_nsec - pps_tf[0].tv_nsec;
		}
	}

	/*
	 * Nominal jitter is due to PPS signal noise and interrupt
	 * latency. If it exceeds the popcorn threshold, the sample is
	 * discarded. otherwise, if so enabled, the time offset is
	 * updated. We can tolerate a modest loss of data here without
	 * much degrading time accuracy.
	 */
	if (u_nsec > (pps_jitter << PPS_POPCORN)) {
		time_status |= STA_PPSJITTER;
		pps_jitcnt++;
	} else if (time_status & STA_PPSTIME) {
		time_monitor = -v_nsec;
		L_LINT(time_offset, time_monitor);
	}
	pps_jitter += (u_nsec - pps_jitter) >> PPS_FAVG;
	u_sec = pps_tf[0].tv_sec - pps_lastsec;
	if (u_sec < (1 << pps_shift))
		return;

	/*
	 * At the end of the calibration interval the difference between
	 * the first and last counter values becomes the scaled
	 * frequency. It will later be divided by the length of the
	 * interval to determine the frequency update. If the frequency
	 * exceeds a sanity threshold, or if the actual calibration
	 * interval is not equal to the expected length, the data are
	 * discarded. We can tolerate a modest loss of data here without
	 * much degrading frequency accuracy.
	 */
	pps_calcnt++;
	v_nsec = -pps_fcount;
	pps_lastsec = pps_tf[0].tv_sec;
	pps_fcount = 0;
	u_nsec = MAXFREQ << pps_shift;
	if (v_nsec > u_nsec || v_nsec < -u_nsec || u_sec != (1 <<
	    pps_shift)) {
		time_status |= STA_PPSERROR;
		pps_errcnt++;
		return;
	}

	/*
	 * Here the raw frequency offset and wander (stability) is
	 * calculated. If the wander is less than the wander threshold
	 * for four consecutive averaging intervals, the interval is
	 * doubled; if it is greater than the threshold for four
	 * consecutive intervals, the interval is halved. The scaled
	 * frequency offset is converted to frequency offset. The
	 * stability metric is calculated as the average of recent
	 * frequency changes, but is used only for performance
	 * monitoring.
	 */
	L_LINT(ftemp, v_nsec);
	L_RSHIFT(ftemp, pps_shift);
	L_SUB(ftemp, pps_freq);
	u_nsec = L_GINT(ftemp);
	if (u_nsec > PPS_MAXWANDER) {
		L_LINT(ftemp, PPS_MAXWANDER);
		pps_intcnt--;
		time_status |= STA_PPSWANDER;
		pps_stbcnt++;
	} else if (u_nsec < -PPS_MAXWANDER) {
		L_LINT(ftemp, -PPS_MAXWANDER);
		pps_intcnt--;
		time_status |= STA_PPSWANDER;
		pps_stbcnt++;
	} else {
		pps_intcnt++;
	}
	if (pps_intcnt >= 4) {
		pps_intcnt = 4;
		if (pps_shift < pps_shiftmax) {
			pps_shift++;
			pps_intcnt = 0;
		}
	} else if (pps_intcnt <= -4) {
		pps_intcnt = -4;
		if (pps_shift > PPS_FAVG) {
			pps_shift--;
			pps_intcnt = 0;
		}
	}
	if (u_nsec < 0)
		u_nsec = -u_nsec;
	pps_stabil += (u_nsec * SCALE_PPM - pps_stabil) >> PPS_FAVG;

	/*
	 * The PPS frequency is recalculated and clamped to the maximum
	 * MAXFREQ. If enabled, the system clock frequency is updated as
	 * well.
	 */
	L_ADD(pps_freq, ftemp);
	u_nsec = L_GINT(pps_freq);
	if (u_nsec > MAXFREQ)
		L_LINT(pps_freq, MAXFREQ);
	else if (u_nsec < -MAXFREQ)
		L_LINT(pps_freq, -MAXFREQ);
	if (time_status & STA_PPSFREQ)
		time_freq = pps_freq;
}
#endif /* PPS_SYNC */
