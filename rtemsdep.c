#include <rtems.h>
#include <bsp.h>
#include <bsp/bspExt.h>
#include <bsp/irq.h>
#include <rtems/rtems_bsdnet.h>
#include <netinet/in.h>
#include <string.h>
#include <assert.h>

#include <cexp.h>

#include "kern.h"
#include "rtemsdep.h"
#include "timex.h"

#ifdef USE_PICTIMER
#include "pictimer.h"
#endif

#define DAEMON_SYNC_INTERVAL_SECS	60
#define KILL_DAEMON					RTEMS_EVENT_1

#define PPM_SCALE					(1<<16)
#define PPM_SCALED					((double)PPM_SCALE)

#ifdef NTP_NANO
struct timespec TIMEVAR;	/* kernel nanosecond clock */
#else
struct timeval TIMEVAR;		/* kernel microsecond clock */
#endif

int microset_flag[NCPUS];	/* microset() initialization filag */
int hz;

static rtems_id daemon_id = 0;
static rtems_id ticker_id = 0;
static rtems_id kill_sem;
static rtems_id mutex_id  = 0;
#ifndef USE_DECREMENTER
       rtems_id rtems_ntp_mqueue = 0;
#else
static volatile int tickerRunning = 0;
#endif

unsigned      rtems_ntp_debug = 0;
unsigned      rtems_ntp_daemon_sync_interval_secs = DAEMON_SYNC_INTERVAL_SECS;
FILE		  *rtems_ntp_debug_file;

int
splx(int level)
{
	if ( level )
		rtems_semaphore_release( mutex_id );
	return 0;
}

int
splclock()
{
	rtems_semaphore_obtain( mutex_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT );
	return 1;
}

/*
 * RTEMS base: 1988, January 1
 *  UNIX base: 1970, January 1
 *   NTP base: 1900, January 1
 */
#define UNIX_BASE_TO_NTP_BASE (((70UL*365UL)+17UL) * (24*60*60))

#define PARANOIA(something)	assert( RTEMS_SUCCESSFUL == (something) )

#define NTP_DEBUG

#ifdef NTP_DEBUG
long long rtems_ntp_max_t1;
long long rtems_ntp_max_t2;
long long rtems_ntp_max_t3;
long long rtems_ntp_max_diff = 0;

int
rtems_ntp_print_maxdiff(int reset)
{
time_t    t;
long long t4;

	printf("Max adjust %llins\n",rtems_ntp_max_diff * NANOSECOND);
	printf("    req. sent at (local time) %lu.%lu\n",
				frac2sec(rtems_ntp_max_t1),frac2nsec(rtems_ntp_max_t1));
	printf("    received at (remote time) %lu.%lu\n",
				frac2sec(rtems_ntp_max_t2),frac2nsec(rtems_ntp_max_t2));
	printf("    reply sent  (remote time) %lu.%lu\n",
				frac2sec(rtems_ntp_max_t3),frac2nsec(rtems_ntp_max_t3));
	t4 =  rtems_ntp_max_t3 - rtems_ntp_max_diff;

	if ( rtems_ntp_max_t1 && rtems_ntp_max_t2 )
		t4 +=  rtems_ntp_max_t2 - rtems_ntp_max_diff - rtems_ntp_max_t1;
	printf("    reply received (lcl time) %lu.%lu\n",
				frac2sec(t4),frac2nsec(t4));

	t = frac2sec(t4) - rtems_bsdnet_timeoffset - UNIX_BASE_TO_NTP_BASE;

	printf("Happened around %s\n", ctime(&t));
	if ( reset )
		rtems_ntp_max_diff = 0;
}
#endif

/* could use ntp_gettime() for this - avoid the overhead */
static inline void locked_nano_time(struct timespec *pt)
{
int s;
	s = splclock();
	nano_time(pt);
	splx(s);
}

static inline void locked_hardupdate(long nsecs)
{
int s;
	s = splclock();
	hardupdate(&TIMEVAR, nsecs );
	splx(s);
}

