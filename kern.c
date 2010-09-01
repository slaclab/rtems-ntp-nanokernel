/***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1993-1998			       *
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
 * purpose. It is provided "as is" without express or implied          *
 * warranty.							       *
 *								       *
 **********************************************************************/

#include "kern.h"

#define MAXLONG 4.2949673e9		/* biggest long */
#define NSTAGE 32			/* max delay stages */

/*
 * This program simulates a hybrid phase/frequency-lock clock discipline
 * loop using actual code segments from modified kernel distributions
 * for SunOS, Solaris, Ultrix, OSF/1, HP-UX and FreeBSD kernels. These
 * segments involve no licensed code. The program runs on Unix systems,
 * when compiled with either cc or gcc, and on IBM compatibles, when
 * compiled with Microsoft C.
 */
/*
 * Phase/frequency-lock loop (PLL/FLL) definitions
 */
extern int time_status;		/* clock status bits */
extern long time_tick;		/* nanoseconds per tick (ns) */
extern l_fp time_offset;	/* time offset (ns) */
extern l_fp time_freq;		/* frequency offset (ns/s) */
extern l_fp time_adj;		/* tick adjust (ns/s) */

#ifdef EXT_CLOCK
/*
 * External clock definitions
 *
 * The following definitions and declarations are used only if an
 * external clock is configured on the system.
 */
#define CLOCK_INTERVAL 30	/* CPU clock update interval (s) */

/*
 * The clock_count variable is set to CLOCK_INTERVAL at each PPS
 * interrupt and decremented once each second.
 */
extern int clock_count;		/* CPU clock counter */

#ifdef HIGHBALL
/*
 * The clock_offset and clock_cpu variables are used by the HIGHBALL
 * interface. The clock_offset variable defines the offset between
 * system time and the HIGBALL counters. The clock_cpu variable contains
 * the offset between the system clock and the HIGHBALL clock for use in
 * disciplining the kernel time variable.
 */
extern struct timeval clock_offset; /* Highball clock offset */
long clock_cpu;	    		 /* CPU clock adjust */
#endif /* HIGHBALL */
#endif /* EXT_CLOCK */
/*
 * End of phase/frequency-lock loop (PLL/FLL) definitions
 */
/*
 * Function declarations
 */
static double churn(double);
static void chime();
static void display();
static void trace();

/*
 * The following variables and functions are defined elsewhere in the
 * kernel.
 */
#ifdef NTP_NANO
struct timespec TIMEVAR;	/* kernel nanosecond clock */
#else
struct timeval TIMEVAR;		/* kernel microsecond clock */
#endif /* NTP_NANO */
int hz = HZ;			/* tick interrupt frequency (Hz) */
int master_cpu = MASTER_CPU;	/* current master CPU number */

/*
 * Simulator variables
 */
static double time_real = 0.;	/* real time (s) */
static double time_read = 0.;	/* kernel time (s) */
static double time_tack = 0.;	/* next tick interrupt time */
static double time_pps = 0.;		/* next PPS interrupt time (s) */
static double sim_phase = 0.;	/* phase offset */
static double delay[NSTAGE]={0.};	/* delay shift register */
static double sim_freq = 0.;		/* frequency offset */
static double sim_begin = -1;	/* begin simulation time (s) */
static double sim_end = 1000;	/* end simulation time (s) */
static double walk=0.;		/* random-walk frequency parameter */
static long poll_interval=0;	/* poll counter */
static int priority=0;		/* CPU priority (just for looks) */
static int poll = 1;		/* poll interval (s) */
static int delptr=0;		/* delay register pointer */
static int delmax=0;		/* delay max */
static int debug=0;		/* der go derbug */
static struct timex ntv = { 0};	/* for ntp_adjtime() */
static long nsec = 0;		/* nanoseconds of the second */
static int cpu_intr = 0;		/* current processor number */
static int fixcnt = 0;		/* tick counter */
static long cpu_clock[8] = {433000000L, 432980000L, 432990000L,
    433000000L, 433010000L, 433020000L, 233000000L, 333000000L};
