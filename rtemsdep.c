#include <rtems.h>
#include <bsp.h>
#include <bsp/bspExt.h>
#include <bsp/irq.h>
#include <rtems/rtems_bsdnet.h>
#include <netinet/in.h>
#include <string.h>

#include <cexp.h>

#include "kern.h"
#include "rtemsdep.h"
#include "timex.h"

#define TIMER_IVEC 					BSP_MISC_IRQ_LOWEST_OFFSET
#define TIMER_PRI 					6
#define UARG 						0

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

static rtems_id daemon_id;
static rtems_id kill_sem;

static unsigned long base_count;

static unsigned long timer_period_ns;

unsigned long rtems_ntp_nano_ticks;
unsigned      rtems_ntp_debug = 0;
unsigned      rtems_ntp_daemon_sync_interval_secs = DAEMON_SYNC_INTERVAL_SECS;
FILE		  *rtems_ntp_debug_file;


static void isr(void *arg)
{
	ntp_tick_adjust(&TIMEVAR, 0);
#ifdef NTP_NANO
	if (TIMEVAR.tv_nsec >= NANOSECOND)
#else
	if (TIMEVAR.tv_usec >= 1000000)
#endif /* NTP_NANO */
	{
		second_overflow(&TIMEVAR);
	}
#ifdef USE_MICRO
	microset();
#else
	rtems_ntp_nano_ticks = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Current_Count );
#endif
}


#ifndef USE_MICRO
long nano_time(struct timespec *tp)
{
long cnt,tgl;
int  flags;

	flags = CLOCK_INTERRUPT_DISABLE();
#ifdef NTP_NANO
	*tp = TIMEVAR;
#else
	tp->tv_sec  = TIMEVAR.tv_sec;
	tp->tv_nsec = TIMEVAR.tv_usec * 1000;
#endif
	tgl = cnt = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Current_Count );
	CLOCK_INTERRUPT_ENABLE(flags);
	
	cnt &= ~ OPENPIC_TIMER_TOGGLE;
	if ( (tgl ^ rtems_ntp_nano_ticks) & OPENPIC_TIMER_TOGGLE ) {
		/* timer rolled over but ISR hadn't had a chance to
		 * execute ntp_tick_adjust()
		 */
		cnt-=base_count;
	}
	/* OPENPIC timer runs backwards */
	cnt = base_count - cnt;
	/* accept some roundoff error - should we adjust the frequency ? */
	tp->tv_nsec += cnt * ( timer_period_ns /* + time_freq */ );
	/* assume the tick timer is running faster than 1Hz */
	if ( tp->tv_nsec >= NANOSECOND ) {
		tp->tv_nsec-=NANOSECOND;
		tp->tv_sec++;
	}
	return 0;
}
#endif

/*
 * RTEMS base: 1988, January 1
 *  UNIX base: 1970, January 1
 *   NTP base: 1900, January 1
 */
#define UNIX_BASE_TO_NTP_BASE (((70UL*365UL)+17UL) * (24*60*60))


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
		nano_time(&nowts);
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

static unsigned sec2tconst(int secs)
{
int tc;
	for ( tc = 0; secs > 0 && tc < MAXTC; secs>>=1 )
		tc++;
	return tc;
}