static int copyPacketCb(struct ntpPacketSmall *p, int state, void *usr_data)
{
	if ( 0 == state )
		memcpy(usr_data, p, sizeof(*p));
	return 0;
}

static inline long long nts2ll(struct timestamp *pt)
{
	return (((long long)ntohl(pt->integer))<<32) + (unsigned long)ntohl(pt->fraction);
}

static inline long long nsec2frac(unsigned long nsec)
{
	return (((long long)nsec)<<32)/NANOSECOND;
}

static inline long frac2nsec(long long f)
{
	return (long) ((((long long)NANOSECOND) * f) >> 32);
}

static inline long frac2sec(long long f)
{
long rval = f>>32;
	return rval < 0 ? rval + 1 : rval;
}

static int diffTimeCb(struct ntpPacketSmall *p, int state, void *usr_data)
{
struct timespec nowts;
long long       now, diff, org, rcv;
	
	if ( state >= 0 ) {
		locked_nano_time(&nowts);
		now = nsec2frac(nowts.tv_nsec);
		/* convert RTEMS to NTP seconds */
		nowts.tv_sec += rtems_bsdnet_timeoffset + UNIX_BASE_TO_NTP_BASE;
		if ( 1 == state ) {
			/* first pass; record our current time */
			p->originate_timestamp.integer  = htonl( nowts.tv_sec );
			p->originate_timestamp.fraction = htonl( (unsigned long)now ); 
		} else {
			now  += ((long long)nowts.tv_sec)<<32;
			diff  = nts2ll( &p->transmit_timestamp ) - now;
			if ( ( org  = nts2ll( &p->originate_timestamp ) ) && 
			     ( rcv  = nts2ll( &p->receive_timestamp   ) ) ) {
				/* correct for delays */
				diff += (rcv - org);
				diff >>=1;
			}
			*(long long*)usr_data = diff;
#ifdef NTP_DEBUG
			if ( llabs(diff) > llabs(rtems_ntp_max_diff) ) {
				rtems_ntp_max_t1 = org;
				rtems_ntp_max_t2 = rcv;
				rtems_ntp_max_t3 = nts2ll( &p->transmit_timestamp );
				rtems_ntp_max_diff = diff;
			}
#endif
		}
	}
	return 0;
}

unsigned
ntpdiff()
{
long long diff;
	if ( 0 == rtems_bsdnet_get_ntp(-1,diffTimeCb, &diff) ) {
		printf("%lli; %lis; %lins\n",diff, frac2sec(diff), frac2nsec(diff));
		printf("%lli; %lis; %lins\n",-diff, -frac2sec(-diff), -frac2nsec(-diff));
	}
	return (unsigned)diff;
}

unsigned
ntppack(void *p)
{
	return rtems_bsdnet_get_ntp(-1,copyPacketCb,p);
}