static long long cycles[NCPUS];	/* PCCs in each processor */
static FILE *fp = 0;		/* file pointer */
static int fmtsw = 0;		/* output format switch */

/*
 * This is the current system time
 */
static long nsec;		/* nanoseconds of the second */
static struct timespec times;	/* current nanosecond time */

/*
 * Simulation test program
 *
 * This program is designed to test the code segments actually used in
 * the kernel modifications for SunOS 4, Ultrix 4 and OSF/1. It includes
 * segments which support the PPS signal and external clocks (e.g.,
 * KSI/Odetics TPRO IRIG-B interface).
 */
int
main(
	int argc,		/* number of command-line arguments */
	char **argcv		/* vector of command-line argument */
	)
{
	double delta;		/* time correction (ns) */
	double dtemp, etemp, ftemp;
	int temp, i;

	/*
	 * Initialize and decode command line switches.
	 */
#ifdef NTP_NANO
	TIMEVAR.tv_sec = TIMEVAR.tv_nsec = 0;
#else
	TIMEVAR.tv_sec = TIMEVAR.tv_usec = 0;
#endif /* NTP_NANO */
	for (i = 0; i < NCPUS; i++)
		cycles[i] = random();
	ntv.offset = 0;
	ntv.freq = 0;
	ntv.status = STA_PLL;
	ntv.constant = 0;
	ntv.modes = MOD_STATUS | MOD_NANO;
	while ((temp = getopt(argc, argcv,
	    "ac:dD:f:F:l:m:p:P:r:s:t:w:z:")) != -1) {
		switch (temp) {

			/*
			 * -a use alternate output format
			 */
			case 'a':
			fmtsw = 1;
			continue;

			/*
			 * -c specify PPS mode and averaging time
			 */
			case 'c':
			sscanf(optarg, "%d", &ntv.shift);
			ntv.status |= STA_PPSFREQ | STA_PPSTIME;
			ntv.modes |= MOD_PPSMAX;
			continue;

			/*
			 * -d specify debug mode
			 */
			case 'd':
			debug++;
			continue;

			/*
			 * -D specify delay stages
			 */
			case 'D':
			sscanf(optarg, "%d", &delmax);
			continue;

			/*
			 * -f  specify frequency (PPM)
			 */
			case 'f':
			sscanf(optarg, "%lf", &dtemp);
			sim_freq = dtemp * 1e-6;
			continue;

			/*
			 * -F specify input file name
			 */
			case 'F':
			if ((fp = fopen(optarg, "r")) == NULL) {
				printf("*** file not found\n");
				exit(-1);
			}
			continue;

			/*
			 * -l specify FLL and poll interval (s)
			 */
			case 'l':
			sscanf(optarg, "%d", &poll);
			ntv.status |= STA_FLL;
			continue;

			/*
			 * -m specify beginning simulation time (s)
			 */
			case 'm':
			sscanf(optarg, "%lf", &sim_begin);
			continue;

			/*
			 * -p specify phase (us)
			 */
			case 'p':
			sscanf(optarg, "%lf", &dtemp);
			sim_phase = dtemp * 1e-6;
			continue;

			/*
			 * -r specify random-walk frequency parameter
			 * (ns/s/s)
			 */
			case 'r':
			sscanf(optarg, "%lf", &walk);
			continue;

			/*
			 * -s specify ending simulation time (s)
			 */
			case 's':
			sscanf(optarg, "%lf", &sim_end);
			continue;

			/*
			 * -t specify time constant
			 * (shift)
			 */
			case 't':
			sscanf(optarg, "%ld", &ntv.constant);
			poll = 1<<ntv.constant;
			ntv.modes |= MOD_TIMECONST;
			continue;

			/*
			 * -P specify poll interval
			 */
			case 'P':
				sscanf(optarg,"%d",&poll);
			continue;

			/*
			 * -w specify status word
			 */
			case 'w':
			sscanf(optarg, "%x", &ntv.status);
			ntv.modes |= MOD_STATUS;
			continue;

			/*
			 * -z specify clock frequency (Hz)
			 */
			case 'z':
			sscanf(optarg, "%d", &hz);
			continue;

			/*
			 * unknown command line switch
			 */
			default:
			printf("unknown switch %s\n", optarg);
			continue;
		}
	}
	ntp_init();
	temp = ntp_adjtime(&ntv);
	if (!fmtsw) {
		(void)printf(
		    "start %.0f s, stop %.0f s\n", sim_begin, sim_end);
		(void)printf(
		"state %d, status %04x, poll %d s, phase %.0f us, freq %.0f PPM\n",
		    temp, ntv.status, poll, sim_phase * 1e6, sim_freq *
		    1e6);
		(void)printf(
		    "hz = %d Hz, tick %ld ns\n", hz, time_tick);
		(void)printf(
		    "  time      offset     freq          _offset            _freq             _adj\n");
	}

	/*
	 * This should be recognized as a simple discrete event
	 * simulator with two entities, one corresponding to the tick
	 * interrupt and the other to the PPS interrupt. We are very
	 * careful to get the timescale correct here.
	 */
	delta = churn(0);
	if (delmax == 0) {
		hardupdate(&TIMEVAR, (long)delta);
	} else {
		hardupdate(&TIMEVAR, (long)delay[delptr]);
		delay[delptr] = delta;
		delptr = (delptr + 1) % delmax;
	}
	chime();
	display();
	time_tack = time_real + 1. / hz;
	time_pps = time_real + 1.;
	while (time_real < sim_end) {
		if (time_pps > time_tack) {

			/*
			 * This event is a tick interrupt.
			 */
			ntp_tick_adjust(&TIMEVAR, 0);
			delta = churn(time_tack);
/*
if (time_real >= sim_begin)
printf("%.9f %.9f %.9f %.9f %6ld %10ld\n", time_real,
time_tack, time_read, time_tack + 0 - time_read,
time.tv_sec, time.tv_nsec);
*/
#ifdef NTP_NANO
			if (TIMEVAR.tv_nsec >= NANOSECOND) {
#else
			if (TIMEVAR.tv_usec >= 1000000) {
#endif /* NTP_NANO */
				second_overflow(&TIMEVAR);
				if (master_cpu == 0)
					cpu_intr = 0;
				else
					cpu_intr = random() % NCPUS;
				chime();
				poll_interval++;
				poll_interval %= poll;
				if (poll_interval == 0) {
					if (delmax == 0) {
						hardupdate(&TIMEVAR,
						    (long)delta);
					} else {
						hardupdate(&TIMEVAR,
						     (long)delay[delptr]);
						delay[delptr] = delta;
						delptr = (delptr + 1) % delmax;
					}
					chime();
					/* display(); */
				}
				/* T.S: moved display from after chime to after brace */
				display();
				if (walk > 0)
					sim_freq += gauss(walk);
			} /*balance }*/
			cpu_intr = master_cpu;
			if (fixcnt < NCPUS) {
				cpu_intr = fixcnt;
				if (cpu_intr == master_cpu)
					master_pcc = 0;
				else
					master_pcc = nsec;
				microset();
			}
			fixcnt++;
			if (fixcnt >= hz)
				fixcnt = 0;
			time_tack = time_real + 1. / hz;
			trace("tic");
		} else {

			/*
			 * This event is a PPS interrupt.
			 */
			if (fp != NULL) {
				if (fscanf(fp, "%lf %lf %lf", &etemp,
				    &dtemp, &ftemp) != 3)
					exit(-2);
				sim_phase = dtemp * 1e-6;
			}
			delta = churn(time_pps);
			if (master_cpu == 0)
				cpu_intr = 0;
			else
				cpu_intr = random() % NCPUS;
#ifdef PPS_SYNC
			if (time_status & (STA_PPSFREQ | STA_PPSTIME)) {
				dtemp = -delta;
				while (dtemp < 0) {
					dtemp += NANOSECOND;
					times.tv_sec--;
				}
				while (dtemp >= NANOSECOND) {
					dtemp -= NANOSECOND;
					times.tv_sec++;
				}
				times.tv_nsec = (long)dtemp;
				dtemp = nsec - (sim_phase + sim_freq *
				    time_real) * 1e9;
				while (dtemp < 0)
					dtemp += NANOSECOND;
				while (dtemp >= NANOSECOND)
					dtemp -= NANOSECOND;
				hardpps(&times, (long)dtemp);
			}
#endif /* PPS_SYNC */
			time_pps = time_real + 1.;
			trace("PPS");
		}
	}
	return (0);
}

/*
 * churn - advance the system clock. Returns error in nanoseconds.
 */
double
churn(
	double new
	)
{
	int i;

	for (i = 0; i < NCPUS; i++)
		cycles[i] = cpu_clock[i] * new;
	chime();
	time_real = new;
	return ((new - time_read + sim_phase + sim_freq * time_real) *
	    1e9);
}

/*
 * chime - read the system clock
 */
void
chime()
{
	nsec = nano_time(&times);
	time_read = times.tv_sec + times.tv_nsec * 1e-9;
}

/*
 * trace - trace simulation steps
 */
void
trace(pfx)
	char *pfx;
{
	if (time_real < sim_begin)
		return;
	if (debug)
		printf(
		    "%s %.9f %12.9f %12.9f %6.3f %6.3f %2d %10ld %ld\n",
		    pfx, time_real, time_real - time_read + sim_phase,
		    sim_phase, (double)L_GINT(time_offset) / 1000,
    		    (double)L_GINT(time_freq) / 1000, cpu_intr, nsec,
		    poll_interval);
}

/*
 * display - trace updates. Note the occasional use of 32-bit masks to
 * preserve output formats when using 32-bit mode in 64-bit systems.
 */
void
display()
{
	if (time_real < sim_begin)
		return;
#ifdef NTP_L64
	if (fmtsw) {
		printf("%ld %.3f %.3f\n",
		    TIMEVAR.tv_sec,
     		    (double)L_GINT(time_offset) / 1000,
		    (double)L_GINT(time_freq) / 1000);
		return;
	}
	(void)printf(
	    "%6ld%12.3f%9.3f %016llx %016llx %016llx\n",
	    TIMEVAR.tv_sec,
	    (double)L_GINT(time_offset) / 1000,
	    (double)L_GINT(time_freq) / 1000,
	    time_offset, time_freq, time_adj);
#else
	(void)printf(
	    "%6ld%12.3f%9.3f %08lx%08lx %08lx%08lx %08lx%08lx\n",
	    TIMEVAR.tv_sec,
	    (double)L_GINT(time_offset) / 1000,
	    (double)L_GINT(time_freq) / 1000,
	    time_offset.l_i & 0xffffffff,
	    time_offset.l_uf & 0xffffffff,
	    time_freq.l_i & 0xffffffff,
	    time_freq.l_uf & 0xffffffff,
	    time_adj.l_i & 0xffffffff,
	    time_adj.l_uf & 0xffffffff);
#endif /* NTP_L64 */
}

/*
 * Miscellaneous leaves and twigs. These don't do anything except make
 * the simulator code closer to the real thing.
 */
int
cpu_number()
{
	return (cpu_intr);
}

long long
rpcc()
{
	return (cycles[cpu_intr]);
}

int
splextreme()			/* 7 */
{
	return (priority = 7);
}

int
splsched()			/* 5 */
{
	return (priority = 5);
}

int
splclock()			/* 5 */
{
	return (priority = 5);
}

int
splx(pri)			/* set priority */
	int pri;
{
	int s;

	s = priority;
	priority = pri;
	return (s);
}
