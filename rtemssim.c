#include "kern.h"
#include "timex.h"
#include "pcc.h"

#include <assert.h>
#include <math.h>
#include <sys/time.h>

#define TICKS_PER_S 100

#define NS 1000000000

struct timespec real_time   = {0, 0};
struct timespec real_rate   = {0, NS/TICKS_PER_S};
unsigned        max_ticks   = 1000*TICKS_PER_S;
unsigned        disp_ticks  = TICKS_PER_S;
unsigned        poll_ticks  = TICKS_PER_S;

void
tsadd(struct timespec *res, struct timespec *ts1, struct timespec *ts2)
{
	res->tv_sec   = ts1->tv_sec  + ts2->tv_sec;
	res->tv_nsec  = ts1->tv_nsec + ts2->tv_nsec;
	while ( res->tv_nsec >= NS ) {
		res->tv_nsec -= NS;
		res->tv_sec++;
	}
	while ( res->tv_nsec < 0 ) {
		res->tv_nsec += NS;
		res->tv_sec--;
	}
}

void
tsdiff(struct timespec *res, struct timespec *ts1, struct timespec *ts2)
{
struct timespec tmp = *ts2;
	tmp.tv_sec  = - tmp.tv_sec;
	tmp.tv_nsec = - tmp.tv_nsec;
	tsadd(res, ts1, &tmp);
}

void
tsinc(struct timespec *ts1, struct timespec *ts2)
{
	tsadd(ts1, ts1, ts2);
}

long long tsdiff_ns(struct timespec *ts1, struct timespec *ts2)
{
long long res =  (long long)(unsigned long)ts1->tv_sec
               - (long long)(unsigned long)ts2->tv_sec;

          res *=  NS;
          res +=   (long long)(unsigned long)ts1->tv_nsec
                 - (long long)(unsigned long)ts2->tv_nsec;

	return res;
}

int
splclock()
{
	return 1;
}

int
splsched()
{
	return 0;
}

int
splextreme()
{
	return 0;
}

int
splx(int level)
{
	return 0;
}

int
cpu_number(void)
{
	return 0;
}

#define rtems_interrupt_disable(flags) do {flags=0;} while (0)
#define rtems_interrupt_enable(flags)  do {flags=0;} while (0)

#define _USED_FROM_SIMULATOR_
#include "rtemsdep.c"

static int
gd(const char *s, double *pd)
{
	if ( 1 != sscanf(s,"%lg",pd) ) {
		fprintf(stderr,"Not a valid 'double' number: %s\n", s);
		return 1;
	}
	return 0;
}

static void
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-ahSF] [-c time_const] [-d interval] [-o time_off], [-f freq_off] [-p poll_interval] [-j time_jitter] [-t time_end]\n", nm);
	fprintf(stderr,"       -h             : print this message\n");
	fprintf(stderr,"       -a             : Alternate output format (offset/freq only)\n");
	fprintf(stderr,"       -c             : PLL time constant (s)\n");
	fprintf(stderr,"       -d             : display time interval (s)\n");
	fprintf(stderr,"       -f freq_off    : Initial frequency offset (ppm)\n");
	fprintf(stderr,"       -F             : Use FLL mode (when possible)\n");
	fprintf(stderr,"       -j jitter_var  : Add gamma(2,1) distributed jitter when updating time (variance us)\n");
	fprintf(stderr,"       -o time_off    : Initial time offset (s)\n");
	fprintf(stderr,"       -p poll_intvl  : NTP poll/update interval (s)\n");
	fprintf(stderr,"       -t time_end    : Simulation end time (s)\n");
	fprintf(stderr,"       -S             : Use fixed seed\n");
}