static rtems_task
ntpDaemon(rtems_task_argument unused)
{
rtems_status_code     rc;
rtems_interval        rate;
rtems_event_set       got;
long                  nsecs;

	rtems_clock_get( RTEMS_CLOCK_GET_TICKS_PER_SECOND , &rate );

	while ( RTEMS_TIMEOUT == (rc = rtems_event_receive(
									KILL_DAEMON,
									RTEMS_WAIT | RTEMS_EVENT_ANY,
									rate * rtems_ntp_daemon_sync_interval_secs,
									&got )) ) {
#if 0
		struct timespec       here;
		long			      secs;
		struct ntpPacketSmall p;
		if ( 0 == rtems_bsdnet_get_ntp(-1, copyPacketCb, &p) ) {
			locked_nano_time(&here);
			/* convert NTP to RTEMS seconds */
			secs   = ntohl(p.transmit_timestamp.integer);
			secs  -= rtems_bsdnet_timeoffset + UNIX_BASE_TO_NTP_BASE;
			secs  -= here.tv_sec;
			nsecs  = (unsigned long)((double)ntohl(p.transmit_timestamp.fraction)/4.294967295);
			nsecs -= here.tv_nsec;
			switch ( secs ) {
				case  1: nsecs += 1000000000; break;
				case -1: nsecs -= 1000000000;
				case  0: break;
				default:
					nsecs = secs > 0 ? MAXPHASE : -MAXPHASE;
			}
				
			locked_hardupdate( nsecs ); 

			if ( rtems_ntp_debug )
				printf("Update diff %li %sseconds\n", secs ? secs : nsecs, secs ? "" : "nano");
		}
#else
		long long diff;
		if ( 0 == rtems_bsdnet_get_ntp(-1, diffTimeCb, &diff) ) {
			if ( diff > nsec2frac(MAXPHASE) )
				nsecs =  MAXPHASE;
			else if ( diff < -nsec2frac(MAXPHASE) )
				nsecs = -MAXPHASE;
			else
				nsecs = frac2nsec(diff);

			locked_hardupdate( nsecs ); 

			if ( rtems_ntp_debug ) {
				if ( rtems_ntp_debug_file ) {
					/* log difference in microseconds */
					fprintf(rtems_ntp_debug_file,"%.5g\n", 1000000.*(double)diff/2./(double)(1<<31));
					fflush(rtems_ntp_debug_file);
				} else {
					long secs = frac2sec(diff);
					printf("Update diff %li %sseconds\n", secs ? secs : nsecs, secs ? "" : "nano");
				}
			}
		}
#endif
	}
	if ( RTEMS_SUCCESSFUL == rc && (KILL_DAEMON==got) ) {
		rtems_semaphore_release(kill_sem);
		/* DONT delete ourselves; note the race condition - our creator
		 * who sent the KILL event might get the CPU and remove us from
		 * memory before we get a chance to delete!
		 */
	}
	rtems_task_suspend( RTEMS_SELF );
}

#ifdef USE_DECREMENTER
extern unsigned Clock_Decrementer_value;

unsigned rtems_ntp_ticker_misses = 0;

static unsigned	ticksAdjusted;

long
nano_time(struct timespec *tp)
{
unsigned flags;
unsigned pcc;
unsigned rtemsTicks;
unsigned long long pccl;

	rtems_interrupt_disable( flags );
#ifdef NTP_NANO
	*tp = TIMEVAR;
#else
	tp->tv_sec   = TIMEVAR.tv_sec;
	tp->tv_nsec  = TIMEVAR.tv_usec;
#endif
	PPC_Get_decrementer(pcc);
	rtemsTicks   = Clock_driver_ticks;
	rtems_interrupt_enable( flags );

#ifndef NTP_NANO
	tp->tv_nsec *= 1000;
#endif

	/* even correct if the decrementer has underflown */
	pcc = Clock_Decrementer_value - pcc;

	/* account for the number of ticks expired on which ntp_tick_adjust() has not been run yet */
	rtemsTicks = rtemsTicks - ticksAdjusted;

	pccl = pcc + Clock_Decrementer_value * (unsigned long long)rtemsTicks;

	/* convert to nanoseconds */
	pccl *= BSP_time_base_divisor * 1000000;
	pccl /= BSP_bus_frequency;

	tp->tv_sec  += pccl / NANOSECOND;
	tp->tv_nsec += pccl % NANOSECOND;

	if ( tp->tv_nsec >= NANOSECOND ) {
		tp->tv_nsec -= NANOSECOND;
		tp->tv_sec++;
	}

	return 0;
}

static rtems_task
tickerDaemon(rtems_task_argument unused)
{
int s;
rtems_id			pid;
rtems_status_code	rc;

	PARANOIA( rtems_rate_monotonic_create( rtems_build_name('n','t','p','T'), &pid ) );

	tickerRunning = 1;

	while ( tickerRunning ) {

		rc = rtems_rate_monotonic_period( pid, RATE_DIVISOR );

		if ( RTEMS_TIMEOUT == rc )
			rtems_ntp_ticker_misses++;

		s = splclock();

		ntp_tick_adjust(&TIMEVAR, 0);
		second_overflow(&TIMEVAR);

		ticksAdjusted = Clock_driver_ticks;

		splx(s);

	}
	PARANOIA( rtems_rate_monotonic_delete( pid ) );
	PARANOIA( rtems_semaphore_release( kill_sem ) );
	rtems_task_suspend( RTEMS_SELF );
}