static rtems_task
ntpDaemon(rtems_task_argument sync)
{
rtems_status_code     rc;
rtems_interval        rate, interval = 0;
rtems_event_set       got;
long                  nsecs;

	rtems_clock_get( RTEMS_CLOCK_GET_TICKS_PER_SECOND , &rate );

	interval = rtems_ntp_daemon_sync_interval_secs;

	while ( RTEMS_TIMEOUT == (rc = rtems_event_receive(
									KILL_DAEMON,
									RTEMS_WAIT | RTEMS_EVENT_ANY,
									rate * interval,
									&got )) ) {

		if ( interval != rtems_ntp_daemon_sync_interval_secs ) {
			extern long time_constant;
			interval = rtems_ntp_daemon_sync_interval_secs;
			/* TODO: we probably should use ntp_adjtime() for this
			 *       but it seems like a lot of overhead!
			 */
			time_constant = sec2tconst(interval);
		}
#if 0
		struct timespec       here;
		long			      secs;
		struct ntpPacketSmall p;
		if ( 0 == rtems_bsdnet_get_ntp(-1, copyPacketCb, &p) ) {
			nano_time(&here);
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
				
			hardupdate( &TIMEVAR, nsecs ); 

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

			hardupdate( &TIMEVAR, nsecs ); 

			if ( rtems_ntp_debug ) {
				if ( rtems_ntp_debug_file ) {
					/* log difference in microseconds */
					fprintf(rtems_ntp_debug_file,"%.5g\n", 1000000.*(double)diff/2./(double)(1<<31));
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
	}
	/* DONT delete ourselves; note the race condition - our creator
	 * who sent the KILL event might get the CPU and remove us from
	 * memory before we get a chance to delete!
	 */
	rtems_task_suspend( RTEMS_SELF );
}

void
_cexpModuleInitialize(void *handle)
{
unsigned              tmp;
struct timex          ntv;
struct timespec       initime;

	ntv.offset = 0;
	ntv.freq = 0;
	ntv.status = STA_PLL;
	ntv.constant = sec2tconst( rtems_ntp_daemon_sync_interval_secs );
	ntv.modes = MOD_STATUS | MOD_NANO | MOD_TIMECONST;

	rtems_ntp_debug_file = stdout;

	ntp_init();
	ntp_adjtime(&ntv);

	base_count = (tmp = in_le32( &OpenPIC->Global.Timer_Frequency )) / TIMER_FREQ;
	timer_period_ns = NANOSECOND / tmp;
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Base_Count, OPENPIC_MASK | base_count );
	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Base_Count, base_count );
	/* map to 1st CPU */
	openpic_maptimer(TIMER_NO, 1);
	if ( 0 == bspExtInstallSharedISR( TIMER_IVEC, isr, UARG, BSPEXT_ISR_NONSHARED ) ) {
		openpic_inittimer( TIMER_NO, TIMER_PRI, TIMER_IVEC );
	tmp = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority );

	/* initialize time */
	if ( 0 == rtems_bsdnet_get_ntp(-1, 0, &initime) ) {
#ifdef NTP_NANO
		TIMEVAR = initime;
#else
		TIMEVAR.tv_sec  = initime.tv_sec;
		TIMEVAR.tv_usec = initime.tv_nsec/1000;
#endif
	}

	out_le32( &OpenPIC->Global.Timer[TIMER_NO].Vector_Priority, tmp & ~OPENPIC_MASK );
	printf("Timer at %p\n",&OpenPIC->Global.Timer[TIMER_NO]); 
	if ( RTEMS_SUCCESSFUL != rtems_task_create(
								rtems_build_name('N','T','P','d'),
								120,
								1000,
								RTEMS_DEFAULT_MODES,
								RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT,
								&daemon_id) ||
	     RTEMS_SUCCESSFUL != rtems_task_start( daemon_id, ntpDaemon, 0) ) {
			printf("daemon couldn't be started :-(\n");
		}
  } else {
	printf("ISR installation failed!\n");
  }
}

int
_cexpModuleFinalize(void *handle)
{
	openpic_inittimer( TIMER_NO, 0, 0 );
	openpic_maptimer( TIMER_NO, 0 );
	if ( !bspExtRemoveSharedISR(TIMER_IVEC, isr, UARG) ) {
		if ( RTEMS_SUCCESSFUL == rtems_semaphore_create(
									rtems_build_name('k','i','l','l'),
									0,
									RTEMS_LOCAL | RTEMS_SIMPLE_BINARY_SEMAPHORE,
									0,
									&kill_sem) ) {
			rtems_event_send( daemon_id, KILL_DAEMON );
			rtems_semaphore_obtain( kill_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT );
			rtems_semaphore_delete( kill_sem );	
			rtems_task_delete( daemon_id );
			return 0;
		}
	}
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
		fprintf(stderr,"            time offset %11li  "UNITS"\n", ntp.offset);
		fprintf(stderr,"       frequency offset %11.3f ""ppm""\n", (double)ntp.freq/PPM_SCALED);
		fprintf(stderr,"             max  error %11li  ""uS""\n",  ntp.maxerror);
		fprintf(stderr,"        estimated error %11li  ""uS""\n",  ntp.esterror);
		fprintf(stderr,"          poll interval %11i   ""S""\n",   1<<ntp.constant);
		fprintf(stderr,"              precision %11li  "UNITS"\n", ntp.precision);
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