int main(int argc, char **argv)
{
int          i;
struct timex ntv;

double       toff;
double       tmpd;
double       jitter_scale = 0.;
int          fixed_seed   = 0;
double       off_m1       = 0.;
double       off_m2       = 0.;
int          alt_fmt      = 0;

	ntv.offset   = 0;
	ntv.freq     = 0;
	ntv.status   = STA_PLL | STA_UNSYNC;
	ntv.constant = 0;
	ntv.modes    = MOD_STATUS | MOD_NANO | MOD_TIMECONST | MOD_OFFSET | MOD_FREQUENCY;

	hz           = TICKS_PER_S;

	while ( (i=getopt(argc, argv, "ahc:d:f:Fj:o:p:t:S")) > 0 ) {
		switch ( i ) {
			case 'h':
			default:
				if ( 'h' != i )
					fprintf(stderr,"Unknown option '%c'\n", i);
				usage(argv[0]);
				return 'h'==i ? 0 : 1;

			case 'a':
				alt_fmt = 1;
				break;

			case 'c':
				if ( gd(optarg, &tmpd) ) return 1;

				ntv.constant = secs2tcld(nearbyint(tmpd));

				fprintf(stderr,"Setting PLL time constant (LD) to %ld\n", ntv.constant);

				break;

			case 'd':
				if ( gd(optarg, &tmpd) ) return 1;

				disp_ticks = tmpd * TICKS_PER_S;

				break;

			case 'f':
				if ( gd(optarg, &tmpd) ) return 1;

				real_rate.tv_nsec *= 1.0 + tmpd/1.0E6;

				break;

			case 'F':
				ntv.status |= STA_FLL;
				break;

			case 'j':
				if ( gd(optarg, &jitter_scale) ) return 1;
			break;

			case 'o':
				if ( gd(optarg, &toff) ) return 1;

				TIMEVAR.tv_nsec = 1.0E9 * modf(toff, &tmpd);
				TIMEVAR.tv_sec  = tmpd;

				break;

			case 'p':
				if ( gd(optarg, &tmpd) ) return 1;

				poll_ticks = tmpd * hz;
 
				break;

			case 'S':
				fixed_seed = 1;
				break;

			case 't':
				if ( gd(optarg, &tmpd) ) return 1;
				max_ticks = tmpd * TICKS_PER_S;
				break;
		}
	}

	/* Properly scale jitter.
	 *
	 * X = -ln(U1) -ln(U2) = -ln(U1*U2)
	 *
	 * with U1, U2 uniformly and independent random variables in [0..1]
	 * yields a gamma-distributed random variable X | P(X) = Gamdist(2,1)
	 *
	 *    PDF(X) = x^(k-1) exp(-x/theta) / theta^k / Gamma(k)
	 *
	 * The first two moments of Gamdist(k,1) are
	 *   Mean(X) = k
	 *   Var(X)  = sqrt(k)
	 */

	/* Scale jitter so it has the desired variance when using
	 * X = -ln(U1*U2)
	 */
	jitter_scale *= 1000./sqrt(2.0); /* convert to ns */

	if ( ! fixed_seed ) {
		struct timeval tv;
		gettimeofday(&tv,0);
		srand48(tv.tv_sec ^ tv.tv_usec);
	} else {
		srand48(0xdeadbeef);
	}

	ntp_init();
	ntp_adjtime(&ntv);

	if ( !alt_fmt )
		printf("Tick  #:  Toff/us: Foff/ppm:   SysTime/s.ns:  RealTime/s.ns:\n");
	for ( i=0; i<max_ticks; i++ ) {
		long long off = tsdiff_ns(&real_time, &TIMEVAR);
		if ( i % disp_ticks == 0 ) {
			ntv.modes = 0;
			ntp_adjtime(&ntv);
			tmpd = (double)NS + (double)ntv.freq/(double)SCALE_PPM;
			tmpd/= (double)real_rate.tv_nsec * (double)TICKS_PER_S;
			printf("%8u %9lld %9.1lf",
                   i,
			       -off/1000,
			       (tmpd - 1.0)/1.E-6);
			if ( ! alt_fmt )
				printf(" %5ld.%09ld %5ld.%09ld",
                   TIMEVAR.tv_sec, TIMEVAR.tv_nsec,
                   real_time.tv_sec, real_time.tv_nsec);
			fputc('\n',stdout);
		}
		off_m1 += off;
		off_m2 += (double)off * (double)off;

		tsinc(&real_time, &real_rate);
		ticker_body();
		if ( i % poll_ticks == 0 ) {
			off = tsdiff_ns(&real_time, &TIMEVAR);

			if ( jitter_scale > 0. ) {
				double jitter = - log(drand48() * drand48());

				/* subtract mean and scale */
				off += jitter_scale * (jitter - 2.0);
			}
			hardupdate(&TIMEVAR,off);
		}
	}

	off_m1/=max_ticks;
	off_m2/=max_ticks;

	if ( !alt_fmt )
		printf("Mean offset: %lgus, variance %lgus\n", off_m1/1000., sqrt(off_m2-off_m1*off_m1)/1000.);

	return 0;
}