#else

static rtems_task
tickerDaemon(rtems_task_argument unused)
{
int					s;
pcc_t				pcc;

	while ( 1 ) {

		/* if they want to kill this daemon, they send a zero sized message
		 */
		PARANOIA( rtems_message_queue_receive( rtems_ntp_mqueue,
										  &pcc,
										  &s,
										  RTEMS_WAIT,
										  RTEMS_NO_TIMEOUT ) );
		if ( 0 == s )
			break;

		s = splclock();

#ifdef USE_MICRO
		microset_from_saved(pcc, &TIMEVAR);
#endif

		ntp_tick_adjust(&TIMEVAR, 0);
		second_overflow(&TIMEVAR);

		splx(s);

	}

	/* they killed us */
	PARANOIA( rtems_semaphore_release( kill_sem ) );
	rtems_task_suspend( RTEMS_SELF );
}
#endif

void
_cexpModuleInitialize(void *handle)
{
struct timex          ntv;
struct timespec       initime;

	ntv.offset = 0;
	ntv.freq = 0;
	ntv.status = STA_PLL;
	ntv.constant = 0;
	ntv.modes = MOD_STATUS | MOD_NANO;

	rtems_ntp_debug_file = stdout;

#ifdef USE_PICTIMER
	hz = TIMER_FREQ;
#else
	{
	rtems_interval rate;
	rtems_clock_get( RTEMS_CLOCK_GET_TICKS_PER_SECOND , &rate );
	hz = rate / RATE_DIVISOR;
	}
#endif

	ntp_init();
	ntp_adjtime(&ntv);

#ifdef USE_PICTIMER
	if ( pictimerInstall() ) {
		return;
	}
#endif

	/* initialize time */
	if ( 0 == rtems_bsdnet_get_ntp(-1, 0, &initime) ) {
#ifdef NTP_NANO
		TIMEVAR = initime;
#else
		TIMEVAR.tv_sec  = initime.tv_sec;
		TIMEVAR.tv_usec = initime.tv_nsec/1000;
#endif
	}

	if ( RTEMS_SUCCESSFUL != rtems_semaphore_create(
								rtems_build_name('N','T','P','m'),
								1,
								RTEMS_LOCAL | RTEMS_BINARY_SEMAPHORE |
								RTEMS_PRIORITY | RTEMS_INHERIT_PRIORITY,
								0,
								&mutex_id ) ) {
		printf("Unable to create mutex\n");
		return;
	}

#ifndef USE_DECREMENTER
	if ( RTEMS_SUCCESSFUL != rtems_message_queue_create(
								rtems_build_name('N','T','P','q'),
								5,
								sizeof(pcc_t),
								RTEMS_FIFO | RTEMS_LOCAL,
								&rtems_ntp_mqueue) ) {
		printf("Unable to create message queue\n");
		return;
	}
#endif

	if ( RTEMS_SUCCESSFUL != rtems_task_create(
								rtems_build_name('C','L','K','d'),
								80,
								1000,
								RTEMS_DEFAULT_MODES,
								RTEMS_DEFAULT_ATTRIBUTES,
								&ticker_id) ||
	     RTEMS_SUCCESSFUL != rtems_task_start( ticker_id, tickerDaemon, 0) ) {
		printf("Clock Ticker daemon couldn't be started :-(\n");
		return;
	}

	if ( RTEMS_SUCCESSFUL != rtems_task_create(
								rtems_build_name('N','T','P','d'),
								120,
								1000,
								RTEMS_DEFAULT_MODES,
								RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT,
								&daemon_id) ||
	     RTEMS_SUCCESSFUL != rtems_task_start( daemon_id, ntpDaemon, 0) ) {
		printf("NTP daemon couldn't be started :-(\n");
		return;
	}

#ifdef USE_PICTIMER
	/* start timer */
	pictimerEnable(1);
#endif
}

