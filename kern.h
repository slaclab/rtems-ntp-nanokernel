/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1998-2001			       *
 *								       *
 * Permission to use, copy, modify, and distribute this software and   *
 * its documentation for any purpose and without fee is hereby	       *
 * granted, provided that the above copyright notice appears in all    *
 * copies and that both the copyright notice and this permission       *
 * notice appear in supporting documentation, and that the name        *
 * University of Delaware not be used in advertising or publicity      *
 * pertaining to distribution of the software without specific,	       *
 * written prior permission. The University of Delaware makes no       *
 * representations about the suitability this software for any	       *
 * purpose. It is provided "as is" without express or implied	       *
 * warranty.							       *
 *								       *
 **********************************************************************/
/*
 * Global definitions. These control the various package configuraitons.
 */

/*
 * Avoid collisions with kernel and library names
 */
#ifdef KERNEL
#define TIMEVAR	time
#else
#define TIMEVAR	timevar
#endif

/*
 * The HZ define specifies the tick interupt frequency, which is
 * normally defined by the hz kernel variable. The value can range from
 * 1 to at least 5000.
 */
#define HZ		100	/* default tick interrupt frequency */

/*
 * Kernel header files which should already be in /usr/include/sys.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/errno.h>
#include <math.h>
#include <sys/time.h>

/*
 * Needed for gcc on SunOS4
 */
#ifdef SYS_SUNOS4
extern char *optarg;
typedef long long int64_t;
typedef struct timespec {	/* definition per POSIX.4 */
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* and nanoseconds */
} timespec_t;
#endif /* SUNOS */

/*
 * If NTP_NANO is defined, the package is configured for a nanosecond
 * clock, where the kernel clock (timespec time) runs in seconds and
 * nanoseconds. If undefined, the package is configured for a
 * microsecond clock, where the kernel clock (timeval time) runs in
 * seconds and microseconds. In either case, the actual system time
 * delivered to the user is in seconds and nanoseconds in either a
 * timeval or a timespec structure.
 */
/* #define NTP_NANO */		/* kernel nanosecond clock */
/* T.Straumann: we define NTP_NANO in 'timex.h' because that's what applications see */

/*
 * If NTP_L64 is defined, the package is configured for a 64-bit
 * architecture. This works only with 64-bit machines like the Alpha and
 * UltraSPARC. If undefined, the package is configured for a 32-bit
 * architecture. This works with both 32-bit and 64-bit architectures,
 * but is somewhat slower than the 64-bit version.
 */
#define NTP_L64 			/* 64-bit architecture */

#ifdef __m68k__
/* T.S. 68k gcc somehow doesn't grok some long long operation */
#undef NTP_L64
#endif

/*
 * Package header files which should be copied to /usr/include/sys.
 */
#include "timex.h"		/* API and kernel interface */
#include "l_fp.h"		/* double precision arithmetic */

/*
 * Miscellaneous defines. The EXT_CLOCK and PPS_SYNC establish whether
 * an external clock and/or pulse-per-second (PPS) signal is present,
 * respectively. The _KERNEL define is necessary to avoid conflicts with
 * the current Unix header file conventions. The ROOT macro establishes
 * root priviledge for the ntp_adjtime() syscall. The CPU_CLOCK defines
 * the default CPU clock speed. The tick interrupts always go to the
 * MASTER_CPU; if this is zero, the PPS interrupts go to the same
 * processor as well.
 */
#if 0
#define EXT_CLOCK		/* external clock option */
#define PPS_SYNC		/* PPS discipline option */
#endif
#define _KERNEL			/* supppress /usr/include/time.h */
#define ROOT		0	/* 0 = superuser, 1 = other */
#define CPU_CLOCK	433000000 /* default CPU clock speed (Hz) */
#define NCPUS		1	/* number of SMP processors */
#define MASTER_CPU	0	/* where the tick interrupts go */

/*
 * Function declarations
 */
#ifdef NTP_NANO
extern void ntp_tick_adjust(struct timespec *, int);
extern void hardupdate(struct timespec *, long);
extern void hardclock(struct timespec *, long);
extern void second_overflow(struct timespec *);
#else
extern void ntp_tick_adjust(struct timeval *, int);
extern void hardupdate(struct timeval *, long);
extern void hardclock(struct timeval *, long);
extern void second_overflow(struct timeval *);
#endif /* NTP_NANO */

extern double gauss(double);
extern void ntp_init(void);
extern void hardpps(struct timespec *, long);
extern long nano_time(struct timespec *);
extern void microset(void);

/* allow for running in task driven mode: 
 * ISR calls rpcc() and notifies the task
 *     passing the return value;
 * TASK calls microset_from_saved(pcc, &TIMEVAR) using the
 *     value saved by the ISR. The call must be issued PRIOR
 *     to updating TIMEVAR (ntp_tick_adjust).
 */

extern int cpu_number(void);


extern int ntp_adjtime(struct timex *);

/*
 * The following variables and functions are defined in the Unix kernel.
 */
#ifdef NTP_NANO
extern struct timespec TIMEVAR;	/* kernel nanosecond clock */
#else
extern struct timeval TIMEVAR;	/* kernel microsecond clock */
#endif /* NTP_NANO */
extern int hz;			/* tick interrupt frequency (Hz) */
extern int splextreme(), splsched(), splclock(), splx();
#ifdef __rtems__
#include "rtemsdep.h"
#endif

/*
 * The following variables are defined in the nanokernel code.
 */
extern long time_tick;		/* nanoseconds per tick (ns) */
extern long master_pcc;		/* master PCC at interrupt */
extern int master_cpu;		/* master CPU */
extern int microset_flag[NCPUS]; /* microset() initialization filag */
#if !defined(NTP_NANO)
extern long time_nano;		/* nanoseconds at last tick */
#endif /* NTP_NANO */
