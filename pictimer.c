#include <rtems.h>
#include <bsp.h>
#include <bsp/bspExt.h>
#include <bsp/irq.h>
#include <rtems/rtems_bsdnet.h>
#include <netinet/in.h>
#include <string.h>

#include <cexp.h>

#include "pictimer.h"

#ifdef USE_PICTIMER
#include "timex.h"
#include "pcc.h"
#include "kern.h"
#include "rtemsdep.h"
#endif

#define TIMER_IVEC 					BSP_MISC_IRQ_LOWEST_OFFSET
#define TIMER_PRI 					6

#define NumberOf(arr)				(sizeof((arr))/sizeof((arr)[0]))

static unsigned long timer_period_ns;


unsigned rtems_ntp_pictimer_irqs_missed = 0;

extern rtems_id rtems_ntp_ticker_id;

#ifdef USE_PICTIMER

#ifndef USE_METHOD_B_FOR_DEMO
volatile unsigned nano_ticks;
#endif

static void clock_isr(void *arg)
{
#ifdef USE_METHOD_B_FOR_DEMO
	rtems_ntp_isr_snippet();
#endif

	if ( RTEMS_SUCCESSFUL != rtems_event_send(rtems_ntp_ticker_id, PICTIMER_SYNC_EVENT) )
		rtems_ntp_pictimer_irqs_missed++;
}

#ifndef USE_METHOD_B_FOR_DEMO

static unsigned long base_count;

pcc_t
getPcc()
{
pcc_t cnt,tgl;

	cnt = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Current_Count );
	tgl = cnt ^ nano_ticks;

	cnt &= ~ OPENPIC_TIMER_TOGGLE;
	if ( tgl & OPENPIC_TIMER_TOGGLE ) {
		/* timer rolled over but ticker task hadn't had a chance to
		 * execute ntp_tick_adjust() yet.
		 */
		cnt-=base_count;
	}
	/* OPENPIC timer runs backwards */
	cnt = base_count - cnt;

	return cnt;
}

pcc_t
setPccBase()
{
	nano_ticks = in_le32( &OpenPIC->Global.Timer[TIMER_NO].Current_Count );
	return base_count;
}
#endif
#endif

/* on SVGM; timer 3 is the watchdog -- reserve it */
static void (*timerConnected[4])() = { 0, 0, (void (*)(void*))-1, 0 };

int pictimerInstall(unsigned timer_no, int pri, int freq, void (*isr)(void*))
{
unsigned              tmp;

	if ( timer_no >= NumberOf(timerConnected) ) {
		fprintf(stderr,"Invalid timer instance %i; (0..3 allowed)\n", timer_no);
		return -1;
	}

	if ( timerConnected[timer_no] ) {
		fprintf(stderr,"OpenPIC Timer #%i already connected\n", timer_no);
		return -1;
	}

	if ( pri > 15 || pri < 1 ) {
		fprintf(stderr,"Invalid timer IRQ priority %i (1..15 allowed)\n",pri);
		return -1;
	}

	printf("Using OpenPIC Timer #%i at %p\n", timer_no, &OpenPIC->Global.Timer[timer_no]);

	timer_period_ns = 1000000000 / in_le32( &OpenPIC->Global.Timer_Frequency );

	/* freq < 0 means they want to specify a period in ticks directly */
	if ( freq < 0 )
		tmp = -freq;
	else
		tmp = in_le32( &OpenPIC->Global.Timer_Frequency ) / freq;

	out_le32( &OpenPIC->Global.Timer[timer_no].Base_Count, OPENPIC_MASK | tmp );
	out_le32( &OpenPIC->Global.Timer[timer_no].Base_Count, tmp );
	/* map to 1st CPU */
	openpic_maptimer(timer_no, 1);
	if ( 0 == bspExtInstallSharedISR( TIMER_IVEC + timer_no, isr, (void*)timer_no, BSPEXT_ISR_NONSHARED ) ) {
		openpic_inittimer( timer_no, pri, TIMER_IVEC + timer_no );
		timerConnected[timer_no] = isr;
		return 0;
	}
	fprintf(stderr,"Unable to install shared ISR for OpenPIC Timer #%i\n",timer_no);
	return -1;
}

#ifdef USE_PICTIMER
int
pictimerInstallClock(unsigned timer_no)
{
	if ( ! pictimerInstall(timer_no, TIMER_PRI, TIMER_FREQ, clock_isr) ) {
		base_count      = in_le32( &OpenPIC->Global.Timer[timer_no].Base_Count );
		base_count     &= ~OPENPIC_MASK;
		return 0;
	}
	return -1;
}
#endif

int
pictimerCleanup(unsigned timer_no)
{
	if ( timer_no >= NumberOf(timerConnected) || !timerConnected[timer_no] )
		return 0;

	openpic_inittimer( timer_no, 0, 0 );
	openpic_maptimer( timer_no, 0 );
	
	if ( bspExtRemoveSharedISR(TIMER_IVEC + timer_no, timerConnected[timer_no], (void*)timer_no) ) {
		fprintf(stderr,"Unable to remove shared ISR for OpenPIC Timer #%i\n",timer_no);
		return -1;
	}
	timerConnected[timer_no] = 0;
	return 0;
}

unsigned
pictimerEnable(unsigned timer_no, int v)
{
unsigned tmp;

	if ( timer_no > NumberOf(timerConnected) )
		return -1;

	tmp = in_le32( &OpenPIC->Global.Timer[timer_no].Vector_Priority );
	if ( v )
		out_le32( &OpenPIC->Global.Timer[timer_no].Vector_Priority, tmp & ~OPENPIC_MASK );
	return tmp;
}

unsigned
pictimerDisable(unsigned timer_no)
{
unsigned tmp;
	if ( timer_no > NumberOf(timerConnected) )
		return -1;

	tmp = in_le32( &OpenPIC->Global.Timer[timer_no].Vector_Priority );
	out_le32( &OpenPIC->Global.Timer[timer_no].Vector_Priority, tmp | OPENPIC_MASK );
	return !(tmp | OPENPIC_MASK);
}