int
_cexpModuleFinalize(void *handle)
{

#ifdef USE_PICTIMER
	if ( pictimerCleanup() ) {
		return -1;
	}
#endif

	if ( daemon_id ) {
		if ( RTEMS_SUCCESSFUL == rtems_semaphore_create(
								rtems_build_name('k','i','l','l'),
								0,
								RTEMS_LOCAL | RTEMS_SIMPLE_BINARY_SEMAPHORE,
								0,
								&kill_sem) ) {
			if ( daemon_id ) {
				PARANOIA( rtems_event_send( daemon_id, KILL_DAEMON ) );
				PARANOIA( rtems_semaphore_obtain( kill_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT ) );
				PARANOIA( rtems_task_delete( daemon_id ));
			}
			if ( ticker_id ) {
#ifndef USE_DECREMENTER
				int tmp;
				PARANOIA( rtems_message_queue_urgent( rtems_ntp_mqueue, &tmp, 0 ) );
#else
				tickerRunning = 0;
#endif
				/* end of aborting sequence */
				PARANOIA( rtems_semaphore_obtain( kill_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT ) );
				PARANOIA( rtems_task_delete( ticker_id ) );
			}
			PARANOIA( rtems_semaphore_release( kill_sem ) );
			PARANOIA( rtems_semaphore_delete( kill_sem ) );	
			return 0;
		}
	}

#ifndef USE_DECREMENTER
	if ( rtems_ntp_mqueue )
		PARANOIA( rtems_message_queue_delete( rtems_ntp_mqueue ) );
#endif

	if ( mutex_id )
		PARANOIA( rtems_semaphore_delete( mutex_id ) );
	return -1;
}

#ifdef NTP_NANO
#define UNITS	"nS"
#else
#define UNITS	"uS"
#endif

static char *yesno(struct timex *p, unsigned mask)
{
	return p->status & mask ? "YES" : "NO";
}


long dumpNtpTimex(FILE *f)
{
struct timex ntp;

	if ( !f )
		f = stdout;

	memset( &ntp, 0, sizeof(ntp) );
	if ( 0 == ntp_adjtime( &ntp ) ) {
		fprintf(stderr,"Current Timex Values:\n");
		fprintf(stderr,"            time offset %11li "UNITS"\n",  ntp.offset);
		fprintf(stderr,"       frequency offset %11.3f ""ppm""\n", (double)ntp.freq/PPM_SCALED);
		fprintf(stderr,"             max  error %11li ""uS""\n",   ntp.maxerror);
		fprintf(stderr,"        estimated error %11li ""uS""\n",   ntp.esterror);
		fprintf(stderr,"          poll interval %11i  ""S""\n",    1<<ntp.constant);
		fprintf(stderr,"              precision %11li "UNITS"\n",  ntp.precision);
		fprintf(stderr,"              tolerance %11.3f ""ppm""\n", (double)ntp.tolerance/PPM_SCALED);
		fprintf(stderr,"    PLL updates enabled %s\n", yesno(&ntp, STA_PLL));
		fprintf(stderr,"       FLL mode enabled %s\n", yesno(&ntp, STA_FLL));
		fprintf(stderr,"            insert leap %s\n", yesno(&ntp, STA_INS));
		fprintf(stderr,"            delete leap %s\n", yesno(&ntp, STA_DEL));
		fprintf(stderr,"   clock unsynchronized %s\n", yesno(&ntp, STA_UNSYNC));
		fprintf(stderr,"         hold frequency %s\n", yesno(&ntp, STA_FREQHOLD));
		fprintf(stderr,"   clock hardware fault %s\n", yesno(&ntp, STA_CLOCKERR));
		fprintf(stderr,     "          %ssecond resolution\n", ntp.status & STA_NANO ? " nano" : "micro");
		fprintf(stderr,"         operation mode %s\n", ntp.status & STA_MODE ? "FLL" : "PLL");
		fprintf(stderr,"           clock source %s\n", ntp.status & STA_CLK  ? "B"   : "A");
	}
	return 0;
}
